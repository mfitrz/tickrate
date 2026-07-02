#include "HeatmapWidget.h"
#include "util/ShimmerTimer.h"
#include <QPainter>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QDateTime>
#include <algorithm>
#include <cmath>
#include <limits>

HeatmapWidget::HeatmapWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);   // accept keyboard after a click
    setStyleSheet("background-color: #080c12;");

    connect(&ShimmerTimer::instance(), &ShimmerTimer::tick,
            this, [this] { if (m_skelActive) update(); });
}

void HeatmapWidget::pushSnapshot(const BookSnapshot &snap, qint64 timestampMs)
{
    m_skelActive = false;
    m_history.push_back(snap);
    m_timestamps.push_back(timestampMs);
    if (static_cast<int>(m_history.size()) > kMaxHistory) {
        m_history.pop_front();
        m_timestamps.pop_front();
    }

    m_maxQty = 1.0;
    for (const auto &s : m_history) {
        for (const auto &l : s.bids) m_maxQty = std::max(m_maxQty, l.quantity);
        for (const auto &l : s.asks) m_maxQty = std::max(m_maxQty, l.quantity);
    }

    m_tickSize = detectTickSize(snap);
    update();
}

void HeatmapWidget::wheelEvent(QWheelEvent *e)
{
    const float factor = (e->angleDelta().y() > 0) ? 1.3f : (1.0f / 1.3f);
    m_zoom = qBound(0.25f, m_zoom * factor, 4.0f);
    update();
    e->accept();
}

void HeatmapWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_mousePos = e->pos();
    m_mouseIn  = true;
    update();
}

void HeatmapWidget::leaveEvent(QEvent *)
{
    m_mouseIn = false;
    update();
}

void HeatmapWidget::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        m_zoom = qBound(0.25f, m_zoom * 1.3f, 4.0f);
        update();
        break;
    case Qt::Key_Minus:
        m_zoom = qBound(0.25f, m_zoom / 1.3f, 4.0f);
        update();
        break;
    case Qt::Key_0:
        m_zoom = 1.0f;
        update();
        break;
    default:
        QWidget::keyPressEvent(e);
    }
}

double HeatmapWidget::detectTickSize(const BookSnapshot &snap) const
{
    double minDiff = std::numeric_limits<double>::max();
    for (size_t i = 1; i < snap.bids.size() && i < 20; ++i) {
        double d = snap.bids[i-1].price - snap.bids[i].price;
        if (d > 1e-9) minDiff = std::min(minDiff, d);
    }
    for (size_t i = 1; i < snap.asks.size() && i < 20; ++i) {
        double d = snap.asks[i].price - snap.asks[i-1].price;
        if (d > 1e-9) minDiff = std::min(minDiff, d);
    }
    if (minDiff == std::numeric_limits<double>::max()) return 1.0;
    if (minDiff < 0.00015) return 0.0001;
    if (minDiff < 0.0015)  return 0.001;
    if (minDiff < 0.015)   return 0.01;
    if (minDiff < 0.15)    return 0.1;
    return 1.0;
}

QColor HeatmapWidget::levelColor(double qty, bool isBid) const
{
    double raw       = std::min(qty / m_maxQty, 1.0);
    double intensity = std::pow(raw, 0.40);
    int    alpha     = static_cast<int>(18 + intensity * 232);
    return isBid
        ? QColor(0x00, 0xcc, 0x68, alpha)
        : QColor(0xe0, 0x40, 0x50, alpha);
}

void HeatmapWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();
    p.fillRect(rect(), QColor(0x08, 0x0c, 0x12));

    if (m_history.empty()) { paintSkeleton(p); return; }

    const BookSnapshot &latest = m_history.back();
    if (latest.bids.empty() || latest.asks.empty()) { paintSkeleton(p); return; }

    // ── Layout ────────────────────────────────────────────────────────────────
    const int yAxisW  = 60;
    const int priceW  = 66;
    const int headerH = 22;
    const int footerH = 24;
    const int chartW  = w - yAxisW - priceW;
    const int chartH  = h - headerH - footerH;
    const int midY    = headerH + chartH / 2;

    const int visDepth  = std::max(5, static_cast<int>(kDepth / m_zoom));
    const int rowHeight = std::max(1, chartH / (visDepth * 2 + 1));

    int priceDec = 0;
    if      (m_tickSize < 0.00015) priceDec = 4;
    else if (m_tickSize < 0.0015)  priceDec = 3;
    else if (m_tickSize < 0.015)   priceDec = 2;
    else if (m_tickSize < 0.15)    priceDec = 1;

    int labelStep;
    if      (rowHeight >= 14) labelStep = 1;
    else if (rowHeight >= 8)  labelStep = 2;
    else if (rowHeight >= 5)  labelStep = 5;
    else if (rowHeight >= 3)  labelStep = 10;
    else                      labelStep = 20;

    const double tickSize = m_tickSize;
    const int    cols     = static_cast<int>(m_history.size());
    const int    offset   = kMaxHistory - cols;

    // ── Heat cells ────────────────────────────────────────────────────────────
    for (int col = 0; col < cols; ++col) {
        const BookSnapshot &snap    = m_history[col];
        const double        snapMid = snap.midPrice;

        int x1 = yAxisW + static_cast<int>(static_cast<double>(col + offset)     * chartW / kMaxHistory);
        int x2 = yAxisW + static_cast<int>(static_cast<double>(col + offset + 1) * chartW / kMaxHistory);
        int cw = std::max(1, x2 - x1);

        for (const auto &bid : snap.bids) {
            int row = static_cast<int>((snapMid - bid.price) / tickSize);
            if (row < 0 || row >= visDepth) continue;
            p.fillRect(x1, midY + row * rowHeight, cw, rowHeight,
                       levelColor(bid.quantity, true));
        }
        for (const auto &ask : snap.asks) {
            int row = static_cast<int>((ask.price - snapMid) / tickSize);
            if (row < 0 || row >= visDepth) continue;
            p.fillRect(x1, midY - (row + 1) * rowHeight, cw, rowHeight,
                       levelColor(ask.quantity, false));
        }
    }

    // ── Grid lines ────────────────────────────────────────────────────────────
    p.setPen(QPen(QColor(0x18, 0x24, 0x38, 75), 1));
    for (int row = labelStep; row <= visDepth; row += labelStep) {
        int ay = midY - row * rowHeight;
        int by = midY + row * rowHeight;
        if (ay >= headerH)          p.drawLine(yAxisW, ay, yAxisW + chartW, ay);
        if (by <= headerH + chartH) p.drawLine(yAxisW, by, yAxisW + chartW, by);
    }

    // ── Mid-price line ────────────────────────────────────────────────────────
    p.setPen(QPen(QColor(0xe8, 0xc8, 0x38, 200), 1));
    p.drawLine(yAxisW, midY, yAxisW + chartW, midY);

    // ── LEFT Y-axis panel ─────────────────────────────────────────────────────
    p.fillRect(0, 0, yAxisW, h, QColor(0x05, 0x08, 0x10, 245));
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(yAxisW - 1, headerH, yAxisW - 1, headerH + chartH);

    {
        QFont yf; yf.setFamily("Consolas"); yf.setPointSize(9);
        p.setFont(yf);

        for (int row = labelStep; row <= visDepth; row += labelStep) {
            int ay = midY - row * rowHeight;
            if (ay >= headerH + 6 && ay <= headerH + chartH - 6) {
                p.setPen(QColor(0x88, 0x42, 0x42, 190));
                p.drawText(QRect(2, ay - 7, yAxisW - 6, 14),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(latest.midPrice + row * tickSize, 'f', priceDec));
                p.setPen(QColor(0x42, 0x5e, 0x7a, 210));
                p.drawLine(yAxisW - 5, ay, yAxisW - 1, ay);
            }
            int by = midY + row * rowHeight;
            if (by >= headerH + 6 && by <= headerH + chartH - 6) {
                p.setPen(QColor(0x24, 0x78, 0x48, 190));
                p.drawText(QRect(2, by - 7, yAxisW - 6, 14),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(latest.midPrice - row * tickSize, 'f', priceDec));
                p.setPen(QColor(0x42, 0x5e, 0x7a, 210));
                p.drawLine(yAxisW - 5, by, yAxisW - 1, by);
            }
        }
    }

    // ── RIGHT price-axis panel ────────────────────────────────────────────────
    const int labelX = yAxisW + chartW;
    p.fillRect(labelX, 0, priceW, h, QColor(0x05, 0x08, 0x10, 245));
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(labelX, headerH, labelX, headerH + chartH);

    {
        QFont rf; rf.setFamily("Consolas"); rf.setPointSize(9);
        p.setFont(rf);

        const double bestAsk = latest.asks.empty() ? -1.0 : latest.asks.front().price;
        const double bestBid = latest.bids.empty() ? -1.0 : latest.bids.front().price;

        for (int row = labelStep; row <= visDepth; row += labelStep) {
            double askPx = latest.midPrice + row * tickSize;
            int ay = midY - row * rowHeight;
            if (ay >= headerH + 6 && ay <= headerH + chartH - 6) {
                bool highlight = (bestAsk > 0 && std::abs(askPx - bestAsk) < tickSize * 0.5);
                p.setPen(highlight ? QColor(0xf4, 0x62, 0x62) : QColor(0x88, 0x42, 0x42, 190));
                p.drawText(QRect(labelX + 5, ay - 7, priceW - 7, 14),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QString::number(askPx, 'f', priceDec));
                p.setPen(QColor(0x42, 0x5e, 0x7a, 210));
                p.drawLine(labelX, ay, labelX + 4, ay);
            }
            double bidPx = latest.midPrice - row * tickSize;
            int by = midY + row * rowHeight;
            if (by >= headerH + 6 && by <= headerH + chartH - 6) {
                bool highlight = (bestBid > 0 && std::abs(bidPx - bestBid) < tickSize * 0.5);
                p.setPen(highlight ? QColor(0x3a, 0xe8, 0x85) : QColor(0x24, 0x78, 0x48, 190));
                p.drawText(QRect(labelX + 5, by - 7, priceW - 7, 14),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QString::number(bidPx, 'f', priceDec));
                p.setPen(QColor(0x42, 0x5e, 0x7a, 210));
                p.drawLine(labelX, by, labelX + 4, by);
            }
        }

        // Mid price box on top
        p.fillRect(labelX + 2, midY - 10, priceW - 4, 20, QColor(0x28, 0x22, 0x04));
        p.setPen(QPen(QColor(0xe8, 0xc8, 0x38, 160), 1));
        p.drawRect(labelX + 2, midY - 10, priceW - 4, 20);
        QFont mf; mf.setFamily("Consolas"); mf.setPointSize(10); mf.setBold(true);
        p.setFont(mf);
        p.setPen(QColor(0xf2, 0xdc, 0x5a));
        p.drawText(QRect(labelX + 3, midY - 10, priceW - 6, 20),
                   Qt::AlignCenter,
                   QString::number(latest.midPrice, 'f', priceDec));
    }

    // ── HEADER strip ─────────────────────────────────────────────────────────
    p.fillRect(0, 0, w, headerH, QColor(0x05, 0x07, 0x0e, 220));
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(0, headerH - 1, w, headerH - 1);
    {
        QFont lf; lf.setFamily("Segoe UI"); lf.setPointSize(10);
        lf.setWeight(QFont::Medium);
        p.setFont(lf);
        // "ASKS ↑" lives in the y-axis gutter so it never collides with chart content
        p.setPen(QColor(0xb8, 0x38, 0x38, 220));
        p.drawText(QRect(2, 0, yAxisW - 4, headerH),
                   Qt::AlignRight | Qt::AlignVCenter, "ASKS \xe2\x86\x91");

        // Widget title centered in chart area
        QFont tf; tf.setFamily("Segoe UI"); tf.setPointSize(10);
        tf.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        p.setFont(tf);
        p.setPen(QColor(0x48, 0x64, 0x80));
        p.drawText(QRect(yAxisW, 0, chartW, headerH), Qt::AlignCenter, "HEATMAP");

        if (m_zoom != 1.0f) {
            QFont zf; zf.setFamily("Consolas"); zf.setPointSize(9);
            p.setFont(zf);
            p.setPen(QColor(0x40, 0x58, 0x74, 200));
            p.drawText(QRect(yAxisW + 4, 0, 80, headerH),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString("\xc3\x97%1").arg(m_zoom, 0, 'f', 2));
        }
    }

    // ── FOOTER strip ─────────────────────────────────────────────────────────
    const int footerY = headerH + chartH;
    p.fillRect(0, footerY, w, footerH, QColor(0x05, 0x07, 0x0e, 220));
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(0, footerY, w, footerY);
    {
        // "BIDS ↓" lives in the y-axis gutter (right-aligned) — no collision with timestamps
        QFont lf; lf.setFamily("Segoe UI"); lf.setPointSize(10);
        lf.setWeight(QFont::Medium);
        p.setFont(lf);
        p.setPen(QColor(0x22, 0xa0, 0x52, 220));
        p.drawText(QRect(2, footerY, yAxisW - 4, footerH),
                   Qt::AlignRight | Qt::AlignVCenter, "BIDS \xe2\x86\x93");

        // Timestamps span the full chart area — no BIDS label to collide with now
        if (!m_timestamps.empty()) {
            QFont tf; tf.setFamily("Consolas"); tf.setPointSize(9);
            p.setFont(tf);
            p.setPen(QColor(0x48, 0x64, 0x82));

            QString oldest = QDateTime::fromMSecsSinceEpoch(m_timestamps.front())
                                 .toString("HH:mm:ss");
            QString newest = QDateTime::fromMSecsSinceEpoch(m_timestamps.back())
                                 .toString("HH:mm:ss");
            p.drawText(QRect(yAxisW + 4, footerY, chartW / 2, footerH),
                       Qt::AlignLeft | Qt::AlignVCenter, oldest);
            p.drawText(QRect(yAxisW + chartW / 2, footerY, chartW / 2 - 4, footerH),
                       Qt::AlignRight | Qt::AlignVCenter, newest);
        }
    }

    // ── Crosshair ─────────────────────────────────────────────────────────────
    if (m_mouseIn && m_mousePos.x() >= yAxisW && m_mousePos.x() < labelX &&
        m_mousePos.y() > headerH && m_mousePos.y() < footerY)
    {
        const int mx = m_mousePos.x();
        const int my = m_mousePos.y();

        p.setPen(QPen(QColor(0x60, 0x88, 0xaa, 140), 1, Qt::DashLine));
        p.drawLine(yAxisW, my, labelX, my);
        p.drawLine(mx, headerH, mx, footerY);

        // Price at crosshair
        const double rowsFromMid = static_cast<double>(my - midY) / std::max(1, rowHeight);
        const double hoverPrice  = latest.midPrice - rowsFromMid * tickSize;

        // Price label on right axis
        p.fillRect(labelX + 2, my - 9, priceW - 4, 18, QColor(0x14, 0x20, 0x30));
        p.setPen(QColor(0xd0, 0xe8, 0xff));
        QFont lf; lf.setFamily("Consolas"); lf.setPointSize(10);
        p.setFont(lf);
        p.drawText(QRect(labelX + 3, my - 9, priceW - 6, 18),
                   Qt::AlignCenter,
                   QString::number(hoverPrice, 'f', priceDec));

        // ── Floating tooltip: price + book quantity at this level ─────────────
        {
            const bool isAskSide = (my < midY);
            double hoverQty = 0.0;
            bool   foundQty = false;
            const double half = tickSize * 0.6;

            const auto &levels = isAskSide ? latest.asks : latest.bids;
            for (const auto &lvl : levels) {
                if (std::abs(lvl.price - hoverPrice) < half) {
                    hoverQty = lvl.quantity; foundQty = true; break;
                }
            }

            const QColor sideCol = isAskSide
                ? QColor(0xe0, 0x50, 0x50) : QColor(0x00, 0xcc, 0x68);

            // Format quantity string
            QString qStr;
            if (foundQty) {
                if      (hoverQty >= 1e6) qStr = QString::number(hoverQty / 1e6, 'f', 2) + "M";
                else if (hoverQty >= 1e3) qStr = QString::number(hoverQty / 1e3, 'f', 2) + "K";
                else                      qStr = QString::number(hoverQty, 'f', 4);
            }

            const int boxW = 88;
            const int boxH = foundQty ? 36 : 20;
            int boxX = mx + 10;
            int boxY = my - boxH - 4;
            if (boxX + boxW > labelX - 2) boxX = mx - boxW - 10;
            if (boxY < headerH + 2)       boxY = my + 6;

            p.fillRect(boxX, boxY, boxW, boxH, QColor(0x08, 0x0e, 0x1a, 235));
            p.setPen(QPen(sideCol.darker(130), 1));
            p.drawRect(boxX, boxY, boxW - 1, boxH - 1);

            // Price row
            QFont bf; bf.setFamily("Consolas"); bf.setPointSize(8); bf.setBold(true);
            p.setFont(bf);
            p.setPen(QColor(0xd8, 0xec, 0xff));
            p.drawText(QRect(boxX + 4, boxY + 2, boxW - 8, 16),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(hoverPrice, 'f', priceDec));

            // Quantity row
            if (foundQty) {
                QFont sf; sf.setFamily("Consolas"); sf.setPointSize(7);
                p.setFont(sf);
                p.setPen(sideCol);
                p.drawText(QRect(boxX + 4, boxY + 18, boxW - 8, 14),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           (isAskSide ? "ASK  " : "BID  ") + qStr);
            }
        }

        // Time label in footer from column index
        if (!m_timestamps.empty() && chartW > 0) {
            int colIdx = static_cast<int>(
                static_cast<double>(mx - yAxisW) / chartW * cols);
            colIdx = qBound(0, colIdx, cols - 1);
            QString ts = QDateTime::fromMSecsSinceEpoch(m_timestamps[colIdx])
                             .toString("HH:mm:ss");
            p.fillRect(mx - 28, footerY + 1, 56, footerH - 2, QColor(0x14, 0x20, 0x30));
            p.setPen(QColor(0xd0, 0xe8, 0xff));
            QFont tf; tf.setFamily("Consolas"); tf.setPointSize(7);
            p.setFont(tf);
            p.drawText(QRect(mx - 28, footerY + 1, 56, footerH - 2),
                       Qt::AlignCenter, ts);
        }
    }
}

// ── Skeleton (no data) ────────────────────────────────────────────────────────
void HeatmapWidget::paintSkeleton(QPainter &p)
{
    const int w = width(), h = height();
    const int yAxisW = 60, priceW = 66, headerH = 18, footerH = 20;
    const int chartW = w - yAxisW - priceW;
    const int chartH = h - headerH - footerH;

    // Header
    p.fillRect(0, 0, w, headerH, QColor(0x05, 0x08, 0x10));
    {
        QFont hf; hf.setFamily("Segoe UI"); hf.setPointSize(7);
        hf.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
        p.setFont(hf); p.setPen(QColor(0x14, 0x22, 0x38));
        p.drawText(QRect(yAxisW, 0, chartW, headerH), Qt::AlignCenter, "LIQUIDITY HEATMAP");
    }

    // Grid of skeleton cells — alternating dim green/red intensity by row band
    // Uses a spatial hash for organic-looking variation without randomness
    static const int kI[] = {8, 32, 18, 55, 12, 45, 25, 60, 15, 38,
                              50, 20, 42, 10, 35, 48, 22, 58, 14, 40};
    const int nCols = std::min(30, chartW / 8);
    const int nRows = std::min(24, chartH / 6);
    const int cellW = nCols > 0 ? chartW / nCols : 1;
    const int cellH = nRows > 0 ? chartH / nRows : 1;
    for (int row = 0; row < nRows; ++row) {
        const bool bidSide = (row > nRows / 2);
        for (int col = 0; col < nCols; ++col) {
            const int intensity = kI[(row * 3 + col * 7) % 20];
            const int rv = bidSide ?  intensity / 10 : intensity * 22 / 100;
            const int gv = bidSide ? intensity * 22 / 100 : intensity / 10;
            const int bv = 10 + intensity / 8;
            p.fillRect(yAxisW + col * cellW, headerH + row * cellH,
                       cellW - 1, cellH - 1, QColor(rv, gv, bv));
        }
    }

    // Ghost axis label bars
    for (int i = 0; i < 5; ++i) {
        const int ry = headerH + 4 + i * chartH / 5;
        p.fillRect(2,              ry, yAxisW - 6, 10, QColor(0x0e, 0x16, 0x22));
        p.fillRect(yAxisW + chartW + 2, ry, priceW - 6, 10, QColor(0x0e, 0x16, 0x22));
    }

    // Shimmer sweep across chart area
    const double ph = ShimmerTimer::phase();
    const double cx = yAxisW + ph * (chartW + 240.0) - 120.0;
    QLinearGradient g(cx - 120, 0, cx + 120, 0);
    g.setColorAt(0.0, Qt::transparent);
    g.setColorAt(0.5, QColor(0x1e, 0x30, 0x52, 50));
    g.setColorAt(1.0, Qt::transparent);
    p.fillRect(QRect(yAxisW, headerH, chartW, chartH), g);
}
