#include "CS2MarketStatsWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

static constexpr int kTitleH   = 30;
static constexpr int kImpactH  = 84;
static constexpr int kMinBodyH = 80;

static QString fmtPrice(double p)
{
    if (p <= 0) return "-";
    return QString("$%1").arg(p, 0, 'f', p >= 1000 ? 0 : 2);
}

static QString fmtPct(double p, bool showPlus = false)
{
    if (p == 0) return "-";
    const char *sign = (p > 0 && showPlus) ? "+" : "";
    return QString("%1%2%").arg(sign).arg(std::abs(p), 0, 'f', 1);
}

// ── Construction ──────────────────────────────────────────────────────────────

CS2MarketStatsWidget::CS2MarketStatsWidget(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet("background:#06090f;");
    setMinimumHeight(90);

    m_shimmerTimer.setInterval(40);  // ~25 fps
    connect(&m_shimmerTimer, &QTimer::timeout, this, [this]() {
        m_shimmer = (m_shimmer + 4) % 200;
        update();
    });
}

// ── Public API ────────────────────────────────────────────────────────────────

void CS2MarketStatsWidget::setSkinName(const QString &name)
{
    m_skinName = name; update();
}

void CS2MarketStatsWidget::setStatusMessage(const QString &msg)
{
    m_state  = State::Status;
    m_status = msg;
    m_shimmerTimer.stop();
    update();
}

void CS2MarketStatsWidget::setOpenSkinPrices(const OpenSkinPrices &prices)
{
    m_wearPrices[m_activeWear] = prices;
    m_prices = prices;
    m_state  = State::Data;
    m_shimmerTimer.stop();
    update();
}

void CS2MarketStatsWidget::setWearPrices(int wearIdx, const OpenSkinPrices &prices)
{
    if (wearIdx < 0 || wearIdx >= kWearCount) return;
    m_wearPrices[wearIdx] = prices;
    if (wearIdx == m_activeWear) {
        m_prices = prices;
        m_state  = State::Data;
        m_shimmerTimer.stop();
        update();
    }
}

void CS2MarketStatsWidget::setActiveWear(int wearIdx)
{
    if (wearIdx < 0 || wearIdx >= kWearCount) return;
    m_activeWear = wearIdx;
    m_prices = m_wearPrices[wearIdx];
    const bool hasData = m_prices.steam.valid || m_prices.skinport.valid
                      || m_prices.buff.valid   || m_prices.csfloat.valid
                      || m_prices.youpin.valid;
    if (hasData) {
        m_state = State::Data;
        m_shimmerTimer.stop();
    }
    update();
}

void CS2MarketStatsWidget::setHealth(const OpenSkinHealth &health)
{
    m_health = health;
    update();
}

void CS2MarketStatsWidget::setLoading(bool loading)
{
    if (loading) {
        m_state  = State::Loading;
        m_shimmer = 0;
        m_shimmerTimer.start();
    } else if (m_state == State::Loading) {
        m_state = State::Idle;
        m_shimmerTimer.stop();
    }
    update();
}

void CS2MarketStatsWidget::setIdle()
{
    m_state = State::Idle;
    m_shimmerTimer.stop();
    update();
}

void CS2MarketStatsWidget::clear()
{
    m_skinName.clear(); m_status.clear();
    m_prices = {};
    m_health = {};
    for (int i = 0; i < kWearCount; ++i)
        m_wearPrices[i] = {};
    m_activeWear = 0;
    m_state  = State::Idle;
    m_shimmerTimer.stop();
    update();
}

// ── paintEvent ────────────────────────────────────────────────────────────────

void CS2MarketStatsWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = rect();
    p.fillRect(r, QColor(0x06, 0x09, 0x0f));

    p.setPen(QColor(0x0e, 0x18, 0x28));
    p.drawLine(0, 0, r.width(), 0);

    // ── Title bar ─────────────────────────────────────────────────────────────
    p.fillRect(0, 0, r.width(), kTitleH, QColor(0x07, 0x0b, 0x14));
    p.setPen(QColor(0x0e, 0x18, 0x28));
    p.drawLine(0, kTitleH, r.width(), kTitleH);

    p.setFont(m_fTitle);
    p.setPen(QColor(0x48, 0x64, 0x82));
    p.drawText(QRect(8, 0, r.width() - 220, kTitleH), Qt::AlignLeft | Qt::AlignVCenter,
               m_skinName.isEmpty() ? "CS2 MARKET ANALYTICS" : m_skinName);

    if (m_prices.updatedAt.isValid()) {
        p.setFont(m_fUpd);
        p.setPen(QColor(0x38, 0x54, 0x72));
        const QString updStr = QString("Updated %1").arg(m_prices.updatedAt.toLocalTime().toString("HH:mm"));
        p.drawText(QRect(r.width() - 260, 0, 130, kTitleH),
                   Qt::AlignRight | Qt::AlignVCenter, updStr);
    }

    p.setFont(m_fBadge);
    p.setPen(QColor(0x30, 0x50, 0x78));
    p.drawText(QRect(r.width() - 128, 0, 124, kTitleH),
               Qt::AlignRight | Qt::AlignVCenter, "openskin.dev");

    const QRect body(0, kTitleH, r.width(), r.height() - kTitleH);

    switch (m_state) {
    case State::Status:
    {
        QFont sf("Segoe UI", 8);
        p.setFont(sf);
        p.setPen(QColor(0x38, 0x58, 0x78));
        p.drawText(body, Qt::AlignCenter, m_status);
        return;
    }
    case State::Idle:
        paintIdle(p, body);
        return;
    case State::Loading:
        paintLoading(p, body);
        return;
    case State::Data:
        break;
    }

    // ── Data layout ───────────────────────────────────────────────────────────
    const bool showImpact = (r.height() >= kTitleH + kMinBodyH + kImpactH);
    const QRect impactRect(0, r.height() - kImpactH, r.width(), kImpactH);
    const QRect bodyRect(0, kTitleH, r.width(),
                         r.height() - kTitleH - (showImpact ? kImpactH : 0));

    const int colW = bodyRect.width() / 3;
    paintArbitrage(p,   QRect(bodyRect.left(),          bodyRect.top(), colW,                    bodyRect.height()));
    paintOrderDepth(p,  QRect(bodyRect.left() + colW,   bodyRect.top(), colW,                    bodyRect.height()));
    paintVolatility(p,  QRect(bodyRect.left() + 2*colW, bodyRect.top(), bodyRect.width()-2*colW, bodyRect.height()));

    p.setPen(QColor(0x0c, 0x14, 0x20));
    p.drawLine(colW,   kTitleH+4, colW,   r.height() - (showImpact ? kImpactH : 0) - 4);
    p.drawLine(2*colW, kTitleH+4, 2*colW, r.height() - (showImpact ? kImpactH : 0) - 4);

    if (showImpact) {
        p.setPen(QColor(0x0c, 0x14, 0x20));
        p.drawLine(0, impactRect.top(), r.width(), impactRect.top());
        paintPriceImpact(p, impactRect);
    }
}

// ── Idle state ────────────────────────────────────────────────────────────────

void CS2MarketStatsWidget::paintIdle(QPainter &p, const QRect &body) const
{
    // Ghost column outlines so the layout is readable before data arrives
    const int colW = body.width() / 3;
    const QColor kGhost(0x0a, 0x10, 0x1a);
    const QColor kGhostBright(0x0e, 0x16, 0x24);

    static const char *kColHeaders[] = { "BEST FLIP", "ORDER DEPTH", "VOLATILITY" };
    static const int   kRowsPerCol[] = { 4, 5, 5 };

    QFont hdrFont("Segoe UI", 7, QFont::Bold);
    for (int c = 0; c < 3; ++c) {
        const int cx = body.left() + c * colW + 8;
        int y = body.top() + 6;
        p.setFont(hdrFont);
        p.setPen(QColor(0x16, 0x22, 0x32));
        p.drawText(QRect(cx, y, colW-16, 12), Qt::AlignLeft | Qt::AlignVCenter, kColHeaders[c]);
        y += 14;
        for (int row = 0; row < kRowsPerCol[c]; ++row) {
            const int barW = (c == 1) ? (colW - 32 - 30) : 44 + row * 6;
            p.fillRect(cx, y + 2, barW, 7, kGhost);
            p.fillRect(cx + barW + 4, y + 2, 28, 7, kGhostBright);
            y += 13;
        }
    }

    // Column dividers
    p.setPen(QColor(0x0c, 0x14, 0x20));
    p.drawLine(colW,   body.top()+4, colW,   body.bottom()-4);
    p.drawLine(2*colW, body.top()+4, 2*colW, body.bottom()-4);

    // Centered "connect" prompt
    const QRect msgR = body.adjusted(0, 10, 0, -10);
    QFont msgFont("Segoe UI", 9, QFont::Bold);
    p.setFont(msgFont);
    p.setPen(QColor(0x20, 0x38, 0x58));
    p.drawText(msgR, Qt::AlignCenter, "Connect to view analytics");
}

// ── Loading shimmer ───────────────────────────────────────────────────────────

void CS2MarketStatsWidget::paintLoading(QPainter &p, const QRect &body) const
{
    const int colW = body.width() / 3;
    // Shimmer: a bright band that sweeps left-to-right across the skeleton bars
    const double phase = m_shimmer / 100.0;   // 0→2, wraps

    auto shimmerAlpha = [&](int x, int barW) -> int {
        const double norm = (barW > 0) ? static_cast<double>(x - body.left()) / body.width() : 0;
        const double dist = std::abs(norm - (phase > 1.0 ? phase - 1.0 : phase));
        return static_cast<int>(std::max(0.0, 1.0 - dist * 8.0) * 60);
    };

    QFont hdrFont("Segoe UI", 7, QFont::Bold);
    static const char *kColHeaders[] = { "BEST FLIP", "ORDER DEPTH", "VOLATILITY" };
    static const int   kRowsPerCol[] = { 4, 5, 5 };

    for (int c = 0; c < 3; ++c) {
        const int cx = body.left() + c * colW + 8;
        int y = body.top() + 6;

        p.setFont(hdrFont);
        p.setPen(QColor(0x18, 0x28, 0x3c));
        p.drawText(QRect(cx, y, colW-16, 12), Qt::AlignLeft | Qt::AlignVCenter, kColHeaders[c]);
        y += 14;

        for (int row = 0; row < kRowsPerCol[c]; ++row) {
            const int barW = (c == 1) ? (colW - 32 - 30) : 44 + row*6;
            const int sa = shimmerAlpha(cx, barW);
            QColor base(0x0d, 0x16, 0x24);
            QColor bright(0x14 + sa/3, 0x22 + sa/3, 0x38 + sa/3);
            p.fillRect(cx, y+2, barW, 7, base);
            if (sa > 0) p.fillRect(cx, y+2, barW, 7, bright);
            p.fillRect(cx+barW+4, y+2, 28, 7, QColor(0x10, 0x1a, 0x2c));
            y += 13;
        }
    }

    p.setPen(QColor(0x0c, 0x14, 0x20));
    p.drawLine(colW,   body.top()+4, colW,   body.bottom()-4);
    p.drawLine(2*colW, body.top()+4, 2*colW, body.bottom()-4);
}

// ── BEST FLIP ─────────────────────────────────────────────────────────────────
//
// Sell-side fee rates (what % of the bid price the seller actually receives).
// Steam is excluded — its proceeds are locked in the Steam wallet (not real cash).
//   CSFloat   2%
//   Buff     2.5%
//   YouPin    3%
//   Skinport 12%
//
// Buy-side: buyer pays the listed ask price directly (no buyer-side fee on any of
// these markets), so buy cost = lowestAskPrice.

void CS2MarketStatsWidget::paintArbitrage(QPainter &p, const QRect &r) const
{
    const int pad = 8;
    const int availH = r.height() - 6;
    const int sp     = std::min(40, std::max(4, (availH - 140) / 5));

    int y = r.top() + 6;
    p.setFont(m_fHdr);
    p.setPen(QColor(0x48, 0x64, 0x82));
    p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 16), Qt::AlignLeft|Qt::AlignVCenter, "BEST FLIP");
    y += 18 + sp;

    const double buyPrice = m_prices.lowestAskPrice;

    // Evaluate every possible sell destination after fees
    struct SellOpt {
        const char *label;
        double      bidPrice;   // raw bid (what buyer offers)
        double      feeRate;    // fraction seller keeps (1 - fee%)
    };
    const SellOpt kOpts[] = {
        { "CSFLOAT",  m_prices.csfloat.bid,  0.98  },
        { "BUFF",     m_prices.buff.bid,      0.975 },
        { "YOUPIN",   m_prices.youpin.bid,    0.97  },
        { "SKINPORT", m_prices.skinport.bid,  0.88  },
    };

    double      bestNet   = 0;
    const char *bestLabel = nullptr;
    double      bestBid   = 0;
    for (const auto &o : kOpts) {
        if (o.bidPrice <= 0) continue;
        const double net = o.bidPrice * o.feeRate;
        if (net > bestNet) {
            bestNet   = net;
            bestLabel = o.label;
            bestBid   = o.bidPrice;
        }
    }

    const bool canFlip = buyPrice > 0 && bestNet > 0 && bestNet > buyPrice;
    const double profit = canFlip ? bestNet - buyPrice : 0;
    const double pct    = canFlip ? (profit / buyPrice * 100.0) : 0;

    if (!canFlip) {
        static constexpr int kLineH = 17;
        p.setFont(m_fSub);
        p.setPen(QColor(0x48, 0x60, 0x7e));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, kLineH*3),
                   Qt::AlignLeft|Qt::AlignTop, "No profitable\nflip opportunity\n(after fees)");
        y += kLineH * 3 + sp;
    } else {
        // Buy side
        p.setFont(m_fSub); p.setPen(QColor(0x38, 0x58, 0x78));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 14), Qt::AlignLeft|Qt::AlignVCenter,
                   QString("Buy %1").arg(m_prices.lowestAskMarket.toUpper()));
        y += 14;
        p.setFont(m_fVal); p.setPen(QColor(0x50, 0x80, 0xb0));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 18), Qt::AlignLeft|Qt::AlignVCenter, fmtPrice(buyPrice));
        y += 18 + sp;

        // Sell side — show market + fee deduction
        const int feeInt = qRound((1.0 - (bestNet / bestBid)) * 100.0);
        p.setFont(m_fSub); p.setPen(QColor(0x38, 0x58, 0x78));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 14), Qt::AlignLeft|Qt::AlignVCenter,
                   QString("Sell %1 (\xe2\x88\x92%2% fee)").arg(bestLabel).arg(feeInt));
        y += 14;
        p.setFont(m_fVal); p.setPen(QColor(0x50, 0xa0, 0xd0));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 18), Qt::AlignLeft|Qt::AlignVCenter, fmtPrice(bestNet));
        y += 18 + sp;

        // Net profit
        p.setFont(m_fVal); p.setPen(QColor(0x20, 0xe0, 0x80));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 18), Qt::AlignLeft|Qt::AlignVCenter,
                   QString("+%1 / +%2%").arg(fmtPrice(profit)).arg(pct, 0, 'f', 1));
        y += 18;

        // Real money confirmation
        p.setFont(m_fSub); p.setPen(QColor(0x20, 0x80, 0x50));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 14), Qt::AlignLeft|Qt::AlignVCenter,
                   "real money");
        y += 14 + sp;
    }

    if (m_prices.skinportSuggested > 0 && y + 30 < r.bottom()) {
        p.setPen(QColor(0x0e, 0x18, 0x28));
        p.drawLine(r.left()+pad, y+2, r.right()-pad, y+2);
        y += 8;
        p.setFont(m_fSmall); p.setPen(QColor(0x48, 0x60, 0x7e));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 13), Qt::AlignLeft|Qt::AlignVCenter, "FAIR VALUE");
        y += 13;
        p.setFont(m_fVal); p.setPen(QColor(0xa0, 0x60, 0xd0));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 18), Qt::AlignLeft|Qt::AlignVCenter,
                   fmtPrice(m_prices.skinportSuggested));
    }
}

// ── ORDER DEPTH ───────────────────────────────────────────────────────────────

void CS2MarketStatsWidget::paintOrderDepth(QPainter &p, const QRect &r) const
{
    const int pad = 8;
    int y = r.top() + 6;
    p.setFont(m_fHdr);
    p.setPen(QColor(0x48, 0x64, 0x82));
    p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 16), Qt::AlignLeft|Qt::AlignVCenter, "ORDER DEPTH");
    y += 20;

    const int buyOrders  = m_prices.steam.bidVol;
    const int sellOrders = m_prices.steam.askVol;
    const int total      = buyOrders + sellOrders;

    // Depth bar — height scales with available space (min 12, max 28)
    const int barAreaW = r.width() - 2*pad;
    const int barH     = std::min(28, std::max(12, (r.height() - 120) / 8));
    p.fillRect(r.left()+pad, y, barAreaW, barH, QColor(0x0c, 0x14, 0x20));
    if (total > 0) {
        const int buyW = static_cast<int>(barAreaW * static_cast<double>(buyOrders) / total);
        if (buyW > 0)
            p.fillRect(r.left()+pad, y, buyW, barH, QColor(0x20, 0x99, 0x50));
        if (barAreaW - buyW > 0)
            p.fillRect(r.left()+pad+buyW, y, barAreaW-buyW, barH, QColor(0xaa, 0x30, 0x30));
    }
    y += barH + 6;

    // Buy / Sell counts + ratio
    if (total > 0) {
        const double ratio = (sellOrders > 0)
            ? static_cast<double>(buyOrders) / sellOrders : 99.0;
        p.setFont(m_fLbl); p.setPen(QColor(0x20, 0x99, 0x50));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 14),
                   Qt::AlignLeft|Qt::AlignVCenter,
                   QString("%1%2 buy").arg(QChar(0x2191)).arg(buyOrders));
        p.setPen(QColor(0xaa, 0x40, 0x40));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 14),
                   Qt::AlignRight|Qt::AlignVCenter,
                   QString("%1%2 sell").arg(QChar(0x2193)).arg(sellOrders));
        y += 14;

        p.setFont(m_fSmall); p.setPen(QColor(0x38, 0x58, 0x78));
        const int ratioInt = (sellOrders > 0)
            ? static_cast<int>(std::min(static_cast<double>(buyOrders) / sellOrders, 999.0))
            : buyOrders;
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 12),
                   Qt::AlignHCenter|Qt::AlignVCenter,
                   (ratio >= 99.0) ? QString("RATIO: %1:1").arg(ratioInt)
                                   : QString("RATIO: %1:1").arg(ratio, 0, 'f', 1));
        y += 14;
    } else {
        p.setFont(m_fLbl); p.setPen(QColor(0x24, 0x34, 0x48));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 14),
                   Qt::AlignLeft|Qt::AlignVCenter, "No Steam order data");
        y += 18;
    }

    p.setPen(QColor(0x0e, 0x16, 0x24));
    p.drawLine(r.left()+pad, y, r.right()-pad, y);
    y += 6;

    // Metrics rows — row height scales with available space
    struct MRow { QString label; QString value; QColor color; };
    const bool noMetrics = (m_prices.spreadPct == 0 && m_prices.slippagePct == 0
                            && m_prices.steamLiquidity == 0);

    QList<MRow> mrows;
    if (m_prices.steamVolume24h > 0)
        mrows.append({"24H VOL", QString("%1 sold").arg(m_prices.steamVolume24h), QColor(0x50, 0x78, 0xa0)});

    if (!noMetrics) {
        QString spreadStr = fmtPrice(m_prices.spreadAbs);
        if (m_prices.spreadPct > 0) spreadStr += QString(" (%1%)").arg(m_prices.spreadPct, 0, 'f', 1);
        mrows.append({"SPREAD", spreadStr, QColor(0x60, 0x90, 0xb8)});

        QString slipStr = fmtPrice(m_prices.slippageAbs);
        if (m_prices.slippagePct > 0) slipStr += QString(" (%1%)").arg(m_prices.slippagePct, 0, 'f', 1);
        mrows.append({"SLIP", slipStr, QColor(0xc0, 0x80, 0x40)});

        const int liq = m_prices.steamLiquidity;
        mrows.append({"STM LIQ",
                      liq > 0 ? QString("%1/100").arg(liq) : "-",
                      liq >= 70 ? QColor(0x30,0xcc,0x70) : liq >= 40 ? QColor(0xd0,0xb0,0x30)
                                                                       : QColor(0xcc,0x40,0x40)});
    }

    // Distribute remaining height across metric rows
    const int remaining = r.bottom() - y - 4;
    const int nRows     = static_cast<int>(mrows.size());
    const int rowH      = nRows > 0 ? std::min(52, std::max(13, remaining / nRows)) : 14;
    const int labelW = 56;
    for (const auto &mr : mrows) {
        if (y + rowH > r.bottom()) break;
        p.setFont(m_fSmall); p.setPen(QColor(0x48, 0x64, 0x82));
        p.drawText(QRect(r.left()+pad, y, labelW, rowH), Qt::AlignLeft|Qt::AlignVCenter, mr.label);
        p.setFont(m_fValBold); p.setPen(mr.color);
        p.drawText(QRect(r.left()+pad+labelW, y, r.width()-2*pad-labelW, rowH),
                   Qt::AlignLeft|Qt::AlignVCenter, mr.value);
        y += rowH;
    }
}

// ── VOLATILITY ────────────────────────────────────────────────────────────────

void CS2MarketStatsWidget::paintVolatility(QPainter &p, const QRect &r) const
{
    const int pad = 8;
    int y = r.top() + 6;

    p.setFont(m_fHdr); p.setPen(QColor(0x48, 0x64, 0x82));
    p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 15), Qt::AlignLeft|Qt::AlignVCenter, "VOLATILITY");
    y += 18;

    const double steamVols[5] = {
        m_prices.volatility1d, m_prices.volatility7d,
        m_prices.volatility30d, m_prices.volatility90d, m_prices.volatilityAll
    };
    const char *steamLabels[5] = { "1D", "7D", "30D", "90D", "ALL" };

    double maxVol = 0.0;
    for (int i = 0; i < 5; ++i) if (steamVols[i] > maxVol) maxVol = steamVols[i];
    const double spVols[3] = { m_prices.skinportVol1d, m_prices.skinportVol7d, m_prices.skinportVolAll };
    for (int i = 0; i < 3; ++i) if (spVols[i] > maxVol) maxVol = spVols[i];

    if (maxVol == 0.0) {
        p.setFont(m_fSmall); p.setPen(QColor(0x24, 0x34, 0x48));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, r.height()-(y-r.top())-4),
                   Qt::AlignLeft | Qt::AlignTop, "Insufficient\nprice data");
        return;
    }
    if (maxVol < 1.0) maxVol = 1.0;

    const int barAreaW = r.width() - 2*pad - 30 - 38;
    const int labelW   = 30;
    const int valW     = 38;

    const int availH    = r.height() - (y - r.top()) - 6;
    const int steamRowH = std::min(36, std::max(14, (availH - 22) / 8));
    const int spRowH    = std::max(12, steamRowH - 2);

    // Pre-compute the static Skinport pen color (avoids lighter() per bar)
    static const QColor kSpCol(0xa0, 0x60, 0xd0, 160);
    static const QColor kSpPen = kSpCol.lighter(150);
    static const QColor kBarBg(0x0c, 0x14, 0x20);
    static const QColor kSpBarBg(0x0c, 0x10, 0x18);

    for (int i = 0; i < 5; ++i) {
        if (y + steamRowH > r.bottom()) break;
        const double val = steamVols[i];

        p.setFont(m_fSmall); p.setPen(QColor(0x30, 0x48, 0x64));
        p.drawText(QRect(r.left()+pad, y, labelW, steamRowH), Qt::AlignLeft|Qt::AlignVCenter, steamLabels[i]);

        const int barX = r.left()+pad+labelW;
        const int barW = (val > 0 && maxVol > 0)
            ? static_cast<int>(barAreaW * std::min(val / maxVol, 1.0)) : 0;
        const int barH = steamRowH - 4;

        // Build color inline — lighter() avoided by using a brighter pen derived here once
        const double ratio = val / 100.0;
        const int rr = static_cast<int>(std::min(255.0, ratio * 2.0 * 255));
        const int gg = static_cast<int>(std::min(255.0, (1.0 - std::max(0.0, ratio - 0.5)*2.0) * 180));
        const QColor barCol(rr, gg, 40, 180);
        const QColor barPen(std::min(255, rr + 60), std::min(255, gg + 50), 100);

        p.fillRect(barX, y+2, barAreaW, barH, kBarBg);
        if (barW > 0) p.fillRect(barX, y+2, barW, barH, barCol);

        p.setFont(m_fLbl); p.setPen(barPen);
        p.drawText(QRect(barX+barAreaW+2, y, valW, steamRowH), Qt::AlignLeft|Qt::AlignVCenter,
                   val > 0 ? QString("%1%").arg(val, 0, 'f', 1) : "-");
        y += steamRowH;
    }

    if (y + 4 < r.bottom()) {
        p.setPen(QColor(0x0e, 0x16, 0x24));
        p.drawLine(r.left()+pad, y+2, r.right()-pad, y+2);
        y += 6;
    }
    if (y + 12 < r.bottom()) {
        p.setFont(m_fSubHdr); p.setPen(QColor(0x80, 0x50, 0xa8));
        p.drawText(QRect(r.left()+pad, y, r.width()-2*pad, 12),
                   Qt::AlignLeft|Qt::AlignVCenter, "SKINPORT");
        y += 13;
    }

    const char *spLabels[3] = { "1D", "7D", "ALL" };
    for (int i = 0; i < 3; ++i) {
        if (y + spRowH > r.bottom()) break;
        const double val = spVols[i];

        p.setFont(m_fSmall); p.setPen(QColor(0x60, 0x40, 0x80));
        p.drawText(QRect(r.left()+pad, y, labelW, spRowH), Qt::AlignLeft|Qt::AlignVCenter, spLabels[i]);

        const int barX = r.left()+pad+labelW;
        const int barW = (val > 0 && maxVol > 0)
            ? static_cast<int>(barAreaW * std::min(val / maxVol, 1.0)) : 0;
        const int barH = spRowH - 4;

        p.fillRect(barX, y+2, barAreaW, barH, kSpBarBg);
        if (barW > 0) p.fillRect(barX, y+2, barW, barH, kSpCol);

        p.setFont(m_fLbl); p.setPen(kSpPen);
        p.drawText(QRect(barX+barAreaW+2, y, valW, spRowH), Qt::AlignLeft|Qt::AlignVCenter,
                   val > 0 ? QString("%1%").arg(val, 0, 'f', 1) : "-");
        y += spRowH;
    }
}

// ── PRICE IMPACT strip ────────────────────────────────────────────────────────

void CS2MarketStatsWidget::paintPriceImpact(QPainter &p, const QRect &r) const
{
    const int pad = 8;

    // Section header spanning full width
    p.setFont(m_fHdr); p.setPen(QColor(0x48, 0x64, 0x82));
    p.drawText(QRect(r.left()+pad, r.top()+4, r.width()-2*pad, 18),
               Qt::AlignLeft|Qt::AlignVCenter, "STEAM BULK BUY COST");

    // 4 equal columns below the header
    const int colY    = r.top() + 24;
    const int colH    = r.height() - 28;
    const int segW    = std::max(1, (r.width() - 2*pad) / 4);
    const int qtyH    = 16;
    const int priceH  = 20;
    const int pctH    = 15;
    const int spacing = std::max(0, (colH - qtyH - priceH - pctH) / 3);

    for (int i = 0; i < 4; ++i) {
        const PriceImpactLevel &lv = m_prices.priceImpact[i];
        const int x = r.left() + pad + i * segW;
        if (!lv.valid) continue;

        int y = colY + spacing / 2;

        // ×N quantity label
        p.setFont(m_fQty); p.setPen(QColor(0x30, 0x48, 0x64));
        p.drawText(QRect(x, y, segW-4, qtyH), Qt::AlignLeft|Qt::AlignVCenter,
                   QString("%1%2").arg(QChar(0xD7)).arg(lv.units));
        y += qtyH + spacing;

        // Average price
        p.setFont(m_fVal); p.setPen(QColor(0x70, 0xa8, 0xd0));
        p.drawText(QRect(x, y, segW-4, priceH), Qt::AlignLeft|Qt::AlignVCenter,
                   fmtPrice(lv.avgPrice));
        y += priceH + spacing;

        // % impact (green = no impact, orange/red = expensive)
        const QString pct = lv.pctAboveAsk > 0
            ? QString("+%1%").arg(lv.pctAboveAsk, 0, 'f', 1)
            : "at ask";
        const QColor pctCol = lv.pctAboveAsk <= 0 ? QColor(0x30, 0xaa, 0x60)
                            : lv.pctAboveAsk < 5  ? QColor(0xd0, 0x90, 0x30)
                                                   : QColor(0xd0, 0x50, 0x30);
        p.setFont(m_fPct); p.setPen(pctCol);
        p.drawText(QRect(x, y, segW-4, pctH), Qt::AlignLeft|Qt::AlignVCenter, pct);
    }
}
