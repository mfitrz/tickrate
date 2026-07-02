#pragma once

#include <vector>
#include <QtTypes>

// Shared domain types used across the exchange, model, and UI layers.
// Nothing in this file depends on any other project header.

struct PriceLevel {
    double price;
    double quantity;
};

struct OrderBookUpdate {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    qint64 timestampMs = 0;
    qint64 eventTimeMs = 0;
    qint64 latencyMs   = 0;
};

struct TradeInfo {
    double  price;
    double  size;
    bool    isBuy;        // true = buyer-initiated
    qint64  timestampMs;
};

struct Candle {
    qint64 openTimeMs = 0;
    double open = 0, high = 0, low = 0, close = 0;
    double buyVol = 0, sellVol = 0;
    double volume = 0;   // total (from REST seed or live trade sum)
    double vwap   = 0;   // VWAP at candle close (session-cumulative)
    bool   complete = false;
};

struct BookSnapshot {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    double bestBid  = 0.0;
    double bestAsk  = 0.0;
    double spread   = 0.0;
    double midPrice = 0.0;
};
