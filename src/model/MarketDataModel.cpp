#include "MarketDataModel.h"

MarketDataModel::MarketDataModel(QObject *parent)
    : QObject(parent)
{}

void MarketDataModel::applyOrderBookUpdate(const OrderBookUpdate &update)
{
    m_book.applyUpdate(update);
    m_snap         = m_book.snapshot(50);
    m_timestampMs  = update.timestampMs;
    m_analytics.update(m_snap, update.latencyMs);
    m_pending      = true;
}

void MarketDataModel::applyTrade(const TradeInfo &trade)
{
    m_analytics.addTrade(trade.isBuy ? trade.size : -trade.size);
    emit tradeArrived(trade);
}

void MarketDataModel::reset()
{
    m_book.clear();
    m_snap        = BookSnapshot{};
    m_timestampMs = 0;
    m_pending     = false;
    m_analytics.resetDelta();
}

void MarketDataModel::flushFrame()
{
    if (!m_pending) return;
    m_pending = false;
    emit frameReady(m_snap, m_timestampMs, m_analytics.latest());
}
