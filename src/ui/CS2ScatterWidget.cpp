#include "CS2ScatterWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

CS2ScatterWidget::CS2ScatterWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setStyleSheet("background:#080c12;");
}

void CS2ScatterWidget::setListings(const QVector<CS2Listing> &listings)
{
    m_listings = listings;
    m_hoverIdx = -1;
    update();
}

void CS2ScatterWidget::clear()
{
    m_listings.clear();
    m_hoverIdx = -1;
    update();
}

QColor CS2ScatterWidget::wearColor(const QString &wear)
{
    if (wear == "FN") return QColor(0x44, 0x99, 0xff);   // blue
    if (wear == "MW") return QColor(0x22, 0xcc, 0x88);   // green
    if (wear == "FT") return QColor(0xf0, 0xc0, 0x30);   // yellow
    if (wear == "WW") return QColor(0xe0, 0x80, 0x20);   // orange
    return QColor(0xe0, 0x45, 0x45);                       // BS = red
}

QPointF CS2ScatterWidget::toPixel(double floatVal, double price,
                                   double minF, double maxF,
                                   double minP, double maxP) const
{
    const double plotW = width()  - kPadL - kPadR;
    const double plotH = height() - kPadT - kPadB;
    const double rangeF = std::max(maxF - minF, 0.001);
    const double rangeP = std::max(maxP - minP, 0.01);
    const double px = kPadL + (floatVal - minF) / rangeF * plotW;
    const double py = kPadT + plotH - (price - minP) / rangeP * plotH;
    return {px, py};
}

int CS2ScatterWidget::hitTest(const QPointF &pos,
                               double minF, double maxF,
                               double minP, double maxP) const
{
    int closest = -1;
    double bestDist2 = (kDotR * 3.0) * (kDotR * 3.0);
    for (int i = 0; i < m_listings.size(); ++i) {
        const QPointF pt = toPixel(m_listings[i].floatValue, m_listings[i].price,
                                   minF, maxF, minP, maxP);
        const double dx = pos.x() - pt.x();
        const double dy = pos.y() - pt.y();
        const double d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) { bestDist2 = d2; closest = i; }
    }
    return closest;
}

void CS2ScatterWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect();
    p.fillRect(r, QColor(0x08, 0x0c, 0x12));

    // Title
    QFont titleFont;
    titleFont.setFamily("Segoe UI");
    titleFont.setPointSize(8);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(QColor(0x48, 0x64, 0x82));
    const int n = m_listings.size();
    p.drawText(QRect(kPadL, 4, r.width() - kPadL - kPadR, 20),
               Qt::AlignLeft | Qt::AlignVCenter,
               n > 0 ? QString("FLOAT vs PRICE  \xe2\x80\x94  %1 listings").arg(n)
                     : QString("FLOAT vs PRICE"));

    if (n == 0) {
        p.setPen(QColor(0x48, 0x60, 0x7e));
        QFont msgFont;
        msgFont.setFamily("Segoe UI");
        msgFont.setPointSize(11);
        p.setFont(msgFont);
        p.drawText(r, Qt::AlignCenter, "No listings loaded");
        return;
    }

    // Compute ranges with 8% padding
    double minF = m_listings[0].floatValue, maxF = minF;
    double minP = m_listings[0].price,      maxP = minP;
    for (const auto &l : m_listings) {
        minF = std::min(minF, l.floatValue);
        maxF = std::max(maxF, l.floatValue);
        minP = std::min(minP, l.price);
        maxP = std::max(maxP, l.price);
    }
    const double padF = std::max((maxF - minF) * 0.08, 0.005);
    const double padP = std::max((maxP - minP) * 0.08, maxP * 0.02);
    minF -= padF; maxF += padF;
    minP -= padP; maxP += padP;

    // Plot area
    const QRect plotRect(kPadL, kPadT, r.width() - kPadL - kPadR, r.height() - kPadT - kPadB);

    // Grid lines (4 horizontal, 4 vertical)
    QPen gridPen(QColor(0x10, 0x18, 0x24));
    gridPen.setWidth(1);
    p.setPen(gridPen);

    QFont axFont;
    axFont.setFamily("Consolas");
    axFont.setPointSize(7);
    p.setFont(axFont);

    const int kGridLines = 4;
    for (int i = 0; i <= kGridLines; ++i) {
        // Horizontal (price)
        const double pVal = minP + (maxP - minP) * i / kGridLines;
        const int yPx = kPadT + plotRect.height() - static_cast<int>((pVal - minP) / (maxP - minP) * plotRect.height());
        p.setPen(gridPen);
        p.drawLine(kPadL, yPx, r.width() - kPadR, yPx);
        p.setPen(QColor(0x30, 0x48, 0x60));
        p.drawText(QRect(0, yPx - 9, kPadL - 4, 18), Qt::AlignRight | Qt::AlignVCenter,
                   QString("$%1").arg(pVal, 0, 'f', pVal >= 100.0 ? 0 : 2));

        // Vertical (float)
        const double fVal = minF + (maxF - minF) * i / kGridLines;
        const int xPx = kPadL + static_cast<int>((fVal - minF) / (maxF - minF) * plotRect.width());
        p.setPen(gridPen);
        p.drawLine(xPx, kPadT, xPx, kPadT + plotRect.height());
        p.setPen(QColor(0x30, 0x48, 0x60));
        p.drawText(QRect(xPx - 20, kPadT + plotRect.height() + 4, 40, 16),
                   Qt::AlignCenter, QString::number(fVal, 'f', 3));
    }

    // Wear-tier axis label
    p.setPen(QColor(0x48, 0x60, 0x7e));
    p.drawText(QRect(kPadL, kPadT + plotRect.height() + 18, plotRect.width(), 14),
               Qt::AlignCenter, "float value");
    // Rotate price axis label
    p.save();
    p.translate(10, kPadT + plotRect.height() / 2);
    p.rotate(-90);
    p.drawText(QRect(-40, 0, 80, 14), Qt::AlignCenter, "price (USD)");
    p.restore();

    // Wear-tier legend (top-right)
    struct WearInfo { const char *label; QString wear; };
    const WearInfo kWears[] = {{"FN","FN"},{"MW","MW"},{"FT","FT"},{"WW","WW"},{"BS","BS"}};
    int lx = r.width() - kPadR - 5;
    for (int i = 4; i >= 0; --i) {
        const QColor c = wearColor(kWears[i].wear);
        p.setFont(axFont);
        const int tw = QFontMetrics(axFont).horizontalAdvance(kWears[i].label);
        lx -= tw;
        p.setPen(c);
        p.drawText(lx, kPadT - 4, kWears[i].label);
        lx -= 4;
        p.fillRect(lx - 8, kPadT - 12, 8, 8, c);
        lx -= 12;
    }

    // Draw dots
    for (int i = 0; i < m_listings.size(); ++i) {
        const auto &l = m_listings[i];
        const QPointF pt = toPixel(l.floatValue, l.price, minF, maxF, minP, maxP);
        const QColor col = wearColor(l.wear);

        if (i == m_hoverIdx) {
            // Highlight ring
            p.setPen(QPen(col, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(pt, kDotR + 3.0, kDotR + 3.0);
        }

        QColor fill = col;
        fill.setAlpha(i == m_hoverIdx ? 230 : 160);
        p.setBrush(fill);
        p.setPen(QPen(col.lighter(140), 0.8));
        p.drawEllipse(pt, kDotR, kDotR);
    }

    // Hover tooltip
    if (m_hoverIdx >= 0 && m_hoverIdx < m_listings.size()) {
        const auto &l = m_listings[m_hoverIdx];
        const QPointF pt = toPixel(l.floatValue, l.price, minF, maxF, minP, maxP);
        const QString tip = QString("$%1  |  Float: %2  |  %3")
            .arg(l.price, 0, 'f', 2)
            .arg(l.floatValue, 0, 'f', 6)
            .arg(l.wear);

        QFont tipFont;
        tipFont.setFamily("Consolas");
        tipFont.setPointSize(8);
        p.setFont(tipFont);
        const QFontMetrics fm(tipFont);
        const int tw = fm.horizontalAdvance(tip);
        const int th = fm.height();
        const int tx = std::clamp(static_cast<int>(pt.x()) - tw / 2, 4, r.width() - tw - 4);
        const int ty = static_cast<int>(pt.y()) - th - 10;
        const QRect tipRect(tx - 6, ty - 2, tw + 12, th + 6);

        p.fillRect(tipRect, QColor(0x0c, 0x14, 0x20, 220));
        p.setPen(QPen(wearColor(l.wear).darker(120), 1));
        p.drawRect(tipRect);
        p.setPen(QColor(0xc0, 0xd0, 0xe0));
        p.drawText(tx, ty + th - 2, tip);
    }
}

void CS2ScatterWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_listings.isEmpty()) return;

    double minF = m_listings[0].floatValue, maxF = minF;
    double minP = m_listings[0].price,      maxP = minP;
    for (const auto &l : m_listings) {
        minF = std::min(minF, l.floatValue); maxF = std::max(maxF, l.floatValue);
        minP = std::min(minP, l.price);      maxP = std::max(maxP, l.price);
    }
    const double padF = std::max((maxF - minF) * 0.08, 0.005);
    const double padP = std::max((maxP - minP) * 0.08, maxP * 0.02);
    minF -= padF; maxF += padF; minP -= padP; maxP += padP;

    const int newHover = hitTest(e->position(), minF, maxF, minP, maxP);
    if (newHover != m_hoverIdx) {
        m_hoverIdx = newHover;
        setCursor(m_hoverIdx >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void CS2ScatterWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_hoverIdx < 0) return;
    const QString id = m_listings[m_hoverIdx].listingId;
    if (!id.isEmpty())
        QDesktopServices::openUrl(QUrl("https://csfloat.com/item/" + id));
}

void CS2ScatterWidget::leaveEvent(QEvent *)
{
    m_hoverIdx = -1;
    update();
}
