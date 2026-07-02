#include "FloatHistogramWidget.h"
#include <QPainter>
#include <algorithm>
#include <vector>

FloatHistogramWidget::FloatHistogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(200);
    setMinimumHeight(90);
    setStyleSheet("background: #080c12;");
}

void FloatHistogramWidget::setListings(const QVector<CS2Listing> &listings)
{
    m_listings = listings;
    update();
}

void FloatHistogramWidget::clear()
{
    m_listings.clear();
    update();
}

void FloatHistogramWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(0x08, 0x0c, 0x12));

    const int w = width();
    const int h = height();

    // ── Title ─────────────────────────────────────────────────────────────────
    const int titleH = 14;
    p.fillRect(0, 0, w, titleH, QColor(0x05, 0x08, 0x10));
    {
        QFont tf; tf.setFamily("Segoe UI"); tf.setPointSize(7);
        tf.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        p.setFont(tf);
        p.setPen(QColor(0x48, 0x64, 0x82));
        p.drawText(QRect(0, 0, w, titleH), Qt::AlignCenter, "FLOAT DISTRIBUTION");
    }
    p.setPen(QColor(0x0e, 0x16, 0x22));
    p.drawLine(0, titleH, w, titleH);

    if (m_listings.isEmpty()) {
        QFont wf; wf.setFamily("Segoe UI"); wf.setPointSize(9);
        p.setFont(wf);
        p.setPen(QColor(40, 52, 68));
        p.drawText(QRect(0, titleH, w, h - titleH),
                   Qt::AlignCenter, "Waiting for data\xe2\x80\xa6");
        return;
    }

    // ── Histogram ─────────────────────────────────────────────────────────────
    const int mLeft  = 28;
    const int mRight = 6;
    const int mTop   = titleH + 4;
    const int mBot   = 24;
    const int chartW = w - mLeft - mRight;
    const int chartH = h - mTop - mBot;

    // 50 bins across [0, 1]
    const int kBins = 50;
    std::vector<int> bins(kBins, 0);
    for (const CS2Listing &l : m_listings) {
        const double f = l.floatValue;
        if (f >= 0.0 && f < 1.0)
            bins[static_cast<int>(f * kBins)]++;
    }
    const int maxBin = *std::max_element(bins.begin(), bins.end());
    if (maxBin == 0) return;

    // Wear tier color by float midpoint
    struct Tier { double lo, hi; QColor col; };
    static const Tier kTiers[] = {
        {0.00, 0.07, QColor(0x60, 0xa8, 0xff, 200)},
        {0.07, 0.15, QColor(0x50, 0xd8, 0x70, 200)},
        {0.15, 0.38, QColor(0xe0, 0xb8, 0x30, 200)},
        {0.38, 0.45, QColor(0xe0, 0x70, 0x20, 200)},
        {0.45, 1.00, QColor(0xc0, 0x45, 0x45, 200)},
    };

    const double binW = static_cast<double>(chartW) / kBins;
    for (int b = 0; b < kBins; ++b) {
        if (bins[b] == 0) continue;
        const double fMid  = (b + 0.5) / kBins;
        const qreal  x     = mLeft + b * binW;
        const qreal  bw    = std::max(1.0, binW - 1.0);
        const qreal  bh    = static_cast<double>(bins[b]) / maxBin * chartH;
        const qreal  by    = mTop + chartH - bh;

        QColor col(0x50, 0x70, 0x90, 180);
        for (const Tier &t : kTiers) {
            if (fMid >= t.lo && fMid < t.hi) { col = t.col; break; }
        }
        p.fillRect(QRectF(x, by, bw, bh), col);
    }

    // ── Wear tier boundary lines + labels ─────────────────────────────────────
    static const struct { double pos; const char *name; QColor col; } kBounds[] = {
        {0.07, "0.07", QColor(0x28, 0x40, 0x60)},
        {0.15, "0.15", QColor(0x28, 0x40, 0x60)},
        {0.38, "0.38", QColor(0x28, 0x40, 0x60)},
        {0.45, "0.45", QColor(0x28, 0x40, 0x60)},
    };

    {
        QFont lf; lf.setFamily("Consolas"); lf.setPointSize(6);
        p.setFont(lf);
        for (const auto &b : kBounds) {
            const qreal bx = mLeft + b.pos * chartW;
            p.setPen(QPen(QColor(0x20, 0x30, 0x48, 130), 1, Qt::DotLine));
            p.drawLine(QPointF(bx, mTop), QPointF(bx, mTop + chartH));
            p.setPen(b.col);
            p.drawText(QRect(static_cast<int>(bx) - 14, mTop + chartH + 1, 28, 10),
                       Qt::AlignCenter, b.name);
        }
    }

    // ── Tier name labels ──────────────────────────────────────────────────────
    static const struct { double mid; const char *label; QColor col; } kLabels[] = {
        {0.035, "FN", QColor(0x60, 0xa8, 0xff)},
        {0.11,  "MW", QColor(0x50, 0xd8, 0x70)},
        {0.265, "FT", QColor(0xe0, 0xb8, 0x30)},
        {0.415, "WW", QColor(0xe0, 0x70, 0x20)},
        {0.725, "BS", QColor(0xc0, 0x45, 0x45)},
    };
    {
        QFont lf; lf.setFamily("Segoe UI"); lf.setPointSize(7);
        p.setFont(lf);
        for (const auto &lb : kLabels) {
            const int lx = static_cast<int>(mLeft + lb.mid * chartW);
            p.setPen(lb.col);
            p.drawText(QRect(lx - 12, mTop + chartH + 12, 24, 12),
                       Qt::AlignCenter, lb.label);
        }
    }

    // ── Y-axis max label + baseline ───────────────────────────────────────────
    p.setPen(QColor(0x14, 0x1e, 0x2c));
    p.drawLine(mLeft, mTop + chartH, w - mRight, mTop + chartH);
    {
        QFont lf; lf.setFamily("Consolas"); lf.setPointSize(6);
        p.setFont(lf);
        p.setPen(QColor(0x42, 0x5e, 0x7a));
        p.drawText(QRect(0, mTop, mLeft - 2, 12),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(maxBin));
    }
}
