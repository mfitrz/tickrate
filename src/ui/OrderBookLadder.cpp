#include "OrderBookLadder.h"
#include "util/ShimmerTimer.h"
#include <QPainter>
#include <QLinearGradient>
#include <QDateTime>
#include <algorithm>
#include <cmath>
#include <numeric>

OrderBookLadder::OrderBookLadder(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(200);
    setMinimumHeight(300);
    setStyleSheet("background: #080c12;");

    connect(&ShimmerTimer::instance(), &ShimmerTimer::tick,
            this, [this] { if (m_skelActive) update(); });
}

void OrderBookLadder::setSnapshot(const BookSnapshot &snap)
{
    m_snap = snap;
    if (!snap.bids.empty() || !snap.asks.empty()) m_skelActive = false;

    double frameMax = 1.0;
    for (const auto &l : snap.bids) frameMax = std::max(frameMax, l.quantity);
    for (const auto &l : snap.asks) frameMax = std::max(frameMax, l.quantity);
    m_maxQtyHistory.push_back(frameMax);
    if (static_cast<int>(m_maxQtyHistory.size()) > kMaxQtyHistory)
        m_maxQtyHistory.pop_front();
    m_stableMax = *std::max_element(m_maxQtyHistory.begin(), m_maxQtyHistory.end());

    // Prune expired flash entries
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - kFlashMs;
    for (auto it = m_recentHits.begin(); it != m_recentHits.end(); )
        it = (it->ts < cutoff) ? m_recentHits.erase(it) : ++it;

    QWidget::update();
}

void OrderBookLadder::addTrade(const TradeInfo &trade)
{
    const qint64 ts = trade.timestampMs > 0
        ? trade.timestampMs : QDateTime::currentMSecsSinceEpoch();
    m_recentHits[trade.price] = {trade.isBuy, ts};
    QWidget::update();
}

void OrderBookLadder::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();
    p.fillRect(rect(), QColor(0x08, 0x0c, 0x12));

    if (m_snap.bids.empty() && m_snap.asks.empty()) {
        paintSkeleton(p);
        return;
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    const int titleH  = 18;   // widget title strip
    const int colH    = 20;   // column-label row
    const int headerH = titleH + colH;
    const int spreadH = 34;
    const int dataH   = h - headerH - spreadH;

    const int targetRowH = 20;
    const int maxLevels  = std::max(5, std::min(25, dataH / 2 / targetRowH));
    const int nAsks      = std::min((int)m_snap.asks.size(), maxLevels);
    const int nBids      = std::min((int)m_snap.bids.size(), maxLevels);
    const int rowH       = std::max(10, dataH / (nAsks + nBids));

    // ── Tick size / decimal precision ─────────────────────────────────────────
    double minDiff = 1.0;
    for (int i = 1; i < (int)m_snap.bids.size() && i < 10; ++i) {
        double d = m_snap.bids[i-1].price - m_snap.bids[i].price;
        if (d > 1e-9) minDiff = std::min(minDiff, d);
    }
    for (int i = 1; i < (int)m_snap.asks.size() && i < 10; ++i) {
        double d = m_snap.asks[i].price - m_snap.asks[i-1].price;
        if (d > 1e-9) minDiff = std::min(minDiff, d);
    }
    const int priceDec = (minDiff < 0.0015) ? 3
                       : (minDiff < 0.015)  ? 2
                       : (minDiff < 0.15)   ? 1 : 0;

    // ── Wall detection: levels > 2.5× average qty across all visible levels ───
    double totalQty = 0.0;
    for (int i = 0; i < nAsks; ++i) totalQty += m_snap.asks[i].quantity;
    for (int i = 0; i < nBids; ++i) totalQty += m_snap.bids[i].quantity;
    const double avgQty       = totalQty / std::max(1, nAsks + nBids);
    const double wallThreshold = avgQty * 2.5;

    // ── Cumulative sums ───────────────────────────────────────────────────────
    std::vector<double> bidCumul(nBids), askCumul(nAsks);
    {
        double cs = 0;
        for (int i = 0; i < nBids; ++i) { cs += m_snap.bids[i].quantity; bidCumul[i] = cs; }
        cs = 0;
        for (int i = 0; i < nAsks; ++i) { cs += m_snap.asks[i].quantity; askCumul[i] = cs; }
    }
    const double totalBid = bidCumul.empty() ? 0.0 : bidCumul.back();
    const double totalAsk = askCumul.empty() ? 0.0 : askCumul.back();

    // ── Column layout: [CUM_bid | QTY_bid | PRICE | QTY_ask | CUM_ask] ────────
    const int priceW = 76;
    const int sideW  = (w - priceW) / 2;
    const int qtyW   = sideW * 6 / 10;
    const int cumW   = sideW - qtyW;
    const int priceX = sideW;

    const int bidCumX = 0;
    const int bidQtyX = cumW;
    const int askQtyX = priceX + priceW;
    const int askCumX = askQtyX + qtyW;

    const int fsize = std::max(9, std::min(11, rowH - 3));
    QFont mono;    mono.setFamily("Consolas");    mono.setPointSize(fsize);
    QFont dimMono; dimMono.setFamily("Consolas"); dimMono.setPointSize(std::max(8, fsize - 1));

    // ── Header ────────────────────────────────────────────────────────────────
    p.fillRect(0, 0, w, headerH, QColor(0x05, 0x08, 0x10));
    p.setPen(QColor(0x0e, 0x16, 0x22));
    p.drawLine(0, headerH - 1, w, headerH - 1);
    // Title row
    {
        QFont tf; tf.setFamily("Segoe UI"); tf.setPointSize(9);
        tf.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        p.setFont(tf);
        p.setPen(QColor(0x48, 0x64, 0x82));
        p.drawText(QRect(0, 0, w, titleH), Qt::AlignCenter, "ORDER BOOK");
    }
    p.setPen(QColor(0x0e, 0x16, 0x22));
    p.drawLine(0, titleH, w, titleH);
    // Column-label row
    {
        QFont hf; hf.setFamily("Segoe UI"); hf.setPointSize(8);
        hf.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
        p.setFont(hf);
        p.setPen(QColor(0x42, 0x5e, 0x7a));
        p.drawText(QRect(bidCumX, titleH, cumW,   colH), Qt::AlignCenter, "CUM");
        p.drawText(QRect(bidQtyX, titleH, qtyW,   colH), Qt::AlignCenter, "QTY");
        p.drawText(QRect(priceX,  titleH, priceW, colH), Qt::AlignCenter, "PRICE");
        p.drawText(QRect(askQtyX, titleH, qtyW,   colH), Qt::AlignCenter, "QTY");
        p.drawText(QRect(askCumX, titleH, cumW,   colH), Qt::AlignCenter, "CUM");
    }

    p.setPen(QColor(0x0e, 0x16, 0x22));
    p.drawLine(priceX,          headerH, priceX,          h);
    p.drawLine(priceX + priceW, headerH, priceX + priceW, h);
    p.setPen(QColor(0x0a, 0x10, 0x18));
    p.drawLine(bidCumX + cumW,  headerH, bidCumX + cumW,  h);
    p.drawLine(askQtyX + qtyW,  headerH, askQtyX + qtyW,  h);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Helper: look up a flash hit for a given price level (tolerance = 1 ULP tick)
    auto findHit = [&](double price) -> const HitLevel * {
        const double tol = std::max(price * 1e-6, 1e-8);
        auto it = m_recentHits.lowerBound(price - tol);
        if (it != m_recentHits.end() && it.key() <= price + tol)
            return &it.value();
        return nullptr;
    };

    // ── Ask rows (worst → best, drawn top → bottom) ───────────────────────────
    int y = headerH;
    for (int i = nAsks - 1; i >= 0; --i) {
        const auto  &lvl   = m_snap.asks[i];
        const bool   best  = (i == 0);
        const bool   isWall = (lvl.quantity >= wallThreshold);
        const int    barW  = static_cast<int>((lvl.quantity / m_stableMax) * qtyW);

        // Row background — walls get a warmer tint
        if (isWall)
            p.fillRect(0, y, w, rowH, best ? QColor(0x28, 0x0a, 0x0a) : QColor(0x1c, 0x06, 0x06));
        else
            p.fillRect(0, y, w, rowH, best ? QColor(0x1a, 0x07, 0x07) : QColor(0x0c, 0x05, 0x05));

        // Quantity bar — walls are fully opaque, normal rows are translucent
        if (barW > 0) {
            const int barAlpha = isWall ? 160 : (best ? 90 : 60);
            p.fillRect(askQtyX, y + 1, std::min(barW, qtyW), rowH - 2,
                       QColor(0xd0, 0x28, 0x28, barAlpha));
            // Wall accent line on right edge of bar
            if (isWall)
                p.fillRect(askQtyX + std::min(barW, qtyW) - 1, y + 1, 1, rowH - 2,
                           QColor(0xff, 0x60, 0x60, 220));
        }

        // Trade flash overlay
        if (const HitLevel *hit = findHit(lvl.price)) {
            const double age        = 1.0 - double(now - hit->ts) / kFlashMs;
            const int    flashAlpha = static_cast<int>(std::max(0.0, age) * 80);
            if (flashAlpha > 0)
                p.fillRect(0, y, w, rowH,
                           hit->isBuy ? QColor(0x00, 0xff, 0x88, flashAlpha)
                                      : QColor(0xff, 0x44, 0x44, flashAlpha));
        }

        p.setFont(mono);
        p.setPen(best ? QColor(0xf5, 0x60, 0x60) : QColor(0xaa, 0x4a, 0x4a));
        p.drawText(QRect(priceX + 2, y, priceW - 4, rowH),
                   Qt::AlignCenter | Qt::AlignVCenter,
                   QString::number(lvl.price, 'f', priceDec));

        p.setPen(isWall ? QColor(0xff, 0xa0, 0xa0)
                        : (best ? QColor(0xf2, 0x88, 0x88) : QColor(0xb0, 0x58, 0x58)));
        p.drawText(QRect(askQtyX + 3, y, qtyW - 4, rowH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(lvl.quantity, 'f', 3));

        p.setFont(dimMono);
        p.setPen(QColor(0x78, 0x30, 0x30, 180));
        p.drawText(QRect(askCumX + 1, y, cumW - 3, rowH),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(askCumul[i], 'f', 2));

        y += rowH;
    }

    // ── Spread / imbalance row ────────────────────────────────────────────────
    p.fillRect(0, y, w, spreadH, QColor(0x08, 0x0c, 0x14));

    const double totalDepth = totalBid + totalAsk;
    if (totalDepth > 0) {
        const int bidBarW = static_cast<int>((totalBid / totalDepth) * w);
        p.fillRect(0,       y, bidBarW,     3, QColor(0x14, 0xa0, 0x4e, 180));
        p.fillRect(bidBarW, y, w - bidBarW, 3, QColor(0xc8, 0x2a, 0x2a, 180));
    }

    {
        QFont sf; sf.setFamily("Consolas"); sf.setPointSize(10);
        p.setFont(sf);
        p.setPen(QColor(0xc0, 0xa0, 0x28));
        p.drawText(QRect(0, y + 3, w, spreadH - 6), Qt::AlignCenter,
                   QString("spread  $%1").arg(m_snap.spread, 0, 'f', priceDec));
    }
    {
        QFont tf; tf.setFamily("Consolas"); tf.setPointSize(9);
        p.setFont(tf);
        p.setPen(QColor(0x20, 0x88, 0x44, 180));
        p.drawText(QRect(4, y + 3, sideW - 4, spreadH - 6),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(totalBid, 'f', 2));
        p.setPen(QColor(0xa0, 0x38, 0x38, 180));
        p.drawText(QRect(priceX + priceW + 2, y + 3, sideW - 4, spreadH - 6),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(totalAsk, 'f', 2));
    }

    p.setFont(mono);
    y += spreadH;

    // ── Bid rows (best → worst) ───────────────────────────────────────────────
    for (int i = 0; i < nBids; ++i) {
        const auto  &lvl    = m_snap.bids[i];
        const bool   best   = (i == 0);
        const bool   isWall = (lvl.quantity >= wallThreshold);
        const int    barW   = static_cast<int>((lvl.quantity / m_stableMax) * qtyW);

        if (isWall)
            p.fillRect(0, y, w, rowH, best ? QColor(0x08, 0x24, 0x0e) : QColor(0x05, 0x18, 0x09));
        else
            p.fillRect(0, y, w, rowH, best ? QColor(0x04, 0x18, 0x09) : QColor(0x03, 0x0c, 0x05));

        if (barW > 0) {
            const int barAlpha = isWall ? 160 : (best ? 90 : 60);
            p.fillRect(priceX - std::min(barW, qtyW), y + 1, std::min(barW, qtyW), rowH - 2,
                       QColor(0x14, 0xb0, 0x50, barAlpha));
            if (isWall)
                p.fillRect(priceX - std::min(barW, qtyW), y + 1, 1, rowH - 2,
                           QColor(0x60, 0xff, 0x90, 220));
        }

        if (const HitLevel *hit = findHit(lvl.price)) {
            const double age        = 1.0 - double(now - hit->ts) / kFlashMs;
            const int    flashAlpha = static_cast<int>(std::max(0.0, age) * 80);
            if (flashAlpha > 0)
                p.fillRect(0, y, w, rowH,
                           hit->isBuy ? QColor(0x00, 0xff, 0x88, flashAlpha)
                                      : QColor(0xff, 0x44, 0x44, flashAlpha));
        }

        p.setFont(mono);
        p.setPen(best ? QColor(0x3a, 0xf0, 0x8c) : QColor(0x24, 0x90, 0x4c));
        p.drawText(QRect(priceX + 2, y, priceW - 4, rowH),
                   Qt::AlignCenter | Qt::AlignVCenter,
                   QString::number(lvl.price, 'f', priceDec));

        p.setPen(isWall ? QColor(0x80, 0xff, 0xb0)
                        : (best ? QColor(0x68, 0xf0, 0xa8) : QColor(0x3a, 0xa8, 0x62)));
        p.drawText(QRect(bidQtyX, y, qtyW - 3, rowH),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(lvl.quantity, 'f', 3));

        p.setFont(dimMono);
        p.setPen(QColor(0x1e, 0x68, 0x34, 180));
        p.drawText(QRect(bidCumX + 2, y, cumW - 3, rowH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(bidCumul[i], 'f', 2));

        y += rowH;
    }
}

// ── Skeleton (no data) ────────────────────────────────────────────────────────
void OrderBookLadder::paintSkeleton(QPainter &p)
{
    const int w = width(), h = height();
    const int titleH = 18, colH = 20, headerH = titleH + colH;
    const int spreadH = 34;
    const int dataH   = h - headerH - spreadH;
    const int rowH    = std::max(10, dataH / 20);
    const int nRows   = std::min(10, dataH / 2 / rowH);

    // Title strip
    p.fillRect(0, 0, w, titleH, QColor(0x05, 0x08, 0x10));
    {
        QFont tf; tf.setFamily("Segoe UI"); tf.setPointSize(9);
        tf.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
        p.setFont(tf);
        p.setPen(QColor(0x14, 0x20, 0x32));
        p.drawText(QRect(0, 0, w, titleH), Qt::AlignCenter, "ORDER BOOK");
    }
    // Column labels
    {
        QFont cf; cf.setFamily("Consolas"); cf.setPointSize(10); p.setFont(cf);
        p.setPen(QColor(0x16, 0x26, 0x3c));
        p.drawText(QRect(4,     titleH, w/3,    colH), Qt::AlignLeft  | Qt::AlignVCenter, "QTY");
        p.drawText(QRect(0,     titleH, w,      colH), Qt::AlignCenter | Qt::AlignVCenter, "PRICE");
        p.drawText(QRect(0,     titleH, w - 4,  colH), Qt::AlignRight | Qt::AlignVCenter, "QTY");
    }
    p.setPen(QColor(0x10, 0x18, 0x28));
    p.drawLine(0, headerH - 1, w, headerH - 1);

    // Deterministic ask/bid bar widths (% of half-width)
    static const int kW[] = {78, 48, 92, 58, 72, 38, 86, 54, 66, 44};

    // Ask rows (top half, dim red tint on right side)
    for (int i = 0; i < nRows; ++i) {
        const int rowY = headerH + (nRows - 1 - i) * rowH;
        const int barW = kW[i % 10] * (w / 2) / 100;
        p.fillRect(QRect(w - barW, rowY + 2, barW, rowH - 3), QColor(0x18, 0x09, 0x09));
        p.fillRect(QRect(4, rowY + 4, w / 2 - 8, 8), QColor(0x0e, 0x16, 0x22));
    }

    // Spread zone
    const int spreadY = headerH + nRows * rowH;
    p.fillRect(0, spreadY, w, spreadH, QColor(0x06, 0x0c, 0x16));
    p.fillRect(w / 2 - 28, spreadY + 8, 56, 11, QColor(0x0f, 0x1a, 0x2c));

    // Bid rows (dim green tint on left side)
    for (int i = 0; i < nRows; ++i) {
        const int rowY = spreadY + spreadH + i * rowH;
        const int barW = kW[(i + 3) % 10] * (w / 2) / 100;
        p.fillRect(QRect(0, rowY + 2, barW, rowH - 3), QColor(0x09, 0x18, 0x0c));
        p.fillRect(QRect(w / 2 + 4, rowY + 4, w / 2 - 8, 8), QColor(0x0e, 0x16, 0x22));
    }

    // Shimmer sweep
    const double ph = ShimmerTimer::phase();
    const double cx = ph * (w + 240.0) - 120.0;
    QLinearGradient g(cx - 120, 0, cx + 120, 0);
    g.setColorAt(0.0, Qt::transparent);
    g.setColorAt(0.5, QColor(0x1e, 0x30, 0x52, 55));
    g.setColorAt(1.0, Qt::transparent);
    p.fillRect(QRect(0, headerH, w, h - headerH), g);
}
