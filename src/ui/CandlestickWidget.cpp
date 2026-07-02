#include "CandlestickWidget.h"
#include "util/ShimmerTimer.h"
#include <QPainter>
#include <QLinearGradient>
#include <QDateTime>
#include <QKeyEvent>
#include <QMenu>
#include <QContextMenuEvent>
#include <algorithm>
#include <cmath>

CandlestickWidget::CandlestickWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);   // accept keyboard after click
    setCursor(Qt::CrossCursor);
    setStyleSheet("background: #080c12;");

    connect(&ShimmerTimer::instance(), &ShimmerTimer::tick,
            this, [this] { if (m_skelActive) update(); });
}

void CandlestickWidget::setInterval(int seconds)
{
    m_intervalSec    = seconds;
    m_candles.clear();
    m_footprints.clear();
    m_hasCurrent     = false;
    m_panOffset      = 0;
    m_seedBoundaryMs = 0;
    m_skelActive     = true;
    update();
    emit intervalChanged(seconds);
}

void CandlestickWidget::seedCandles(const std::vector<Candle> &candles)
{
    m_candles.clear();
    m_footprints.clear();
    m_hasCurrent     = false;
    m_panOffset      = 0;
    m_vwap.reset();
    m_seedBoundaryMs = 0;
    if (!candles.empty()) m_skelActive = false;

    for (auto c : candles) {
        // Reconstruct running VWAP from seeded volume.
        // Use buy+sell split when available (Binance seed); fall back to the
        // total volume field (Bybit seed has volume but no split).
        // Typical price (H+L+C)/3 for consistency with the paint-time calc.
        const int    day = static_cast<int>(c.openTimeMs / (86400LL * 1000LL));
        const double vol = (c.buyVol + c.sellVol > 0) ? (c.buyVol + c.sellVol) : c.volume;
        m_vwap.add((c.high + c.low + c.close) / 3.0, vol, day);
        c.vwap = m_vwap.value(c.close);

        m_candles.push_back(c);
        if (static_cast<int>(m_candles.size()) > kMaxCandles)
            m_candles.pop_front();
    }

    // Record boundary so paintEvent can draw a seed/live divider line
    if (!m_candles.empty())
        m_seedBoundaryMs = m_candles.back().openTimeMs;

    update();
}

void CandlestickWidget::closeCandle()
{
    if (!m_hasCurrent) return;
    m_current.vwap     = m_vwap.value(m_current.close);
    m_current.complete = true;
    m_candles.push_back(m_current);
    if (static_cast<int>(m_candles.size()) > kMaxCandles) {
        m_footprints.erase(m_candles.front().openTimeMs);
        m_candles.pop_front();
    }
    m_hasCurrent = false;
}

void CandlestickWidget::addMidPrice(double mid, qint64 ts)
{
    if (mid <= 0) return;
    m_skelActive = false;
    const qint64 intervalMs = static_cast<qint64>(m_intervalSec) * 1000LL;
    const qint64 barStart   = (ts / intervalMs) * intervalMs;

    const double prevMid = (m_lastMid > 0.0) ? m_lastMid : mid;

    if (!m_hasCurrent) {
        m_current = Candle{};
        m_current.openTimeMs = barStart;
        m_current.open = m_current.high = m_current.low = m_current.close = mid;
        m_hasCurrent = true;
    } else if (barStart > m_current.openTimeMs) {
        closeCandle();
        m_current = Candle{};
        m_current.openTimeMs = barStart;
        m_current.open = m_current.high = m_current.low = m_current.close = mid;
        m_hasCurrent = true;
    } else {
        m_current.high  = std::max(m_current.high,  mid);
        m_current.low   = std::min(m_current.low,   mid);
        m_current.close = mid;
    }

    checkAlertCrossings(prevMid, mid);
    m_lastMid = mid;
    update();
}

void CandlestickWidget::checkAlertCrossings(double prev, double curr)
{
    if (m_alerts.empty() || prev <= 0.0) return;
    // Iterate in reverse so erasing doesn't invalidate remaining indices
    for (int i = static_cast<int>(m_alerts.size()) - 1; i >= 0; --i) {
        double ap = m_alerts[i];
        bool crossed = (prev < ap && curr >= ap) || (prev > ap && curr <= ap);
        if (crossed) {
            emit alertTriggered(ap);
            m_alerts.erase(m_alerts.begin() + i); // auto-remove after firing
        }
    }
}

void CandlestickWidget::contextMenuEvent(QContextMenuEvent *e)
{
    // Only react to right-clicks in the chart body (not header or time axis)
    const int headerH = 30;
    const int timeH   = 20;
    const int volH    = std::max(20, height() / 6);
    const int priceW  = 66;
    const int chartW  = width() - priceW;
    const int mainH   = height() - headerH - timeH - volH;

    if (e->pos().x() >= chartW || e->pos().y() <= headerH ||
        e->pos().y() >= headerH + mainH)
        return;

    // Recompute price from cursor position
    // We need to know the current visible price range — approximate from last candle set
    std::vector<const Candle *> all;
    for (const auto &c : m_candles) all.push_back(&c);
    if (m_hasCurrent) all.push_back(&m_current);
    if (all.empty()) return;

    const int numActual = static_cast<int>(all.size());
    const int maxVis    = std::max(1, chartW / m_candleW);
    const int endIdx    = numActual - 1 - m_panOffset;
    const int startIdx  = std::max(0, endIdx - maxVis + 1);

    double minP = all[startIdx < numActual ? startIdx : 0]->low;
    double maxP = all[startIdx < numActual ? startIdx : 0]->high;
    for (int i = startIdx; i <= endIdx && i < numActual; ++i) {
        minP = std::min(minP, all[i]->low);
        maxP = std::max(maxP, all[i]->high);
    }
    double pad = std::max((maxP - minP) * 0.08, 0.5);
    minP -= pad; maxP += pad;

    double alertPrice = maxP - static_cast<double>(e->pos().y() - headerH) / mainH * (maxP - minP);

    // Decimal precision
    int dec = 1;
    double rng = maxP - minP - 2 * pad;
    if      (rng < 0.01) dec = 4;
    else if (rng < 0.1)  dec = 3;
    else if (rng < 1.0)  dec = 2;
    else if (rng < 10.0) dec = 1;
    else                 dec = 0;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background:#0d1520; border:1px solid #1e2a3a; color:#c0ccd8; }"
        "QMenu::item:selected { background:#162030; }"
        "QMenu::separator { height:1px; background:#1e2a3a; margin:2px 0; }");

    auto *setAct = menu.addAction(
        QString("Set Alert at %1").arg(alertPrice, 0, 'f', dec));
    menu.addSeparator();
    auto *clearAct = menu.addAction(
        QString("Clear All Alerts (%1)").arg(m_alerts.size()));

    auto *chosen = menu.exec(e->globalPos());
    if (chosen == setAct) {
        m_alerts.push_back(alertPrice);
    } else if (chosen == clearAct) {
        m_alerts.clear();
    }
    update();
}

void CandlestickWidget::addTrade(const TradeInfo &t)
{
    m_vwap.add(t.price, t.size, static_cast<int>(t.timestampMs / (86400LL * 1000LL)));

    if (!m_hasCurrent) return;
    const qint64 intervalMs = static_cast<qint64>(m_intervalSec) * 1000LL;
    const qint64 barStart   = (t.timestampMs / intervalMs) * intervalMs;
    if (barStart == m_current.openTimeMs) {
        if (t.isBuy) m_current.buyVol  += t.size;
        else         m_current.sellVol += t.size;
        m_current.volume += t.size;

        // Footprint: bucket trade into price-level grid
        m_fpTickSize = fpTickSize(t.price);
        const double snap = std::round(t.price / m_fpTickSize) * m_fpTickSize;
        auto &row = m_footprints[m_current.openTimeMs][snap];
        if (t.isBuy) row.buy  += t.size;
        else         row.sell += t.size;
    }
}

// ── Interaction ───────────────────────────────────────────────────────────────

void CandlestickWidget::wheelEvent(QWheelEvent *e)
{
    const bool zoomIn = e->angleDelta().y() > 0;
    // Each step: ±2px per candle, clamped to [2, 40]
    m_candleW = qBound(2, m_candleW + (zoomIn ? 2 : -2), 40);
    update();
    e->accept();
}

void CandlestickWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        // Legend indicator toggles (click to enable/disable)
        for (int i = 0; i < m_legendCount; ++i) {
            if (m_legendRects[i].contains(e->pos())) {
                m_legendEnabled[i] = !m_legendEnabled[i];
                update();
                QWidget::mousePressEvent(e);
                return;
            }
        }
        // Interval buttons in header
        static const struct { int secs; int btnW; } kBtns[] = {
            {60,24},{300,24},{900,28},{3600,24}
        };
        if (e->pos().y() < 30) {
            int bx = 8;
            for (const auto &b : kBtns) {
                if (e->pos().x() >= bx && e->pos().x() < bx + b.btnW) {
                    setInterval(b.secs);
                    return;
                }
                bx += b.btnW + 4;
            }
        }
        // Start drag-pan
        m_isDragging = true;
        m_dragOrigin = e->pos();
        m_panAtDrag  = m_panOffset;
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(e);
}

void CandlestickWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        setCursor(Qt::CrossCursor);
    }
    QWidget::mouseReleaseEvent(e);
}

// Double-click anywhere in the chart body: snap back to live edge and reset zoom.
// This is the standard "fit view" gesture on TradingView / Bloomberg.
void CandlestickWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && e->pos().y() > 30) {
        m_panOffset = 0;
        m_candleW   = 8;   // default candle width
        update();
    }
    QWidget::mouseDoubleClickEvent(e);
}

void CandlestickWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_mousePos = e->pos();
    m_mouseIn  = true;

    if (m_isDragging) {
        const int dx      = e->pos().x() - m_dragOrigin.x();
        const int shifted = dx / std::max(1, m_candleW);
        const int total = static_cast<int>(m_candles.size()) + (m_hasCurrent ? 1 : 0);
        m_panOffset = qBound(0, m_panAtDrag + shifted, std::max(0, total - 1));
    } else if (e->pos().y() < 30) {
        // Check which interval button (if any) the mouse is over
        static const int kBtnWidths[] = {24,24,28,24};
        m_hoveredBtn = -1;
        int bx = 8;
        for (int bi = 0; bi < 4; ++bi) {
            if (e->pos().x() >= bx && e->pos().x() < bx + kBtnWidths[bi]) {
                m_hoveredBtn = bi; break;
            }
            bx += kBtnWidths[bi] + 4;
        }
        setCursor(m_hoveredBtn >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    } else {
        // Pointer cursor when hovering over a legend toggle pill
        bool overLegend = false;
        for (int i = 0; i < m_legendCount; ++i) {
            if (m_legendRects[i].contains(e->pos())) { overLegend = true; break; }
        }
        setCursor(overLegend ? Qt::PointingHandCursor : Qt::CrossCursor);
    }
    update();
}

void CandlestickWidget::leaveEvent(QEvent *)
{
    m_mouseIn    = false;
    m_isDragging = false;
    m_hoveredBtn = -1;
    setCursor(Qt::CrossCursor);
    update();
}

void CandlestickWidget::keyPressEvent(QKeyEvent *e)
{
    const int total = static_cast<int>(m_candles.size()) + (m_hasCurrent ? 1 : 0);

    switch (e->key()) {
    // Zoom
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        m_candleW = qBound(2, m_candleW + 2, 40);
        update();
        break;
    case Qt::Key_Minus:
        m_candleW = qBound(2, m_candleW - 2, 40);
        update();
        break;

    // Pan — left moves further into history, right back toward live edge
    case Qt::Key_Left:
        m_panOffset = qBound(0, m_panOffset + 3, std::max(0, total - 1));
        update();
        break;
    case Qt::Key_Right:
        m_panOffset = std::max(0, m_panOffset - 3);
        update();
        break;

    // Jump to live edge
    case Qt::Key_Escape:
    case Qt::Key_0:
        m_panOffset = 0;
        update();
        break;

    // Clear all price alerts
    case Qt::Key_Delete:
        if (!m_alerts.empty()) {
            m_alerts.clear();
            update();
        }
        break;

    default:
        QWidget::keyPressEvent(e);
    }
}

QString CandlestickWidget::formatTime(qint64 ms) const
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(
        m_intervalSec >= 3600 ? "HH:mm" : "HH:mm");
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void CandlestickWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();
    p.fillRect(rect(), QColor(0x08, 0x0c, 0x12));

    // ── Layout ────────────────────────────────────────────────────────────────
    const int headerH = 30;
    const int priceW  = 66;
    const int timeH   = 20;
    const int volH    = std::max(16, h / 9);   // volume bars
    const int deltaH  = std::max(18, h / 11);  // cumulative delta strip
    const int rsiH    = std::max(44, h / 8);   // RSI sub-pane
    const int chartW  = w - priceW;
    const int mainH   = h - headerH - timeH - volH - deltaH - rsiH;

    // ── Header ────────────────────────────────────────────────────────────────
    p.fillRect(0, 0, w, headerH, QColor(0x05, 0x08, 0x10));
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(0, headerH - 1, w, headerH - 1);

    static const struct { int secs; const char *label; int btnW; } kIntervals[] = {
        {60,"1m",24},{300,"5m",24},{900,"15m",28},{3600,"1h",24}
    };
    {
        QFont bf; bf.setFamily("Segoe UI"); bf.setPointSize(10);
        p.setFont(bf);
        int bx = 8;
        for (int bi = 0; bi < 4; ++bi) {
            const auto &iv  = kIntervals[bi];
            const bool active  = (iv.secs == m_intervalSec);
            const bool hovered = (bi == m_hoveredBtn);
            if (hovered && !active)
                p.fillRect(bx - 2, 3, iv.btnW + 4, headerH - 6, QColor(0x18, 0x28, 0x3e));
            p.setPen(active  ? QColor(0x5a, 0xa3, 0xf5) :
                     hovered ? QColor(0x58, 0x78, 0x9a) :
                               QColor(0x4a, 0x64, 0x80));
            p.drawText(QRect(bx, 0, iv.btnW, headerH), Qt::AlignCenter, iv.label);
            bx += iv.btnW + 4;
        }
    }

    // Collect candles
    std::vector<const Candle *> all;
    all.reserve(m_candles.size() + 1);
    for (const auto &c : m_candles) all.push_back(&c);
    if (m_hasCurrent) all.push_back(&m_current);

    if (all.empty()) {
        paintSkeleton(p);
        return;
    }

    // ── Layout: zoom + pan ────────────────────────────────────────────────────
    const int candleW   = m_candleW;
    const int bodyW     = std::max(1, candleW - (candleW >= 6 ? 2 : 1));
    const int maxVis    = std::max(1, chartW / candleW);
    const int numActual = static_cast<int>(all.size());

    // Clamp pan so we can't scroll past the oldest candle
    m_panOffset = qBound(0, m_panOffset, std::max(0, numActual - 1));

    // endIdx: the rightmost visible candle index (pan=0 → newest)
    const int endIdx   = numActual - 1 - m_panOffset;
    const int startIdx = std::max(0, endIdx - maxVis + 1);

    // Right-aligned: endIdx sits one slot from the right edge
    auto xOfIdx = [&](int i) -> int {
        return chartW - (endIdx - i) * candleW - candleW / 2;
    };

    // ── Price range of visible candles ────────────────────────────────────────
    double minP = all[startIdx]->low, maxP = all[startIdx]->high;
    for (int i = startIdx; i <= endIdx && i < numActual; ++i) {
        minP = std::min(minP, all[i]->low);
        maxP = std::max(maxP, all[i]->high);
    }
    double pad = std::max((maxP - minP) * 0.08, 0.5);
    minP -= pad; maxP += pad;
    const double priceRange = maxP - minP;

    // Decimal precision from visible price range
    int priceDec = 1;
    {
        double rng = maxP - minP - 2 * pad;
        if      (rng < 0.01)  priceDec = 4;
        else if (rng < 0.1)   priceDec = 3;
        else if (rng < 1.0)   priceDec = 2;
        else if (rng < 10.0)  priceDec = 1;
        else                  priceDec = 0;
    }

    // Max volume for scaling — prefer buy+sell split when available, fall back
    // to the total volume field (set by REST-seeded candles from Bybit).
    // Use log scale so a high-volume volatile burst doesn't crush historical bars
    // to near-zero height (e.g. a 10× spike would still only halve shorter bars).
    double maxVolLog = 1e-9;
    for (int i = startIdx; i <= endIdx && i < numActual; ++i) {
        const double v = std::max(all[i]->buyVol + all[i]->sellVol, all[i]->volume);
        if (v > 0) maxVolLog = std::max(maxVolLog, std::log1p(v));
    }

    auto yOfPrice = [&](double price) -> int {
        return headerH + static_cast<int>(mainH * (1.0 - (price - minP) / priceRange));
    };
    auto yClamp = [&](int y) { return qBound(headerH, y, headerH + mainH); };

    // ── Pre-compute indicators (single pass over all candles) ─────────────────
    // Bollinger Bands (20-period, 2σ) — O(n) with running sum + sum-of-squares
    const int bbPeriod = 20;
    std::vector<double> bbMid(numActual, 0), bbUpper(numActual, 0), bbLower(numActual, 0);
    std::vector<bool>   bbValid(numActual, false);
    {
        double rSum = 0, rSumSq = 0;
        for (int i = 0; i < numActual; ++i) {
            const double c = all[i]->close;
            rSum   += c;  rSumSq += c * c;
            if (i >= bbPeriod) {
                const double old = all[i - bbPeriod]->close;
                rSum -= old;  rSumSq -= old * old;
            }
            if (i >= bbPeriod - 1) {
                const double mean  = rSum / bbPeriod;
                const double sigma = std::sqrt(std::max(rSumSq / bbPeriod - mean * mean, 0.0));
                bbMid[i]   = mean;
                bbUpper[i] = mean + 2.0 * sigma;
                bbLower[i] = mean - 2.0 * sigma;
                bbValid[i] = true;
            }
        }
    }

    // RSI (14-period, Wilder smoothing) — O(n)
    const int rsiPeriod = 14;
    std::vector<double> rsiVals(numActual, 50.0);
    if (numActual > rsiPeriod) {
        double avgGain = 0, avgLoss = 0;
        for (int i = 1; i <= rsiPeriod; ++i) {
            const double chg = all[i]->close - all[i-1]->close;
            if (chg > 0) avgGain += chg; else avgLoss -= chg;
        }
        avgGain /= rsiPeriod;  avgLoss /= rsiPeriod;
        rsiVals[rsiPeriod] = avgLoss < 1e-10 ? 100.0
                             : 100.0 - 100.0 / (1.0 + avgGain / avgLoss);
        for (int i = rsiPeriod + 1; i < numActual; ++i) {
            const double chg = all[i]->close - all[i-1]->close;
            avgGain = (avgGain * (rsiPeriod - 1) + std::max( chg, 0.0)) / rsiPeriod;
            avgLoss = (avgLoss * (rsiPeriod - 1) + std::max(-chg, 0.0)) / rsiPeriod;
            rsiVals[i] = avgLoss < 1e-10 ? 100.0
                         : 100.0 - 100.0 / (1.0 + avgGain / avgLoss);
        }
    }

    // ── Grid lines ────────────────────────────────────────────────────────────
    p.setPen(QPen(QColor(0x14, 0x1e, 0x2e), 1));
    for (int step = 1; step <= 4; ++step) {
        int gy = headerH + mainH * step / 5;
        p.drawLine(0, gy, chartW, gy);
    }

    // ── Bollinger Bands (drawn behind candles) ────────────────────────────────
    if (m_legendEnabled[3]) {
        QPolygonF upper, lower, mid;
        for (int i = startIdx; i <= endIdx && i < numActual; ++i) {
            if (!bbValid[i]) continue;
            const double x = xOfIdx(i);
            upper.append(QPointF(x, yClamp(yOfPrice(bbUpper[i]))));
            lower.append(QPointF(x, yClamp(yOfPrice(bbLower[i]))));
            mid.append  (QPointF(x, yClamp(yOfPrice(bbMid[i]))));
        }
        if (upper.size() >= 2) {
            p.setRenderHint(QPainter::Antialiasing, true);

            // Filled band (translucent steel-blue)
            QPolygonF band = upper;
            for (int i = lower.size() - 1; i >= 0; --i)
                band.append(lower[i]);
            p.setBrush(QColor(0x30, 0x60, 0xa8, 22));
            p.setPen(Qt::NoPen);
            p.drawPolygon(band);
            p.setBrush(Qt::NoBrush);

            // Upper / lower rails
            p.setPen(QPen(QColor(0x44, 0x80, 0xcc, 110), 1.0));
            p.drawPolyline(upper);
            p.drawPolyline(lower);

            // Middle SMA (dotted)
            p.setPen(QPen(QColor(0x55, 0x95, 0xdd, 100), 1.0, Qt::DotLine));
            p.drawPolyline(mid);

            p.setRenderHint(QPainter::Antialiasing, false);
        }
    }

    // ── Candles ───────────────────────────────────────────────────────────────
    for (int i = startIdx; i <= endIdx && i < numActual; ++i) {
        const Candle *c = all[i];
        const int cx    = xOfIdx(i);
        if (cx + bodyW / 2 < 0 || cx - bodyW / 2 > chartW) continue;

        const bool   bull    = (c->close >= c->open);
        const QColor bodyCol = bull ? QColor(0x00, 0xcc, 0x70) : QColor(0xe0, 0x40, 0x50);
        const QColor wickCol = bull ? QColor(0x00, 0x99, 0x55, 200) : QColor(0xb0, 0x30, 0x40, 200);

        const int yH   = yOfPrice(c->high);
        const int yL   = yOfPrice(c->low);
        const int yO   = yOfPrice(c->open);
        const int yC   = yOfPrice(c->close);
        const int yTop = std::min(yO, yC);
        const int yBot = std::max(yO, yC);
        const int bH   = std::max(1, yBot - yTop);
        const int x1   = cx - bodyW / 2;

        // Wick (always)
        p.setPen(wickCol);
        p.drawLine(cx, yH, cx, yTop);
        p.drawLine(cx, yBot, cx, yL);

        // ── Footprint mode (zoom ≥ 16 px per candle, live data only) ─────────
        const auto fpIt = (candleW >= 28 && c->openTimeMs > m_seedBoundaryMs)
                          ? m_footprints.find(c->openTimeMs)
                          : m_footprints.end();
        const bool hasFootprint = (fpIt != m_footprints.end()) && !fpIt->second.empty();

        if (hasFootprint) {
            // Dim body background — candle direction still visible
            p.fillRect(x1, yTop, bodyW, bH,
                       bull ? QColor(0x00, 0x55, 0x28, 60) : QColor(0x60, 0x18, 0x18, 60));
            p.setPen(QPen(bodyCol, 1));
            p.drawRect(x1, yTop, bodyW - 1, bH - 1);

            const auto &fp = fpIt->second;
            double fpMax = 1e-9;
            for (const auto &[pr, row] : fp)
                fpMax = std::max(fpMax, row.buy + row.sell);

            const int halfW = std::max(1, (bodyW - 2) / 2);
            double pocVol = 0;  int pocRow = 0;

            for (const auto &[tickPrice, row] : fp) {
                if (tickPrice < minP || tickPrice > maxP) continue;
                const int rowTop = yOfPrice(tickPrice + m_fpTickSize);
                const int rowBot = yOfPrice(tickPrice);
                const int rh     = std::max(1, rowBot - rowTop);
                const double tot = row.buy + row.sell;
                if (tot > pocVol) { pocVol = tot; pocRow = rowTop; }
                const int bW = static_cast<int>(halfW * row.buy  / fpMax);
                const int sW = static_cast<int>(halfW * row.sell / fpMax);
                if (sW > 0) p.fillRect(cx - sW, rowTop, sW, rh, QColor(0xcc, 0x30, 0x30, 200));
                if (bW > 0) p.fillRect(cx,       rowTop, bW, rh, QColor(0x00, 0xaa, 0x55, 200));
            }
            // POC row highlight (brightest price level)
            if (pocVol > 0) {
                p.setPen(QPen(QColor(0xff, 0xe0, 0x40, 200), 1));
                p.drawLine(x1, pocRow, x1 + bodyW, pocRow);
            }
        } else {
            if (bull) {
                // Bullish: solid filled body
                p.fillRect(x1, yTop, bodyW, bH, bodyCol);
            } else {
                // Bearish: hollow body (outline only) — non-color direction cue
                p.fillRect(x1, yTop, bodyW, bH, QColor(0xe0, 0x40, 0x50, 28));
                if (bodyW >= 2) {
                    p.setPen(QPen(bodyCol, 1));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(x1, yTop, bodyW - 1, bH - 1);
                }
            }
        }

        // Volume bars — use buy/sell split when available, fall back to c->volume
        // (REST-seeded Bybit candles have volume but no buyVol/sellVol)
        {
            const double bvs    = c->buyVol + c->sellVol;
            const double totalV = bvs > 0 ? bvs : c->volume;
            if (totalV > 0 && maxVolLog > 0) {
                const int fullH = static_cast<int>(volH * std::log1p(totalV) / maxVolLog);
                const int volY  = headerH + mainH;
                const int baseline = volY + volH;   // bottom of the volume section
                if (bvs > 0) {
                    // Stacked buy (green, bottom) + sell (red, above) — bars grow upward
                    const int buyH  = static_cast<int>(fullH * c->buyVol / totalV);
                    const int sellH = fullH - buyH;
                    p.fillRect(x1, baseline - buyH,  bodyW, buyH,  QColor(0x00, 0xa8, 0x55, 150));
                    p.fillRect(x1, baseline - fullH, bodyW, sellH, QColor(0xcc, 0x30, 0x30, 150));
                } else {
                    // Total volume only — colour matches candle direction (Bybit seed)
                    QColor volCol = bull ? QColor(0x00, 0xa8, 0x55, 110)
                                        : QColor(0xcc, 0x30, 0x30, 110);
                    p.fillRect(x1, baseline - fullH, bodyW, fullH, volCol);
                }
            }
        }
    }

    // Volume baseline
    p.setPen(QColor(0x18, 0x24, 0x30));
    p.drawLine(0, headerH + mainH, chartW, headerH + mainH);

    // ── Seed / live boundary marker ───────────────────────────────────────────
    // A subtle vertical line + "LIVE" chip marks where REST-seeded history ends
    // and WebSocket-live data begins.  Makes it immediately obvious that the
    // two sections can look different (scale, volatility, volume palette, etc.).
    if (m_seedBoundaryMs > 0) {
        // Find the first candle that is strictly AFTER the seed boundary.
        // Search the full array (not just the visible window) so the line
        // stays correct when the user has panned past all seed candles.
        int bndIdx = -1;
        for (int i = 0; i < numActual; ++i) {
            if (all[i]->openTimeMs > m_seedBoundaryMs) { bndIdx = i; break; }
        }
        if (bndIdx >= 0) {
            const int bx = xOfIdx(bndIdx) - candleW / 2;
            if (bx > 0 && bx < chartW) {
                // Dashed vertical line spanning chart + volume strip
                p.setPen(QPen(QColor(0x40, 0x68, 0x98, 100), 1, Qt::DashLine));
                p.drawLine(bx, headerH, bx, headerH + mainH + volH);

                // "LIVE" chip at the top of the line
                QFont lf; lf.setFamily("Consolas"); lf.setPointSize(6); lf.setBold(true);
                p.setFont(lf);
                const int chipW = 28, chipH = 12;
                const int chipX = std::min(bx, chartW - chipW - 1);
                p.fillRect(chipX, headerH + 2, chipW, chipH, QColor(0x10, 0x22, 0x38, 200));
                p.setPen(QColor(0x40, 0x80, 0xc0, 200));
                p.drawText(QRect(chipX, headerH + 2, chipW, chipH),
                           Qt::AlignCenter, "LIVE");
            }
        }
    }

    // ── Cumulative Delta strip ────────────────────────────────────────────────
    {
        const int deltaY = headerH + mainH + volH;

        p.fillRect(0, deltaY, chartW, deltaH, QColor(0x06, 0x0a, 0x12));
        p.setPen(QColor(0x14, 0x1e, 0x2e));
        p.drawLine(0, deltaY, chartW, deltaY);

        // Compute running cumulative delta; reset at UTC session boundary
        std::vector<double> cumDelta(numActual, 0.0);
        {
            double  running = 0.0;
            qint64  lastDay = -1;
            for (int i = 0; i < numActual; ++i) {
                qint64 day = all[i]->openTimeMs / 86400000LL;
                if (day != lastDay) { running = 0.0; lastDay = day; }
                running     += all[i]->buyVol - all[i]->sellVol;
                cumDelta[i]  = running;
            }
        }

        // Visible range
        double minD = 0.0, maxD = 0.0;
        for (int i = startIdx; i <= endIdx && i < numActual; ++i) {
            minD = std::min(minD, cumDelta[i]);
            maxD = std::max(maxD, cumDelta[i]);
        }
        const double dRange = std::max(maxD - minD, 1e-9);

        // Zero line
        const int zeroY = deltaY + static_cast<int>(deltaH * maxD / dRange);
        if (zeroY >= deltaY && zeroY <= deltaY + deltaH) {
            p.setPen(QPen(QColor(0x28, 0x3a, 0x52, 160), 1, Qt::DashLine));
            p.drawLine(0, zeroY, chartW, zeroY);
        }

        // Bars
        for (int i = startIdx; i <= endIdx && i < numActual; ++i) {
            const int cx = xOfIdx(i);
            if (cx + bodyW / 2 < 0 || cx - bodyW / 2 > chartW) continue;
            const double dv  = cumDelta[i];
            const int    yT  = deltaY + static_cast<int>(deltaH * (maxD - std::max(dv, 0.0)) / dRange);
            const int    yB  = deltaY + static_cast<int>(deltaH * (maxD - std::min(dv, 0.0)) / dRange);
            const int    bh  = std::max(1, yB - yT);
            p.fillRect(cx - bodyW / 2, yT, bodyW, bh,
                       dv >= 0 ? QColor(0x00, 0xa0, 0x55, 170)
                                : QColor(0xcc, 0x28, 0x28, 170));
        }

        // Label
        QFont lf; lf.setFamily("Segoe UI"); lf.setPointSize(8);
        lf.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
        p.setFont(lf);
        p.setPen(QColor(0x28, 0x3a, 0x52));
        p.drawText(QRect(2, deltaY + 2, 50, deltaH - 4), Qt::AlignLeft | Qt::AlignTop, "CUM \xce\x94");

        // Current delta value on right axis
        if (endIdx >= 0 && endIdx < numActual) {
            double lastD = cumDelta[endIdx];
            bool   pos   = lastD >= 0;
            QFont vf; vf.setFamily("Consolas"); vf.setPointSize(9);
            p.setFont(vf);
            p.setPen(pos ? QColor(0x00, 0xcc, 0x70) : QColor(0xe0, 0x40, 0x50));
            p.drawText(QRect(chartW + 2, deltaY, priceW - 4, deltaH),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       (pos ? "+" : "") + QString::number(lastD, 'f', 2));
        }
    }

    // ── RSI sub-pane ──────────────────────────────────────────────────────────
    {
        const int rsiY = headerH + mainH + volH + deltaH;

        p.fillRect(0, rsiY, chartW, rsiH, QColor(0x05, 0x08, 0x11));
        p.setPen(QColor(0x14, 0x1e, 0x2e));
        p.drawLine(0, rsiY, chartW, rsiY);

        // Helper: RSI value → Y pixel within strip
        auto rsiToY = [&](double v) -> int {
            return rsiY + static_cast<int>((1.0 - v / 100.0) * rsiH);
        };

        // Overbought / oversold reference lines
        const int y70 = rsiToY(70), y50 = rsiToY(50), y30 = rsiToY(30);
        p.setPen(QPen(QColor(0xcc, 0x30, 0x30, 60), 1, Qt::DashLine));
        p.drawLine(0, y70, chartW, y70);
        p.setPen(QPen(QColor(0x28, 0x38, 0x50, 60), 1, Qt::DotLine));
        p.drawLine(0, y50, chartW, y50);
        p.setPen(QPen(QColor(0x00, 0xaa, 0x55, 60), 1, Qt::DashLine));
        p.drawLine(0, y30, chartW, y30);

        // RSI line — colour switches by zone (green/neutral/red)
        p.setRenderHint(QPainter::Antialiasing, true);
        QPolygonF rsiPoly;
        QColor    prevCol;
        for (int i = startIdx; i <= endIdx && i < numActual; ++i) {
            if (i < rsiPeriod) continue;
            const double rv  = rsiVals[i];
            const QColor col = rv > 70 ? QColor(0xe0, 0x50, 0x50, 220) :
                               rv < 30 ? QColor(0x00, 0xcc, 0x70, 220) :
                                         QColor(0x80, 0xaa, 0xe0, 220);
            const QPointF pt(xOfIdx(i), rsiToY(rv));
            if (!rsiPoly.isEmpty() && col != prevCol) {
                // Flush segment with the previous colour before switching
                if (rsiPoly.size() >= 2) { p.setPen(QPen(prevCol, 1.2)); p.drawPolyline(rsiPoly); }
                rsiPoly.clear();
                rsiPoly.append(pt);   // include junction point for continuity
            }
            rsiPoly.append(pt);
            prevCol = col;
        }
        if (rsiPoly.size() >= 2) { p.setPen(QPen(prevCol, 1.2)); p.drawPolyline(rsiPoly); }
        p.setRenderHint(QPainter::Antialiasing, false);

        // Overbought / oversold zone fill
        if (!rsiPoly.isEmpty()) {
            // (kept light — just the line colouring conveys the zone)
        }

        // Level labels (left side)
        {
            QFont lf; lf.setFamily("Consolas"); lf.setPointSize(8);
            p.setFont(lf);
            p.setPen(QColor(0x44, 0x60, 0x7a, 160));
            p.drawText(QRect(2, y70 - 9, 22, 10), Qt::AlignLeft, "70");
            p.drawText(QRect(2, y30 + 1, 22, 10), Qt::AlignLeft, "30");
            p.setPen(QColor(0x28, 0x3a, 0x52));
            p.drawText(QRect(26, rsiY + 2, 32, 11), Qt::AlignLeft | Qt::AlignTop, "RSI");
        }

        // Current RSI value on right axis
        if (endIdx >= rsiPeriod && endIdx < numActual) {
            const double rv = rsiVals[endIdx];
            const QColor rc = rv > 70 ? QColor(0xe0, 0x50, 0x50) :
                              rv < 30 ? QColor(0x00, 0xcc, 0x70) :
                                        QColor(0x80, 0xaa, 0xe0);
            QFont vf; vf.setFamily("Consolas"); vf.setPointSize(9); vf.setBold(true);
            p.setFont(vf);
            p.setPen(rc);
            p.drawText(QRect(chartW + 2, rsiY, priceW - 4, rsiH),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(rv, 'f', 1));
        }
    }

    // ── EMA overlays (9 / 21 / 50) ───────────────────────────────────────────
    {
        static const struct { int period; unsigned rgb; int toggle; } kEmas[] = {
            {9,  0x4090e0, 0},   // blue  — legend[0]
            {21, 0xe07830, 1},   // orange — legend[1]
            {50, 0xa050e0, 2},   // purple — legend[2]
        };
        p.setRenderHint(QPainter::Antialiasing, true);

        std::vector<double> closes(numActual);
        for (int i = 0; i < numActual; ++i) closes[i] = all[i]->close;

        for (const auto &em : kEmas) {
            if (!m_legendEnabled[em.toggle]) continue;
            if (numActual < 2) continue;
            const auto ema = computeEma(closes, em.period);

            QPolygonF poly;
            for (int i = startIdx; i <= endIdx && i < numActual; ++i) {
                if (i < em.period - 1) continue;
                if (ema[i] < minP || ema[i] > maxP) continue;
                poly.append(QPointF(xOfIdx(i), yOfPrice(ema[i])));
            }
            if (poly.size() >= 2) {
                QColor c((em.rgb >> 16) & 0xff, (em.rgb >> 8) & 0xff, em.rgb & 0xff, 190);
                p.setPen(QPen(c, 1.0));
                p.drawPolyline(poly);
            }
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // ── VWAP overlay ──────────────────────────────────────────────────────────
    if (m_legendEnabled[4]) {
        double cumPV = 0.0, cumV = 0.0;
        qint64 lastDay = -1;
        std::vector<double> vwapVals(numActual, 0.0);
        std::vector<bool>   vwapOk(numActual, false);

        for (int i = 0; i < numActual; ++i) {
            const Candle *c = all[i];
            qint64 day = (c->openTimeMs / 86400000LL) * 86400000LL;
            if (day != lastDay) { cumPV = 0.0; cumV = 0.0; lastDay = day; }
            double vol = c->volume > 0.0 ? c->volume : (c->buyVol + c->sellVol);
            if (vol > 0.0) {
                double typical = (c->high + c->low + c->close) / 3.0;
                cumPV += typical * vol;
                cumV  += vol;
            }
            if (cumV > 0.0) { vwapVals[i] = cumPV / cumV; vwapOk[i] = true; }
        }

        QPolygonF vwapPoly;
        for (int i = startIdx; i <= endIdx && i < numActual; ++i) {
            if (!vwapOk[i]) continue;
            int cy = yOfPrice(vwapVals[i]);
            if (cy < headerH || cy > headerH + mainH) continue;
            vwapPoly.append(QPointF(xOfIdx(i), cy));
        }
        if (vwapPoly.size() >= 2) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(0xff, 0xd0, 0x30, 210), 1.5));
            p.drawPolyline(vwapPoly);
            p.setRenderHint(QPainter::Antialiasing, false);

            // VWAP label on right axis
            int lastVwapIdx = -1;
            for (int i = endIdx; i >= startIdx; --i)
                if (i < numActual && vwapOk[i]) { lastVwapIdx = i; break; }
            if (lastVwapIdx >= 0) {
                int vy = yOfPrice(vwapVals[lastVwapIdx]);
                if (vy >= headerH && vy <= headerH + mainH) {
                    p.fillRect(chartW + 1, vy - 9, priceW - 2, 18, QColor(0x30, 0x22, 0x04));
                    p.setPen(QColor(0xff, 0xd0, 0x30));
                    QFont lf; lf.setFamily("Consolas"); lf.setPointSize(9); lf.setBold(true);
                    p.setFont(lf);
                    p.drawText(QRect(chartW + 3, vy - 9, priceW - 5, 18),
                               Qt::AlignCenter,
                               QString::number(vwapVals[lastVwapIdx], 'f', priceDec));
                }
            }
        }
    }

    // ── Alert lines ───────────────────────────────────────────────────────────
    if (!m_alerts.empty()) {
        QFont af; af.setFamily("Consolas"); af.setPointSize(7); af.setBold(true);
        p.setFont(af);
        for (double ap : m_alerts) {
            if (ap < minP || ap > maxP) continue;
            int ay = yOfPrice(ap);
            p.setPen(QPen(QColor(0xff, 0x80, 0x00, 200), 1, Qt::DashLine));
            p.drawLine(0, ay, chartW, ay);
            // Small label on left edge
            p.fillRect(1, ay - 8, 58, 16, QColor(0x20, 0x10, 0x00, 200));
            p.setPen(QColor(0xff, 0xa0, 0x30));
            p.drawText(QRect(3, ay - 8, 56, 16), Qt::AlignLeft | Qt::AlignVCenter,
                       QString("\xe2\x96\xb6 %1").arg(ap, 0, 'f', priceDec));
        }
    }

    // ── Right price axis ──────────────────────────────────────────────────────
    p.fillRect(chartW, 0, priceW, h, QColor(0x05, 0x08, 0x10, 245));
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(chartW, headerH, chartW, h - timeH);

    // Pre-compute Y positions of "priority" labels so regular labels
    // can be suppressed when they would overlap (within 16 px).
    std::vector<int> reservedAxisY;

    // Live-close label — highest priority, always draw first
    if (m_panOffset == 0 && !all.empty()) {
        double lastClose = all.back()->close;
        int    gy        = yOfPrice(lastClose);
        if (gy >= headerH + 2 && gy <= headerH + mainH - 2) {
            bool bull = all.back()->close >= all.back()->open;
            p.fillRect(chartW + 1, gy - 9, priceW - 2, 18,
                       bull ? QColor(0x04, 0x22, 0x0e) : QColor(0x20, 0x06, 0x06));
            p.setPen(bull ? QColor(0x00, 0xcc, 0x70) : QColor(0xe0, 0x40, 0x50));
            QFont lf; lf.setFamily("Consolas"); lf.setPointSize(9); lf.setBold(true);
            p.setFont(lf);
            p.drawText(QRect(chartW + 3, gy - 9, priceW - 5, 18),
                       Qt::AlignCenter, QString::number(lastClose, 'f', priceDec));
            reservedAxisY.push_back(gy);
        }
    }

    // VWAP axis label (drawn in the VWAP section above; record its Y here)
    // We re-derive it to avoid carrying state across sections.
    if (m_legendEnabled[4]) {
        // Quick scan for the last visible candle with a VWAP value
        double cumPV2 = 0, cumV2 = 0; qint64 lastDay2 = -1;
        double vwapLast = -1.0;
        for (int i = 0; i < numActual; ++i) {
            qint64 day = (all[i]->openTimeMs / 86400000LL) * 86400000LL;
            if (day != lastDay2) { cumPV2 = 0; cumV2 = 0; lastDay2 = day; }
            double vol = all[i]->volume > 0 ? all[i]->volume : (all[i]->buyVol + all[i]->sellVol);
            if (vol > 0) { cumPV2 += (all[i]->high+all[i]->low+all[i]->close)/3.0*vol; cumV2 += vol; }
            if (cumV2 > 0 && i >= startIdx && i <= endIdx) { vwapLast = cumPV2 / cumV2; }
        }
        if (vwapLast > 0) {
            int vy = yOfPrice(vwapLast);
            if (vy >= headerH && vy <= headerH + mainH)
                reservedAxisY.push_back(vy);
        }
    }

    // Regular grid labels — skip any that land within 16 px of a reserved slot
    {
        QFont rf; rf.setFamily("Consolas"); rf.setPointSize(9);
        p.setFont(rf);
        for (int step = 0; step <= 5; ++step) {
            double price = minP + priceRange * step / 5.0;
            int    gy    = yOfPrice(price);
            if (gy < headerH + 8 || gy > headerH + mainH - 8) continue;
            bool blocked = false;
            for (int ry : reservedAxisY)
                if (std::abs(gy - ry) < 16) { blocked = true; break; }
            if (blocked) continue;
            p.setPen(QColor(0x40, 0x58, 0x6e, 190));
            p.drawText(QRect(chartW + 4, gy - 7, priceW - 6, 14),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(price, 'f', priceDec));
            p.setPen(QColor(0x18, 0x24, 0x30));
            p.drawLine(chartW, gy, chartW + 4, gy);
        }
    }

    // ── Indicator legend (top-left, clickable toggles) ───────────────────────
    {
        struct LegendEntry { int minN; unsigned rgb; const char *tag; int idx; };
        static const LegendEntry kLegend[] = {
            {9,  0x4090e0, "EMA9",     0},
            {21, 0xe07830, "EMA21",    1},
            {50, 0xa050e0, "EMA50",    2},
            {20, 0x4480cc, "BB(20,2)", 3},
            {1,  0xffd030, "VWAP",     4},
        };
        QFont lf; lf.setFamily("Consolas"); lf.setPointSize(9);
        p.setFont(lf);
        QFontMetrics fm(lf);

        int totalW = 6, visCount = 0;
        for (const auto &e : kLegend) {
            if (numActual < e.minN) continue;
            totalW += 7 + fm.horizontalAdvance(e.tag) + 6 + 4;
            ++visCount;
        }

        m_legendCount = 0;
        if (visCount > 0) {
            const int ly = headerH + 3;
            p.fillRect(3, ly, totalW, 16, QColor(0x05, 0x08, 0x12, 230));
            p.setPen(QColor(0x14, 0x20, 0x30));
            p.drawRect(3, ly, totalW, 14);

            int lx = 7;
            for (const auto &e : kLegend) {
                if (numActual < e.minN) continue;
                const bool    on    = m_legendEnabled[e.idx];
                const int     alpha = on ? 230 : 70;
                const QColor  c((e.rgb>>16)&0xff, (e.rgb>>8)&0xff, e.rgb&0xff, alpha);
                const int     tw    = fm.horizontalAdvance(e.tag) + 6;
                const int     itemW = 7 + tw + 4;

                // Colour dot
                p.setPen(Qt::NoPen); p.setBrush(c);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.drawEllipse(lx, ly + 4, 5, 5);
                p.setRenderHint(QPainter::Antialiasing, false);
                p.setBrush(Qt::NoBrush);

                // Label
                p.setPen(c);
                p.drawText(QRect(lx + 7, ly + 1, tw, 12),
                           Qt::AlignLeft | Qt::AlignVCenter, e.tag);

                // Strikethrough when disabled
                if (!on) {
                    p.setPen(QColor((e.rgb>>16)&0xff, (e.rgb>>8)&0xff, e.rgb&0xff, 120));
                    p.drawLine(lx, ly + 7, lx + itemW - 4, ly + 7);
                }

                // Store hit-test rect for mousePressEvent
                m_legendRects[m_legendCount++] = QRect(lx - 2, ly, itemW, 14);
                lx += itemW;
            }
        }
    }

    // Determine if crosshair is active (covers price chart + sub-panes above time axis)
    const bool crosshairActive = m_mouseIn && !m_isDragging &&
                                 m_mousePos.x() < chartW &&
                                 m_mousePos.y() > headerH && m_mousePos.y() < h - timeH;
    const int hoveredIdx = crosshairActive
        ? (endIdx - (chartW - m_mousePos.x()) / std::max(1, candleW))
        : -1;

    // ── Header centre: always-live price + "LIVE" tag ────────────────────────
    if (!all.empty()) {
        const Candle *live  = all.back();
        const bool   lbull  = live->close >= live->open;
        const QColor priceCol = lbull ? QColor(0x00, 0xcc, 0x70) : QColor(0xe0, 0x50, 0x50);
        const QString priceStr = QString::number(live->close, 'f', priceDec);

        // Measure price string and centre it in the gap between the interval
        // buttons (~x=128) and the CANDLES label (chartW-72), never at w/2
        // which overlaps when the widget is narrow.
        QFont pf; pf.setFamily("Consolas"); pf.setPointSize(12); pf.setBold(true);
        p.setFont(pf);
        const int priceW2   = QFontMetrics(pf).horizontalAdvance(priceStr);
        const int gapLeft   = 130;            // clears the rightmost interval button
        const int gapRight  = chartW - 76;    // stops before CANDLES label
        const int priceX    = (gapLeft + gapRight) / 2 - priceW2 / 2;

        // Live price — always the most recent candle's close
        p.setPen(priceCol);
        p.drawText(QRect(priceX, 0, priceW2 + 2, headerH),
                   Qt::AlignLeft | Qt::AlignVCenter, priceStr);

        // "LIVE" tag to the left, vertically centred, subtle green
        QFont tf; tf.setFamily("Segoe UI"); tf.setPointSize(9);
        tf.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        p.setFont(tf);
        p.setPen(QColor(0x18, 0x60, 0x38));
        p.drawText(QRect(priceX - 40, 0, 36, headerH),
                   Qt::AlignRight | Qt::AlignVCenter, "LIVE");
    }

    {
        QFont sf; sf.setFamily("Consolas"); sf.setPointSize(7);
        p.setFont(sf);
        if (m_panOffset > 0) {
            p.setPen(QColor(0xd0, 0xa0, 0x30));
            p.drawText(QRect(chartW - 130, 0, 110, headerH), Qt::AlignRight | Qt::AlignVCenter,
                       QString("\xe2\x97\x84 -%1").arg(m_panOffset));
        }
        p.setPen(QColor(0x48, 0x62, 0x80));
        QFont tf; tf.setFamily("Segoe UI"); tf.setPointSize(10);
        tf.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        p.setFont(tf);
        p.drawText(QRect(chartW - 72, 0, 68, headerH), Qt::AlignRight | Qt::AlignVCenter, "CANDLES");
    }

    // ── Bottom time axis ──────────────────────────────────────────────────────
    const int timeY = h - timeH;
    p.fillRect(0, timeY, w, timeH, QColor(0x05, 0x08, 0x10, 220));
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(0, timeY, w, timeY);
    {
        QFont tf; tf.setFamily("Consolas"); tf.setPointSize(9);
        p.setFont(tf);
        p.setPen(QColor(0x38, 0x50, 0x6a));
        const int labelEvery = std::max(1, 80 / std::max(1, candleW));
        for (int i = startIdx; i <= endIdx && i < numActual; i += labelEvery) {
            int tx = xOfIdx(i);
            if (tx < 4 || tx > chartW - 30) continue;
            p.drawText(QRect(tx - 20, timeY + 2, 40, timeH - 4),
                       Qt::AlignCenter, formatTime(all[i]->openTimeMs));
        }
    }

    // ── Crosshair ─────────────────────────────────────────────────────────────
    if (m_mouseIn && !m_isDragging &&
        m_mousePos.x() < chartW &&
        m_mousePos.y() > headerH && m_mousePos.y() < h - timeH)
    {
        const int mx = m_mousePos.x();
        const int my = m_mousePos.y();

        p.setPen(QPen(QColor(0x50, 0x70, 0x90, 140), 1, Qt::DashLine));
        p.drawLine(0, my, chartW, my);
        p.drawLine(mx, headerH, mx, h - timeH);

        // Price label
        double hoverPrice = maxP - static_cast<double>(my - headerH) / mainH * priceRange;
        p.fillRect(chartW + 1, my - 9, priceW - 2, 18, QColor(0x18, 0x28, 0x40));
        p.setPen(QColor(0xd0, 0xe0, 0xf0));
        QFont lf; lf.setFamily("Consolas"); lf.setPointSize(8);
        p.setFont(lf);
        p.drawText(QRect(chartW + 3, my - 9, priceW - 5, 18),
                   Qt::AlignCenter, QString::number(hoverPrice, 'f', priceDec));

        // Time label
        const int tIdx = endIdx - (chartW - mx) / std::max(1, candleW);
        if (tIdx >= 0 && tIdx < numActual) {
            QFont tf; tf.setFamily("Consolas"); tf.setPointSize(7);
            p.setFont(tf);
            p.fillRect(mx - 24, timeY + 1, 48, timeH - 2, QColor(0x18, 0x28, 0x40));
            p.setPen(QColor(0xd0, 0xe0, 0xf0));
            p.drawText(QRect(mx - 24, timeY + 1, 48, timeH - 2),
                       Qt::AlignCenter, formatTime(all[tIdx]->openTimeMs));
        }

        // ── OHLC tooltip box ─────────────────────────────────────────────────
        if (hoveredIdx >= 0 && hoveredIdx < numActual) {
            const Candle *hc   = all[hoveredIdx];
            const bool   hbull = hc->close >= hc->open;
            const bool   hasSplit = (hc->buyVol + hc->sellVol > 1e-9);
            const double vol   = hasSplit ? (hc->buyVol + hc->sellVol) : hc->volume;
            const bool   hasRsi = (hoveredIdx >= rsiPeriod);

            const int lineH = 13;
            const int bw    = 128;
            const int bh    = 5 + lineH + 2           // timestamp
                              + lineH * 4              // O H L C
                              + (vol > 0  ? lineH : 0) // volume
                              + (hasRsi   ? lineH : 0) // RSI
                              + 5;

            // Follow cursor: right side by default, flip left near right edge
            // (same pattern as DepthChartWidget)
            int bx = mx + 8;
            if (bx + bw > chartW - 4) bx = mx - bw - 8;
            int by = qBound(headerH + 4, my - bh / 2, headerH + mainH - bh - 4);

            p.fillRect(bx, by, bw, bh, QColor(0x06, 0x0c, 0x18, 220));
            p.setPen(QColor(0x1e, 0x2e, 0x44));
            p.drawRect(bx, by, bw - 1, bh - 1);

            QFont tf; tf.setFamily("Consolas"); tf.setPointSize(7);
            p.setFont(tf);

            const int lx = bx + 5;
            const QColor neutral(0xc0, 0xcc, 0xd8);
            const QColor bullCol(0x00, 0xcc, 0x70);
            const QColor bearCol(0xe0, 0x50, 0x50);
            int iy = by + 5;

            // Timestamp
            p.setPen(QColor(0x60, 0x80, 0xa0));
            p.drawText(QRect(lx, iy, bw - 10, lineH), Qt::AlignLeft | Qt::AlignVCenter,
                       QDateTime::fromMSecsSinceEpoch(hc->openTimeMs)
                           .toString("yyyy-MM-dd HH:mm"));
            iy += lineH + 2;

            // Row helper
            auto row = [&](const char *label, double val, const QColor &valCol) {
                p.setPen(QColor(0x40, 0x58, 0x72));
                p.drawText(QRect(lx, iy, 18, lineH), Qt::AlignLeft | Qt::AlignVCenter, label);
                p.setPen(valCol);
                p.drawText(QRect(lx + 18, iy, bw - 28, lineH),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(val, 'f', priceDec));
                iy += lineH;
            };

            row("O", hc->open,  neutral);
            row("H", hc->high,  QColor(0x40, 0xdd, 0x80));
            row("L", hc->low,   QColor(0xdd, 0x50, 0x50));

            // C + inline change %
            {
                const double chgPct = (hc->open > 1e-9)
                    ? (hc->close - hc->open) / hc->open * 100.0 : 0.0;
                p.setPen(QColor(0x40, 0x58, 0x72));
                p.drawText(QRect(lx, iy, 18, lineH), Qt::AlignLeft | Qt::AlignVCenter, "C");
                p.setPen(hbull ? bullCol : bearCol);
                p.drawText(QRect(lx + 18, iy, 52, lineH),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(hc->close, 'f', priceDec));
                p.setPen(chgPct >= 0 ? bullCol : bearCol);
                p.drawText(QRect(lx + 72, iy, bw - 82, lineH),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString("%1%2%").arg(chgPct >= 0 ? "+" : "")
                                          .arg(chgPct, 0, 'f', 2));
                iy += lineH;
            }

            // Volume — buy/sell split if available
            if (vol > 1e-9) {
                auto fmtV = [](double v) -> QString {
                    if (v >= 1e6) return QString::number(v / 1e6, 'f', 2) + "M";
                    if (v >= 1e3) return QString::number(v / 1e3, 'f', 2) + "K";
                    return QString::number(v, 'f', 3);
                };
                if (hasSplit) {
                    p.setPen(QColor(0x00, 0xcc, 0x70));
                    p.drawText(QRect(lx, iy, bw / 2 - 2, lineH),
                               Qt::AlignLeft | Qt::AlignVCenter,
                               "\xe2\x86\x91 " + fmtV(hc->buyVol));
                    p.setPen(QColor(0xe0, 0x50, 0x50));
                    p.drawText(QRect(lx + bw / 2 - 6, iy, bw / 2, lineH),
                               Qt::AlignLeft | Qt::AlignVCenter,
                               "\xe2\x86\x93 " + fmtV(hc->sellVol));
                } else {
                    p.setPen(QColor(0x40, 0x58, 0x72));
                    p.drawText(QRect(lx, iy, 24, lineH), Qt::AlignLeft | Qt::AlignVCenter, "VOL");
                    p.setPen(QColor(0x70, 0x90, 0xb0));
                    p.drawText(QRect(lx + 24, iy, bw - 34, lineH),
                               Qt::AlignRight | Qt::AlignVCenter, fmtV(vol));
                }
                iy += lineH;
            }

            // RSI
            if (hasRsi) {
                const double rv = rsiVals[hoveredIdx];
                const QColor rc = rv > 70 ? QColor(0xe0, 0x50, 0x50) :
                                  rv < 30 ? QColor(0x00, 0xcc, 0x70) :
                                            QColor(0x80, 0xaa, 0xe0);
                p.setPen(QColor(0x40, 0x58, 0x72));
                p.drawText(QRect(lx, iy, 28, lineH), Qt::AlignLeft | Qt::AlignVCenter, "RSI");
                p.setPen(rc);
                p.drawText(QRect(lx + 28, iy, bw - 38, lineH),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(rv, 'f', 1));
            }
        }
    }
}

// ── Skeleton (no data) ────────────────────────────────────────────────────────
void CandlestickWidget::paintSkeleton(QPainter &p)
{
    const int w = width(), h = height();
    const int headerH = 26, priceW = 66, timeH = 18;
    const int volH  = std::max(16, h / 9);
    const int chartW = w - priceW;
    const int chartH = h - headerH - timeH - volH;

    // Faint horizontal grid lines
    p.setPen(QColor(0x0f, 0x19, 0x2a));
    for (int i = 1; i < 4; ++i)
        p.drawLine(0, headerH + chartH * i / 4, chartW, headerH + chartH * i / 4);

    // Deterministic skeleton candles {body-height%, body-top% of chartH}
    static const struct { int bh, by; } kC[] = {
        {22,58},{18,62},{28,48},{20,55},{35,32},{25,65},{30,42},
        {16,70},{32,38},{22,56},{28,50},{18,64},{38,28},{24,54},
        {20,60},{30,44},{26,52},{16,68},{34,32},{22,58}
    };
    const int slotW = std::max(9, chartW / 22);
    const int bodyW = std::max(3, slotW - 3);
    const int nSk   = std::min(20, chartW / slotW);

    for (int i = 0; i < nSk; ++i) {
        const int bh   = kC[i].bh * chartH / 100;
        const int bTop = headerH + kC[i].by * chartH / 100;
        const int bx   = chartW - (nSk - i) * slotW + (slotW - bodyW) / 2;
        const bool up  = (i % 3 != 2);
        p.fillRect(QRect(bx, bTop, bodyW, bh),
                   up ? QColor(0x0c, 0x1e, 0x14) : QColor(0x1e, 0x0c, 0x0c));
        // Wick
        p.setPen(up ? QColor(0x12, 0x28, 0x1a) : QColor(0x28, 0x12, 0x12));
        const int mx = bx + bodyW / 2;
        p.drawLine(mx, bTop - 5, mx, bTop);
        p.drawLine(mx, bTop + bh, mx, bTop + bh + 5);
    }

    // Skeleton volume bars
    for (int i = 0; i < nSk; ++i) {
        const int bh  = std::max(2, kC[i].bh * volH / 80);
        const int bx  = chartW - (nSk - i) * slotW + (slotW - bodyW) / 2;
        const bool up = (i % 3 != 2);
        p.fillRect(QRect(bx, h - timeH - bh, bodyW, bh),
                   up ? QColor(0x0a, 0x18, 0x10) : QColor(0x18, 0x0a, 0x0a));
    }

    // Ghost price scale bars (right strip)
    for (int i = 0; i < 5; ++i)
        p.fillRect(QRect(chartW + 4, headerH + 4 + i * chartH / 5, priceW - 8, 10),
                   QColor(0x0e, 0x16, 0x22));

    // Shimmer sweep across entire body
    const double ph = ShimmerTimer::phase();
    const double cx = ph * (w + 240.0) - 120.0;
    QLinearGradient g(cx - 120, 0, cx + 120, 0);
    g.setColorAt(0.0, Qt::transparent);
    g.setColorAt(0.5, QColor(0x1e, 0x30, 0x52, 55));
    g.setColorAt(1.0, Qt::transparent);
    p.fillRect(QRect(0, headerH, w, h - headerH), g);
}
