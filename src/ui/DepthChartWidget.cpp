#include "DepthChartWidget.h"
#include "util/ShimmerTimer.h"
#include <QPainter>
#include <QLinearGradient>
#include <QPolygonF>
#include <algorithm>
#include <vector>

DepthChartWidget::DepthChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(180);
    setMinimumHeight(200);
    setMouseTracking(true);
    setStyleSheet("background: #080c12;");

    connect(&ShimmerTimer::instance(), &ShimmerTimer::tick,
            this, [this] { if (m_skelActive) update(); });
}

void DepthChartWidget::setSnapshot(const BookSnapshot &snap)
{
    m_snap = snap;
    if (!snap.bids.empty() || !snap.asks.empty()) m_skelActive = false;
    QWidget::update();
}

void DepthChartWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_mousePos = e->pos();
    m_mouseIn  = true;
    update();
}

void DepthChartWidget::leaveEvent(QEvent *)
{
    m_mouseIn = false;
    update();
}

void DepthChartWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x08, 0x0c, 0x12));

    const int w = width();
    const int h = height();

    if (m_snap.bids.empty() && m_snap.asks.empty()) {
        paintSkeleton(p);
        return;
    }

    // Clip to nearest kDisplayLevels per side for a readable, focused view
    const int nBids = std::min((int)m_snap.bids.size(), kDisplayLevels);
    const int nAsks = std::min((int)m_snap.asks.size(), kDisplayLevels);

    // Tick size / decimal precision — check whichever side has data
    double minDiff = 1.0;
    for (int i = 1; i < nBids && i < 10; ++i) {
        double d = m_snap.bids[i-1].price - m_snap.bids[i].price;
        if (d > 1e-9) minDiff = std::min(minDiff, d);
    }
    for (int i = 1; i < nAsks && i < 10; ++i) {
        double d = m_snap.asks[i].price - m_snap.asks[i-1].price;
        if (d > 1e-9) minDiff = std::min(minDiff, d);
    }
    const int priceDec = (minDiff < 0.0015) ? 3 : (minDiff < 0.015) ? 2 : (minDiff < 0.15) ? 1 : 0;

    // Cumulative depth
    std::vector<double> bidCum(nBids), askCum(nAsks);
    double s = 0;
    for (int i = 0; i < nBids; ++i) { s += m_snap.bids[i].quantity; bidCum[i] = s; }
    s = 0;
    for (int i = 0; i < nAsks; ++i) { s += m_snap.asks[i].quantity; askCum[i] = s; }

    const double maxCum   = std::max(bidCum.empty() ? 0.0 : bidCum.back(),
                                     askCum.empty() ? 0.0 : askCum.back());
    // When ask-only (CS2), price range spans the ask side; minPrice falls back to best ask.
    const double minPrice  = nBids > 0 ? m_snap.bids[nBids-1].price : m_snap.asks[0].price;
    const double maxPrice  = nAsks > 0 ? m_snap.asks[nAsks-1].price : m_snap.bids[0].price;
    const double priceRange = maxPrice - minPrice;
    if (maxCum == 0 || priceRange == 0) return;

    const int mTop   = 32;
    const int mBot   = 26;
    const int mLeft  = 52;
    const int mRight = 8;
    const int chartW = w - mLeft - mRight;
    const int chartH = h - mTop - mBot;
    const qreal baseY = mTop + chartH;

    auto xOf = [&](double price) -> qreal {
        return mLeft + (price - minPrice) / priceRange * chartW;
    };
    auto yOf = [&](double vol) -> qreal {
        return mTop + chartH * (1.0 - vol / maxCum);
    };

    // ── Y-axis + grid ─────────────────────────────────────────────────────────
    p.setPen(QColor(0x14, 0x1e, 0x2c));
    p.drawLine(QPointF(mLeft, mTop), QPointF(mLeft, baseY));
    {
        QFont lf; lf.setFamily("Consolas"); lf.setPointSize(9);
        p.setFont(lf);
        for (int gi = 1; gi <= 4; ++gi) {
            double vol = maxCum * gi / 4.0;
            qreal  gy  = yOf(vol);
            p.setPen(QPen(QColor(0x14, 0x1e, 0x2c), 1, Qt::DotLine));
            p.drawLine(QPointF(mLeft, gy), QPointF(w - mRight, gy));
            p.setPen(QColor(0x42, 0x5e, 0x7a));
            p.drawLine(QPointF(mLeft - 3, gy), QPointF(mLeft, gy));
            QString vs = (vol < 10) ? QString::number(vol,'f',2)
                       : (vol < 100) ? QString::number(vol,'f',1)
                       : QString::number(qRound(vol));
            p.drawText(QRect(0, (int)gy - 8, mLeft - 5, 16),
                       Qt::AlignRight | Qt::AlignVCenter, vs);
        }
    }

    // ── Bid step polygon (skipped when ask-only, e.g. CS2) ───────────────────
    QPolygonF bidPoly;
    if (nBids > 0) {
        bidPoly << QPointF(xOf(m_snap.bids[0].price), baseY);
        for (int i = 0; i < nBids; ++i) {
            bidPoly << QPointF(xOf(m_snap.bids[i].price), yOf(bidCum[i]));
            if (i + 1 < nBids)
                bidPoly << QPointF(xOf(m_snap.bids[i+1].price), yOf(bidCum[i]));
        }
        bidPoly << QPointF(xOf(m_snap.bids[nBids-1].price), baseY);
    }

    // ── Ask step polygon ──────────────────────────────────────────────────────
    QPolygonF askPoly;
    if (nAsks > 0) {
        askPoly << QPointF(xOf(m_snap.asks[0].price), baseY);
        for (int i = 0; i < nAsks; ++i) {
            askPoly << QPointF(xOf(m_snap.asks[i].price), yOf(askCum[i]));
            if (i + 1 < nAsks)
                askPoly << QPointF(xOf(m_snap.asks[i+1].price), yOf(askCum[i]));
        }
        askPoly << QPointF(xOf(m_snap.asks[nAsks-1].price), baseY);
    }

    // ── Gradient fills ────────────────────────────────────────────────────────
    if (!bidPoly.isEmpty()) {
        QLinearGradient g(0, mTop, 0, baseY);
        g.setColorAt(0.0, QColor(0x00, 0xcc, 0x70, 55));
        g.setColorAt(1.0, QColor(0x00, 0x88, 0x44, 12));
        p.setPen(Qt::NoPen); p.setBrush(g);
        p.drawPolygon(bidPoly);
    }
    if (!askPoly.isEmpty()) {
        QLinearGradient g(0, mTop, 0, baseY);
        g.setColorAt(0.0, QColor(0xe0, 0x40, 0x40, 55));
        g.setColorAt(1.0, QColor(0x90, 0x20, 0x20, 12));
        p.setPen(Qt::NoPen); p.setBrush(g);
        p.drawPolygon(askPoly);
    }

    // ── Outlines ──────────────────────────────────────────────────────────────
    p.setBrush(Qt::NoBrush);
    if (!bidPoly.isEmpty()) {
        p.setPen(QPen(QColor(0x00, 0xcc, 0x70), 1.5));
        p.drawPolyline(bidPoly);
    }
    if (!askPoly.isEmpty()) {
        p.setPen(QPen(QColor(0xe0, 0x48, 0x48), 1.5));
        p.drawPolyline(askPoly);
    }

    // ── Baseline ──────────────────────────────────────────────────────────────
    p.setPen(QColor(0x14, 0x1e, 0x2c));
    p.drawLine(QPointF(mLeft, baseY), QPointF(w - mRight, baseY));

    // ── Mid-price line ────────────────────────────────────────────────────────
    const qreal midX = xOf(m_snap.midPrice);
    p.setPen(QPen(QColor(0xe0, 0xc0, 0x30, 130), 1, Qt::DashLine));
    p.drawLine(QPointF(midX, mTop), QPointF(midX, baseY));
    {
        QFont mf; mf.setFamily("Consolas"); mf.setPointSize(9);
        p.setFont(mf);
        p.setPen(QColor(0xe0, 0xc0, 0x30));
        QString ms = QString("$%1").arg(m_snap.midPrice, 0, 'f', priceDec);
        int lx = (int)midX + 3;
        if (lx + 54 > w - mRight) lx = (int)midX - 57;
        p.drawText(QRect(lx, mTop + 2, 54, 14), Qt::AlignLeft | Qt::AlignVCenter, ms);
    }

    // ── X-axis ────────────────────────────────────────────────────────────────
    {
        QFont lf; lf.setFamily("Consolas"); lf.setPointSize(9);
        p.setFont(lf);
        for (int pi = 0; pi <= 4; ++pi) {
            double price = minPrice + priceRange * pi / 4.0;
            qreal  px    = xOf(price);
            p.setPen(QColor(0x42, 0x5e, 0x7a));
            p.drawLine(QPointF(px, baseY), QPointF(px, baseY + 3));
            p.drawText(QRect((int)px - 28, (int)baseY + 4, 56, mBot - 4),
                       Qt::AlignHCenter | Qt::AlignTop,
                       QString("$%1").arg(price, 0, 'f', priceDec));
        }
    }

    // ── Title ─────────────────────────────────────────────────────────────────
    {
        QFont tf; tf.setFamily("Segoe UI"); tf.setPointSize(10);
        tf.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        p.setFont(tf);
        p.setPen(QColor(0x48, 0x62, 0x80));
        p.drawText(QRect(mLeft, 4, chartW, 22), Qt::AlignHCenter, "DEPTH");
    }

    // ── Crosshair + tooltip ───────────────────────────────────────────────────
    if (m_mouseIn && m_mousePos.x() >= mLeft && m_mousePos.x() <= w - mRight &&
        m_mousePos.y() >= mTop && m_mousePos.y() <= (int)baseY)
    {
        const int mx = m_mousePos.x();
        const int my = m_mousePos.y();

        p.setPen(QPen(QColor(0x50, 0x70, 0x90, 110), 1, Qt::DashLine));
        p.drawLine(mLeft, my, w - mRight, my);
        p.drawLine(mx, mTop, mx, (int)baseY);

        const double hoverPrice = minPrice + (double)(mx - mLeft) / chartW * priceRange;
        const bool   onBid      = hoverPrice < m_snap.midPrice;

        // Cumulative depth at hover price
        double cumAtPrice = 0;
        if (onBid) {
            for (int i = 0; i < nBids; ++i) {
                if (m_snap.bids[i].price >= hoverPrice) cumAtPrice = bidCum[i];
                else break;
            }
        } else {
            for (int i = 0; i < nAsks; ++i) {
                if (m_snap.asks[i].price <= hoverPrice) cumAtPrice = askCum[i];
                else break;
            }
        }

        // Tooltip
        QFont tf; tf.setFamily("Consolas"); tf.setPointSize(9);
        p.setFont(tf);
        const int tipW = 100, tipH = 32;
        int tx = mx + 6;
        if (tx + tipW > w - mRight) tx = mx - tipW - 4;
        int ty = qBound(mTop, my - tipH / 2, (int)baseY - tipH);

        p.fillRect(tx, ty, tipW, tipH, QColor(0x0e, 0x18, 0x28, 220));
        p.setPen(QColor(0x1e, 0x2e, 0x44));
        p.drawRect(tx, ty, tipW - 1, tipH - 1);
        p.setPen(onBid ? QColor(0x3a, 0xee, 0x88) : QColor(0xf0, 0x60, 0x60));
        p.drawText(QRect(tx + 4, ty + 1,  tipW - 6, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString("$%1").arg(hoverPrice, 0, 'f', priceDec));
        p.setPen(QColor(0x50, 0x72, 0x98));
        p.drawText(QRect(tx + 4, ty + 15, tipW - 6, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString("cum %1").arg(cumAtPrice, 0, 'f', 3));
    }
}

// ── Skeleton (no data) ────────────────────────────────────────────────────────
void DepthChartWidget::paintSkeleton(QPainter &p)
{
    const int w = width(), h = height();
    p.setRenderHint(QPainter::Antialiasing, true);

    // Match real chart margins
    const int mTop = 28, mBot = 22, mLeft = 52, mRight = 8;
    const int chartW = w - mLeft - mRight;
    const int chartH = h - mTop - mBot;
    const int baseY  = mTop + chartH;

    // Y-axis line
    p.setPen(QColor(0x12, 0x1c, 0x2c));
    p.drawLine(mLeft, mTop, mLeft, baseY);

    // Ghost Y-axis tick marks and horizontal grid
    for (int gi = 1; gi <= 4; ++gi) {
        const int gy = mTop + chartH * (4 - gi) / 4;
        p.setPen(QPen(QColor(0x0f, 0x18, 0x28), 1, Qt::DotLine));
        p.drawLine(mLeft, gy, w - mRight, gy);
        p.fillRect(0, gy - 5, mLeft - 5, 10, QColor(0x0e, 0x16, 0x22));
    }

    // Skeleton bid stair-step (left half, dim green)
    {
        static const int kBidSteps[] = {10, 22, 38, 58, 80, 100};
        const int nSteps = 5;
        QPolygonF poly;
        poly << QPointF(mLeft, baseY);
        for (int i = 0; i < nSteps; ++i) {
            const qreal x = mLeft + (nSteps - i - 1) * chartW / 2 / nSteps;
            const qreal y = mTop + chartH * (100 - kBidSteps[i]) / 100;
            poly << QPointF(x + chartW / 2 / nSteps, y) << QPointF(x, y);
        }
        poly << QPointF(mLeft, baseY);
        QLinearGradient bidGrad(0, mTop, 0, baseY);
        bidGrad.setColorAt(0, QColor(0x00, 0xaa, 0x55, 40));
        bidGrad.setColorAt(1, QColor(0x00, 0xaa, 0x55, 12));
        p.setBrush(bidGrad); p.setPen(QColor(0x00, 0x88, 0x44, 60));
        p.drawPolygon(poly);
    }

    // Skeleton ask stair-step (right half, dim red)
    {
        static const int kAskSteps[] = {10, 22, 38, 58, 80, 100};
        const int nSteps = 5;
        const int midX   = mLeft + chartW / 2;
        QPolygonF poly;
        poly << QPointF(midX + chartW / 2, baseY);
        for (int i = 0; i < nSteps; ++i) {
            const qreal x = midX + i * chartW / 2 / nSteps;
            const qreal y = mTop + chartH * (100 - kAskSteps[i]) / 100;
            poly << QPointF(x, y) << QPointF(x + chartW / 2 / nSteps, y);
        }
        poly << QPointF(midX + chartW / 2, baseY);
        QLinearGradient askGrad(0, mTop, 0, baseY);
        askGrad.setColorAt(0, QColor(0xcc, 0x30, 0x30, 40));
        askGrad.setColorAt(1, QColor(0xcc, 0x30, 0x30, 12));
        p.setBrush(askGrad); p.setPen(QColor(0xaa, 0x28, 0x28, 60));
        p.drawPolygon(poly);
    }

    // Ghost price axis labels along bottom
    for (int i = 0; i <= 4; ++i)
        p.fillRect(mLeft + i * chartW / 4 - 20, baseY + 4, 40, 10, QColor(0x0e, 0x16, 0x22));

    // Shimmer sweep
    p.setRenderHint(QPainter::Antialiasing, false);
    const double ph = ShimmerTimer::phase();
    const double cx = mLeft + ph * (chartW + 240.0) - 120.0;
    QLinearGradient g(cx - 120, 0, cx + 120, 0);
    g.setColorAt(0.0, Qt::transparent);
    g.setColorAt(0.5, QColor(0x1e, 0x30, 0x52, 50));
    g.setColorAt(1.0, Qt::transparent);
    p.fillRect(QRect(mLeft, mTop, chartW, chartH), g);
}
