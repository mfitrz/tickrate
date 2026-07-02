#include "TapeWidget.h"
#include "util/Indicators.h"
#include "util/ShimmerTimer.h"
#include <QPainter>
#include <QLinearGradient>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QDateTime>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

TapeWidget::TapeWidget(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet("background: #080c12;");

    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    m_scrollBar->setStyleSheet(
        "QScrollBar:vertical { background:#080c12; width:5px; border:none; }"
        "QScrollBar::handle:vertical { background:#1a2638; border-radius:2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");
    m_scrollBar->hide();

    connect(m_scrollBar, &QScrollBar::valueChanged, this, [this](int v) {
        m_scrollOff = v;
        update();
    });

    connect(&ShimmerTimer::instance(), &ShimmerTimer::tick,
            this, [this] { if (m_skelActive) update(); });
}

int TapeWidget::priceDec(double price) const
{
    if (price >= 10000.0) return 1;
    if (price >= 1000.0)  return 2;
    if (price >= 100.0)   return 3;
    if (price >= 1.0)     return 4;
    return 5;
}

void TapeWidget::addTrade(const TradeInfo &t)
{
    if (t.price <= 0 || t.size <= 0) return;
    m_skelActive = false;

    ++m_tradeCount;
    const double alpha = 0.04;
    m_sizeEma = (m_tradeCount == 1) ? t.size
                                    : (m_sizeEma * (1.0 - alpha) + t.size * alpha);

    const int tier = tradeTier(t.size, m_sizeEma, m_tradeCount);

    m_trades.push_front({t.timestampMs, t.price, t.size, t.isBuy, tier});
    if (static_cast<int>(m_trades.size()) > kMaxTrades)
        m_trades.pop_back();

    // Keep scroll position if user has scrolled down, else stay at top
    if (m_scrollOff > 0)
        m_scrollOff = std::min(m_scrollOff + 1, (int)m_trades.size() - 1);

    updateScrollBar();
    update();
}

void TapeWidget::updateScrollBar()
{
    const int visible = std::max(1, (height() - kHeaderH) / kRowH);
    const int total   = static_cast<int>(m_trades.size());
    if (total <= visible) {
        m_scrollBar->hide();
        m_scrollOff = 0;
        return;
    }
    m_scrollBar->setRange(0, total - visible);
    m_scrollBar->setPageStep(visible);
    m_scrollBar->setValue(m_scrollOff);
    m_scrollBar->setGeometry(width() - 6, kHeaderH, 5, height() - kHeaderH);
    m_scrollBar->show();
}

void TapeWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    updateScrollBar();
}

void TapeWidget::wheelEvent(QWheelEvent *e)
{
    const int delta = (e->angleDelta().y() > 0) ? -3 : 3;
    const int visible = std::max(1, (height() - kHeaderH) / kRowH);
    const int maxOff  = std::max(0, (int)m_trades.size() - visible);
    m_scrollOff = qBound(0, m_scrollOff + delta, maxOff);
    m_scrollBar->setValue(m_scrollOff);
    update();
    e->accept();
}

void TapeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();
    p.fillRect(rect(), QColor(0x08, 0x0c, 0x12));

    // ── Header ────────────────────────────────────────────────────────────────
    p.fillRect(0, 0, w, kHeaderH, QColor(0x05, 0x08, 0x10));
    p.setPen(QColor(0x14, 0x1e, 0x2e));
    p.drawLine(0, kHeaderH - 1, w, kHeaderH - 1);

    {
        QFont tf; tf.setFamily("Segoe UI"); tf.setPointSize(10);
        tf.setWeight(QFont::Bold); tf.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
        p.setFont(tf);
        p.setPen(QColor(0x48, 0x62, 0x80));
        p.drawText(QRect(12, 4, w - 24, 20), Qt::AlignLeft | Qt::AlignVCenter, "TIME \xe2\x80\x93 SALES");
    }

    // Column headers
    const int sbW    = m_scrollBar->isVisible() ? 8 : 0;
    const int colW   = w - sbW;
    const int timeX  = 10;
    const int priceX = timeX + 72;
    const int sizeX  = priceX + 90;

    {
        QFont cf; cf.setFamily("Consolas"); cf.setPointSize(10);
        p.setFont(cf);
        p.setPen(QColor(0x42, 0x5e, 0x7a));
        p.drawText(QRect(timeX,  22, 68,  14), Qt::AlignLeft  | Qt::AlignVCenter, "TIME");
        p.drawText(QRect(priceX, 22, 86,  14), Qt::AlignRight | Qt::AlignVCenter, "PRICE");
        p.drawText(QRect(sizeX,  22, colW - sizeX - 4, 14), Qt::AlignRight | Qt::AlignVCenter, "\xc2\xb1SIZE");
    }

    if (m_trades.empty()) {
        paintSkeleton(p);
        return;
    }

    // ── Trade rows ────────────────────────────────────────────────────────────
    QFont rowFont; rowFont.setFamily("Consolas"); rowFont.setPointSize(12);
    p.setFont(rowFont);

    const int visible = (h - kHeaderH) / kRowH;
    const int start   = m_scrollOff;
    const int end     = std::min(start + visible, (int)m_trades.size());

    // Color table: [side=0 sell, side=1 buy][tier 0..4]
    // Tiers 1-5 map to indices 0-4
    static const QColor kBuy[5] = {
        QColor(0x00, 0x60, 0x38),   // tier1: very dim
        QColor(0x00, 0x88, 0x4c),   // tier2: dim
        QColor(0x00, 0xb8, 0x65),   // tier3: normal
        QColor(0x00, 0xe0, 0x78),   // tier4: bright
        QColor(0x40, 0xff, 0xa0),   // tier5: max
    };
    static const QColor kSell[5] = {
        QColor(0x60, 0x18, 0x18),   // tier1
        QColor(0x90, 0x22, 0x22),   // tier2
        QColor(0xc0, 0x30, 0x30),   // tier3
        QColor(0xe8, 0x44, 0x44),   // tier4
        QColor(0xff, 0x70, 0x70),   // tier5
    };

    for (int i = start; i < end; ++i) {
        const TradeRow &tr  = m_trades[i];
        const int       row = i - start;
        const int       y   = kHeaderH + row * kRowH;
        const int       ti  = qBound(0, tr.tier - 1, 4);

        const QColor &col = tr.isBuy ? kBuy[ti] : kSell[ti];

        // Whale row: faint background tint
        if (tr.tier == 5) {
            p.fillRect(1, y, colW - 2, kRowH - 1,
                       tr.isBuy ? QColor(0x00, 0x28, 0x16, 90)
                                : QColor(0x28, 0x06, 0x06, 90));
        }

        // Left accent bar (2px, colored)
        p.fillRect(0, y + 1, 2, kRowH - 2,
                   tr.isBuy ? kBuy[ti].darker(150) : kSell[ti].darker(150));

        p.setPen(col);

        const int dec = priceDec(tr.price);
        const QString timeStr  = QDateTime::fromMSecsSinceEpoch(tr.timestampMs).toString("HH:mm:ss");
        const QString priceStr = QString::number(tr.price, 'f', dec);
        const QString sizeStr  = (tr.isBuy ? "+" : "\xe2\x88\x92") + QString::number(tr.size, 'f', 4);

        const int textY = y + (kRowH - 1) / 2;

        p.drawText(QRect(timeX,  y, 68, kRowH), Qt::AlignLeft  | Qt::AlignVCenter, timeStr);
        p.drawText(QRect(priceX, y, 86, kRowH), Qt::AlignRight | Qt::AlignVCenter, priceStr);
        p.drawText(QRect(sizeX,  y, colW - sizeX - 4, kRowH),
                   Qt::AlignRight | Qt::AlignVCenter, sizeStr);

        // Subtle row separator
        p.setPen(QColor(0x0c, 0x12, 0x1a));
        p.drawLine(0, y + kRowH - 1, colW, y + kRowH - 1);

        Q_UNUSED(textY)
    }
}

// ── Skeleton (no data) ────────────────────────────────────────────────────────
void TapeWidget::paintSkeleton(QPainter &p)
{
    const int w = width(), h = height();
    // Column positions match live layout (scrollbar hidden when empty)
    const int timeX = 10, priceX = 82, sizeX = 172;
    const int nRows = std::min(20, (h - kHeaderH) / kRowH);

    static const int kPW[] = {58, 48, 72, 44, 80, 52, 66, 40, 76, 56, 62, 46, 78, 50, 68};
    static const int kSW[] = {42, 30, 58, 36, 54, 24, 50, 34, 64, 40, 46, 28, 56, 38, 60};

    for (int i = 0; i < nRows; ++i) {
        const int y   = kHeaderH + i * kRowH;
        const bool buy = (i % 3 != 2);
        p.fillRect(0, y, w, kRowH - 1,
                   buy ? QColor(0x05, 0x0e, 0x0a) : QColor(0x0e, 0x05, 0x05));
        // Time ghost
        p.fillRect(timeX, y + 4, 54, 10, QColor(0x0c, 0x14, 0x20));
        // Price ghost (tinted buy/sell)
        const int pw = kPW[i % 15] * 68 / 100;
        p.fillRect(priceX + (68 - pw), y + 4, pw, 10,
                   buy ? QColor(0x08, 0x18, 0x10) : QColor(0x18, 0x08, 0x08));
        // Size ghost
        const int sw = kSW[i % 15] * 50 / 100;
        p.fillRect(sizeX, y + 4, sw, 10, QColor(0x0c, 0x14, 0x20));
    }

    // Shimmer sweep
    const double ph = ShimmerTimer::phase();
    const double cx = ph * (w + 240.0) - 120.0;
    QLinearGradient g(cx - 120, 0, cx + 120, 0);
    g.setColorAt(0.0, Qt::transparent);
    g.setColorAt(0.5, QColor(0x1e, 0x30, 0x52, 50));
    g.setColorAt(1.0, Qt::transparent);
    p.fillRect(QRect(0, kHeaderH, w, h - kHeaderH), g);
}
