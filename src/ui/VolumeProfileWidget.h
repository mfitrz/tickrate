#pragma once

#include <QWidget>
#include <deque>
#include <vector>
#include "core/Types.h"

class VolumeProfileWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VolumeProfileWidget(QWidget *parent = nullptr);
    void addTrade(const TradeInfo &trade);
    void setSnapshot(const BookSnapshot &snap);
    void clear();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    static constexpr int kMaxTrades = 30000;
    static constexpr int kBins      = 60;

    struct Entry { double price; double size; bool isBuy; };
    std::deque<Entry> m_trades;

    double m_rangeMin = 0.0;
    double m_rangeMax = 0.0;
    double m_midPrice = 0.0;

    struct Bin { double buy = 0.0; double sell = 0.0; };
    std::vector<Bin> m_profile;
    bool             m_dirty = true;

    void recompute();

    bool m_skelActive = true;
    void paintSkeleton(QPainter &p);
};
