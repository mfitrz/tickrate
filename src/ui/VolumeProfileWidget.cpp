#include "VolumeProfileWidget.h"
#include "util/Indicators.h"
#include "util/ShimmerTimer.h"
#include <QPainter>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

VolumeProfileWidget::VolumeProfileWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(90);
    setStyleSheet("background: #080c12;");
    m_profile.resize(kBins);

    connect(&ShimmerTimer::instance(), &ShimmerTimer::tick,
            this, [this] { if (m_skelActive) update(); });
}

void VolumeProfileWidget::addTrade(const TradeInfo &trade)
{
    m_skelActive = false;
    m_trades.push_back({trade.price, trade.size, trade.isBuy});
    if (static_cast<int>(m_trades.size()) > kMaxTrades)
        m_trades.pop_front();
    m_dirty = true;
    update();
}

void VolumeProfileWidget::setSnapshot(const BookSnapshot &snap)
{
    if (snap.midPrice <= 0.0) return;

    // Price range covers ~50 levels on each side using best bid/ask as anchors
    double mid   = snap.midPrice;
    double range = snap.spread > 0.0
                   ? snap.spread * 150.0   // scale from spread
                   : mid * 0.02;           // 2% fallback
    double newMin = mid - range / 2.0;
    double newMax = mid + range / 2.0;

    if (std::fabs(newMin - m_rangeMin) > m_rangeMin * 1e-6 ||
        std::fabs(newMax - m_rangeMax) > m_rangeMax * 1e-6)
    {
        m_rangeMin = newMin;
        m_rangeMax = newMax;
        m_dirty = true;
    }
    m_midPrice = mid;
    update();
}

void VolumeProfileWidget::clear()
{
    m_trades.clear();
    std::fill(m_profile.begin(), m_profile.end(), Bin{});
    m_dirty      = false;
    m_skelActive = true;
    update();
}

void VolumeProfileWidget::recompute()
{
    std::fill(m_profile.begin(), m_profile.end(), Bin{});
    if (m_rangeMax <= m_rangeMin) { m_dirty = false; return; }

    const double binSize = (m_rangeMax - m_rangeMin) / kBins;
    for (const auto &t : m_trades) {
        int idx = static_cast<int>((t.price - m_rangeMin) / binSize);
        if (idx < 0 || idx >= kBins) continue;
        if (t.isBuy) m_profile[idx].buy  += t.size;
        else         m_profile[idx].sell += t.size;
    }
    m_dirty = false;
}

void VolumeProfileWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int w = width();
    const int h = height();
    p.fillRect(rect(), QColor(0x08, 0x0c, 0x12));

    constexpr int headerH = 26;
    constexpr int priceW  = 0;    // no dedicated price axis on this widget
    const int chartH = h - headerH;

    // Header
    p.fillRect(0, 0, w, headerH, QColor(0x05, 0x08, 0x10));
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(0, headerH - 1, w, headerH - 1);
    {
        QFont f; f.setFamily("Segoe UI"); f.setPointSize(9);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        p.setFont(f);
        p.setPen(QColor(0x48, 0x64, 0x80));
        p.drawText(QRect(0, 0, w, headerH), Qt::AlignCenter, "VOL PROFILE");
        // Non-color direction legend: S (sell, left of each bar) | B (buy, right)
        QFont lf; lf.setFamily("Consolas"); lf.setPointSize(7);
        p.setFont(lf);
        p.setPen(QColor(0xcc, 0x30, 0x30, 180));
        p.drawText(QRect(3, 0, 10, headerH), Qt::AlignLeft | Qt::AlignVCenter, "S");
        p.setPen(QColor(0x00, 0xaa, 0x55, 180));
        p.drawText(QRect(w - 13, 0, 10, headerH), Qt::AlignRight | Qt::AlignVCenter, "B");
    }

    if (m_rangeMax <= m_rangeMin || m_trades.empty()) {
        paintSkeleton(p);
        return;
    }

    if (m_dirty) recompute();

    // Find max volume bin (POC) and global max for scaling
    double maxVol = 1e-12;
    std::vector<double> binVols(kBins);
    for (int i = 0; i < kBins; ++i) {
        binVols[i] = m_profile[i].buy + m_profile[i].sell;
        if (binVols[i] > maxVol) maxVol = binVols[i];
    }
    const auto va = computeValueArea(binVols);

    const double priceRange = m_rangeMax - m_rangeMin;
    const double binH = static_cast<double>(chartH) / kBins;
    const int    maxBarW = std::max(1, w - 2);
    auto priceToY = [&](double price) -> int {
        const double frac = (price - m_rangeMin) / priceRange;
        return headerH + static_cast<int>((1.0 - frac) * chartH);
    };

    for (int i = 0; i < kBins; ++i) {
        const double buy  = m_profile[i].buy;
        const double sell = m_profile[i].sell;
        const double tot  = binVols[i];
        if (tot < 1e-12) continue;

        // Y position: bin 0 = bottom of price range (low price), bin kBins-1 = top
        // We draw top-to-bottom so highest price (kBins-1) is at the top of the widget
        const int binTop = headerH + static_cast<int>((kBins - 1 - i) * binH);
        const int bh     = std::max(1, static_cast<int>(binH));

        const bool isPoc = (i == va.pocIdx);
        const int totalBarW = static_cast<int>(maxBarW * tot / maxVol);
        const int buyBarW   = static_cast<int>(totalBarW * buy / tot);
        const int sellBarW  = totalBarW - buyBarW;

        const int alpha = isPoc ? 220 : 130;
        // Sell (red) drawn first from left, buy (green) on top
        if (sellBarW > 0)
            p.fillRect(0, binTop, sellBarW, bh, QColor(0xcc, 0x30, 0x30, alpha));
        if (buyBarW > 0)
            p.fillRect(sellBarW, binTop, buyBarW, bh, QColor(0x00, 0xaa, 0x55, alpha));

        // POC highlight: bright top border
        if (isPoc) {
            p.setPen(QColor(0xff, 0xd0, 0x30, 200));
            p.drawLine(0, binTop, totalBarW, binTop);
        }
    }

    // Mid-price line
    if (m_midPrice > m_rangeMin && m_midPrice < m_rangeMax) {
        double frac = (m_midPrice - m_rangeMin) / priceRange;
        int midY = headerH + static_cast<int>((1.0 - frac) * chartH);
        p.setPen(QPen(QColor(0x5a, 0xa3, 0xf5, 180), 1, Qt::DashLine));
        p.drawLine(0, midY, w, midY);
    }

    // ── Value Area (70% of total volume) ────────────────────────────────────
    {
        const double binSize  = priceRange / kBins;
        const double vahPrice = m_rangeMin + (va.hiIdx + 1) * binSize;
        const double valPrice = m_rangeMin + va.loIdx        * binSize;
        const int    vahY     = priceToY(vahPrice);
        const int    valY     = priceToY(valPrice);

        if (vahY < valY)
            p.fillRect(0, vahY, w, valY - vahY, QColor(0x20, 0x30, 0x48, 40));

        QFont lf; lf.setFamily("Consolas"); lf.setPointSize(6); lf.setBold(true);
        p.setFont(lf);

        constexpr int minGap = 11;
        if (vahY >= headerH && vahY <= headerH + chartH) {
            p.setPen(QColor(0x50, 0x90, 0xcc, 200));
            p.drawLine(0, vahY, w, vahY);
            p.drawText(QRect(1, vahY - 9, w - 2, 9), Qt::AlignRight, "VAH");
        }
        if (valY >= headerH && valY <= headerH + chartH &&
            std::abs(valY - vahY) >= minGap) {
            p.setPen(QColor(0x50, 0x90, 0xcc, 200));
            p.drawLine(0, valY, w, valY);
            p.drawText(QRect(1, valY + 1, w - 2, 9), Qt::AlignRight, "VAL");
        }
    }

    // POC line + label — suppress if within 11 px of VAH or VAL
    {
        const double binSz = priceRange / kBins;
        const int pocY  = priceToY(m_rangeMin + (va.pocIdx + 0.5) * binSz);
        const int vahY2 = priceToY(m_rangeMin + (va.hiIdx + 1)    * binSz);
        const int valY2 = priceToY(m_rangeMin + va.loIdx           * binSz);

        QFont f; f.setFamily("Consolas"); f.setPointSize(6); f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0xff, 0xd0, 0x30));
        p.drawLine(0, pocY, w, pocY);
        if (std::abs(pocY - vahY2) >= 11 && std::abs(pocY - valY2) >= 11)
            p.drawText(QRect(1, pocY - 9, w - 2, 9), Qt::AlignRight, "POC");
    }

    // Left border
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(0, headerH, 0, h);
}

// ── Skeleton (no data) ────────────────────────────────────────────────────────
void VolumeProfileWidget::paintSkeleton(QPainter &p)
{
    const int w = width(), h = height();
    constexpr int headerH = 26;
    const int chartH = h - headerH;

    // Deterministic horizontal bar widths (% of widget width)
    static const int kBW[] = {72, 45, 88, 55, 80, 38, 68, 60, 92, 42,
                               76, 50, 84, 35, 70, 58, 62, 48, 78, 52};
    const int nBars = std::min(20, chartH / 8);
    const int barH  = std::max(4, (chartH - nBars) / nBars);

    for (int i = 0; i < nBars; ++i) {
        const int by  = headerH + i * (barH + 1);
        const int bw  = kBW[i % 20] * (w - 2) / 100;
        const bool buy = (i % 4 != 3);   // mostly buy bars, occasional sell
        p.fillRect(QRect(1, by, bw, barH),
                   buy ? QColor(0x08, 0x18, 0x10) : QColor(0x18, 0x08, 0x08));
    }

    // Left border
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(0, headerH, 0, h);

    // Shimmer sweep
    const double ph = ShimmerTimer::phase();
    const double cx = ph * (w + 240.0) - 120.0;
    QLinearGradient g(cx - 120, 0, cx + 120, 0);
    g.setColorAt(0.0, Qt::transparent);
    g.setColorAt(0.5, QColor(0x1e, 0x30, 0x52, 50));
    g.setColorAt(1.0, Qt::transparent);
    p.fillRect(QRect(0, headerH, w, chartH), g);
}
