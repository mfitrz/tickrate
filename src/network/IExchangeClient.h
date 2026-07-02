#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include "core/Types.h"

// Abstract interface for a live market-data exchange connection.
// Concrete implementations: BybitClient, BinanceClient.
class IExchangeClient : public QObject
{
    Q_OBJECT

public:
    explicit IExchangeClient(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IExchangeClient() = default;

    virtual void connect(const QString &symbol) = 0;
    virtual void disconnect()                   = 0;
    virtual bool isConnected() const            = 0;
    virtual void fetchHistoricalCandles(const QString &symbol, int intervalSec) = 0;
    virtual QString exchangeName() const = 0;

signals:
    void orderBookUpdated(const OrderBookUpdate &update);
    void tradeReceived(const TradeInfo &trade);
    void historicalCandlesReady(std::vector<Candle> candles);
    void connected();
    void disconnected();
    void statusChanged(const QString &status);
};
