#include "DeltaBarWidget.h"
#include <QPainter>

DeltaBarWidget::DeltaBarWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(160, 38);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void DeltaBarWidget::setValue(double ofi)
{
    m_hasData  = true;
    m_smoothed = 0.88 * m_smoothed + 0.12 * ofi;
    update();
}

void DeltaBarWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int w = width();
    const int h = height();

    p.fillRect(rect(), QColor(0x07, 0x0b, 0x14));

    // ── Label row ─────────────────────────────────────────────────────────────
    {
        QFont f; f.setFamily("Segoe UI"); f.setPointSize(7);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        p.setFont(f);

        p.setPen(QColor(0x00, 0xcc, 0x66));
        p.drawText(QRect(0, 1, w / 2, 14), Qt::AlignCenter, "BID");

        p.setPen(QColor(0x48, 0x60, 0x7e));
        p.drawText(QRect(0, 1, w, 14), Qt::AlignCenter, "|");

        p.setPen(QColor(0xcc, 0x40, 0x40));
        p.drawText(QRect(w / 2, 1, w / 2, 14), Qt::AlignCenter, "ASK");
    }

    // ── Split-fill bar ────────────────────────────────────────────────────────
    const int barY = 16;
    const int barH = h - barY - 2;

    if (!m_hasData) {
        // No data yet — draw a dim neutral bar with a subtle "no data" hint
        p.fillRect(0, barY, w / 2, barH, QColor(0x0c, 0x22, 0x14));
        p.fillRect(w / 2, barY, w - w / 2, barH, QColor(0x22, 0x0c, 0x0c));
        p.setPen(QColor(0x07, 0x0b, 0x14));
        p.drawLine(w / 2, barY, w / 2, barY + barH - 1);
        QFont bf; bf.setFamily("Consolas"); bf.setPointSize(7); p.setFont(bf);
        p.setPen(QColor(0x18, 0x28, 0x3c));
        p.drawText(QRect(0, barY, w, barH), Qt::AlignCenter, "\xe2\x80\x94");
        return;
    }

    // bidFrac in [0..1]: 0.5 = balanced, 1.0 = pure bid, 0.0 = pure ask
    const double bidFrac = (1.0 + m_smoothed) / 2.0;
    const int    bidPx   = static_cast<int>(bidFrac * w);
    const int    askPx   = w - bidPx;

    // Always fill both halves so the bar is never empty
    if (bidPx > 0)
        p.fillRect(0,     barY, bidPx, barH, QColor(0x00, 0xa0, 0x55));
    if (askPx > 0)
        p.fillRect(bidPx, barY, askPx, barH, QColor(0xbb, 0x28, 0x28));

    // Divider at the split point
    p.setPen(QColor(0x07, 0x0b, 0x14));
    p.drawLine(bidPx, barY, bidPx, barY + barH - 1);

    // ── Percentage labels inside the bar ─────────────────────────────────────
    QFont bf; bf.setFamily("Consolas"); bf.setPointSize(8); bf.setBold(true);
    p.setFont(bf);

    const int bidPct = qRound(bidFrac * 100);
    const int askPct = 100 - bidPct;

    if (bidPx >= 22) {
        p.setPen(QColor(0xc0, 0xff, 0xd0));
        p.drawText(QRect(2, barY, bidPx - 4, barH),
                   Qt::AlignVCenter | Qt::AlignRight,
                   QString::number(bidPct) + "%");
    }
    if (askPx >= 22) {
        p.setPen(QColor(0xff, 0xc0, 0xc0));
        p.drawText(QRect(bidPx + 2, barY, askPx - 4, barH),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QString::number(askPct) + "%");
    }
}
