#include "OrderBook.h"
#include <algorithm>

void OrderBook::applyUpdate(const OrderBookUpdate &update)
{
    for (const auto &level : update.bids) {
        if (level.quantity == 0.0)
            m_bids.erase(level.price);
        else
            m_bids[level.price] = level.quantity;
    }

    for (const auto &level : update.asks) {
        if (level.quantity == 0.0)
            m_asks.erase(level.price);
        else
            m_asks[level.price] = level.quantity;
    }
}

BookSnapshot OrderBook::snapshot(int depth) const
{
    BookSnapshot snap;

    int count = 0;
    for (const auto &[price, qty] : m_bids) {
        if (count++ >= depth) break;
        snap.bids.push_back({price, qty});
    }

    count = 0;
    for (const auto &[price, qty] : m_asks) {
        if (count++ >= depth) break;
        snap.asks.push_back({price, qty});
    }

    if (!snap.bids.empty()) snap.bestBid = snap.bids.front().price;
    if (!snap.asks.empty()) snap.bestAsk = snap.asks.front().price;

    const bool hasBid = snap.bestBid > 0.0;
    const bool hasAsk = snap.bestAsk > 0.0;

    snap.spread   = (hasBid && hasAsk) ? snap.bestAsk - snap.bestBid : 0.0;
    snap.midPrice = (hasBid && hasAsk) ? (snap.bestBid + snap.bestAsk) / 2.0
                  : hasAsk             ? snap.bestAsk
                  : snap.bestBid;

    return snap;
}

void OrderBook::clear()
{
    m_bids.clear();
    m_asks.clear();
}
