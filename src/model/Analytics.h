#pragma once

#include <deque>
#include "core/Types.h"

struct AnalyticsSnapshot {
    double orderFlowImbalance = 0.0;
    double spread             = 0.0;
    double spreadBps          = 0.0;   // spread / mid × 10 000
    double midPrice           = 0.0;
    double bidVolume          = 0.0;
    double askVolume          = 0.0;
    double bookImbalance      = 0.5;   // bid qty / (bid+ask) top-10 levels [0..1]
    double cumulativeDelta    = 0.0;   // running (buyVol - sellVol) since last reset
    qint64 latencyMs          = 0;
};

class Analytics
{
public:
    void update(const BookSnapshot &snap, qint64 latencyMs);
    void addTrade(double delta);   // +buy, -sell; feeds cumulative delta
    void resetDelta();             // call on symbol/session change
    AnalyticsSnapshot latest() const;

private:
    AnalyticsSnapshot  m_latest;
    std::deque<qint64> m_rawLatency;
    static constexpr int kLatencyWindow = 60;
};
