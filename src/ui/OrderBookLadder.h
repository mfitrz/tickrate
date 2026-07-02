#pragma once

#include <QWidget>
#include <QMap>
#include <deque>
#include "core/Types.h"

class OrderBookLadder : public QWidget
{
    Q_OBJECT

public:
    explicit OrderBookLadder(QWidget *parent = nullptr);
    void setSnapshot(const BookSnapshot &snap);
    void addTrade(const TradeInfo &trade);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    struct HitLevel { bool isBuy; qint64 ts; };

    BookSnapshot           m_snap;
    std::deque<double>     m_maxQtyHistory;
    double                 m_stableMax = 1.0;
    QMap<double, HitLevel> m_recentHits;

    static constexpr int    kMaxQtyHistory = 40;
    static constexpr qint64 kFlashMs       = 1500;

    bool m_skelActive = true;
    void paintSkeleton(QPainter &p);
};
