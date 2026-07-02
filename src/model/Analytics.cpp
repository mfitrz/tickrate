#include "Analytics.h"
#include <algorithm>
#include <numeric>

void Analytics::update(const BookSnapshot &snap, qint64 rawLatencyMs)
{
    auto vol = [](const auto &levels) {
        return std::accumulate(levels.begin(), levels.end(), 0.0,
            [](double s, const PriceLevel &l) { return s + l.quantity; });
    };
    double bidVol = vol(snap.bids);
    double askVol = vol(snap.asks);
    double total  = bidVol + askVol;

    m_rawLatency.push_back(rawLatencyMs);
    if (static_cast<int>(m_rawLatency.size()) > kLatencyWindow)
        m_rawLatency.pop_front();

    // Clock-skew-adjusted latency:
    // The rolling minimum approximates the clock offset (local - server).
    // Subtracting it leaves only genuine extra delay above the fastest observed sample.
    qint64 minRaw = *std::min_element(m_rawLatency.begin(), m_rawLatency.end());
    qint64 adjusted = rawLatencyMs - minRaw;

    m_latest.bidVolume  = bidVol;
    m_latest.askVolume  = askVol;
    m_latest.spread     = snap.spread;
    m_latest.midPrice   = snap.midPrice;
    m_latest.latencyMs  = adjusted;
    m_latest.orderFlowImbalance = (total > 0.0) ? (bidVol - askVol) / total : 0.0;

    // Spread in basis points (1 bps = 0.01%)
    m_latest.spreadBps = (snap.midPrice > 0.0)
                         ? snap.spread / snap.midPrice * 10000.0 : 0.0;

    // Top-10 level book imbalance: bidQty / (bidQty + askQty)
    // Values above 0.5 = bid-heavy (buying pressure), below = ask-heavy
    {
        constexpr int kTop = 10;
        double bidQ = 0.0, askQ = 0.0;
        for (int i = 0; i < kTop && i < static_cast<int>(snap.bids.size()); ++i)
            bidQ += snap.bids[i].quantity;
        for (int i = 0; i < kTop && i < static_cast<int>(snap.asks.size()); ++i)
            askQ += snap.asks[i].quantity;
        const double tot = bidQ + askQ;
        m_latest.bookImbalance = (tot > 0.0) ? bidQ / tot : 0.5;
    }
}

void Analytics::addTrade(double delta)
{
    m_latest.cumulativeDelta += delta;
}

void Analytics::resetDelta()
{
    m_latest.cumulativeDelta = 0.0;
}

AnalyticsSnapshot Analytics::latest() const
{
    return m_latest;
}
