#pragma once

#include <QObject>
#include "orderbook/OrderBook.h"
#include "model/Analytics.h"

// MarketDataModel owns the order book and analytics engine.
// It is the single source of truth for market data; widgets never hold their
// own raw update structs — they only receive the processed snapshots this
// class emits.
//
// Data flow:
//   Exchange client  →  applyOrderBookUpdate / applyTrade
//   Render timer     →  flushFrame()   (coalesces bursts to ~30 fps)
//   Widgets          ←  frameReady / tradeArrived signals

class MarketDataModel : public QObject
{
    Q_OBJECT

public:
    explicit MarketDataModel(QObject *parent = nullptr);

    // Called from exchange-client signals (any rate)
    void applyOrderBookUpdate(const OrderBookUpdate &update);
    void applyTrade(const TradeInfo &trade);

    // Called on symbol/exchange switch to wipe stale state
    void reset();

    // Called by the 33 ms render timer; emits frameReady() when a new
    // order-book update has arrived since the last flush.
    void flushFrame();

    // Convenience accessor — safe to call at any time
    BookSnapshot      latestSnapshot()  const { return m_snap; }
    AnalyticsSnapshot latestAnalytics() const { return m_analytics.latest(); }

signals:
    // Emitted by flushFrame() when a frame is pending.
    // timestampMs is the exchange event timestamp of the triggering update.
    void frameReady(const BookSnapshot     &snap,
                    qint64                  timestampMs,
                    const AnalyticsSnapshot &analytics);

    // Emitted immediately for every incoming trade (not rate-limited).
    void tradeArrived(const TradeInfo &trade);

private:
    OrderBook    m_book;
    Analytics    m_analytics;
    BookSnapshot m_snap;
    qint64       m_timestampMs = 0;
    bool         m_pending     = false;
};
