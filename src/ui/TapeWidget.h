#pragma once

#include <QWidget>
#include <QScrollBar>
#include <QWheelEvent>
#include <deque>
#include "core/Types.h"

class TapeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TapeWidget(QWidget *parent = nullptr);

public slots:
    void addTrade(const TradeInfo &trade);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    static constexpr int kMaxTrades = 500;
    static constexpr int kRowH      = 22;
    static constexpr int kHeaderH   = 50;

    struct TradeRow {
        qint64 timestampMs;
        double price;
        double size;
        bool   isBuy;
        int    tier;   // 1=tiny … 5=whale
    };

    std::deque<TradeRow> m_trades;
    double m_sizeEma    = 0.0;
    int    m_tradeCount = 0;
    int    m_scrollOff  = 0;   // rows scrolled from top (0 = newest at top)

    QScrollBar *m_scrollBar;

    int    priceDec(double price) const;
    void   updateScrollBar();

    bool m_skelActive = true;
    void paintSkeleton(QPainter &p);
};
