#pragma once

#include <QTimer>
#include <QNetworkAccessManager>
#include "IExchangeClient.h"
#include "WebSocketClient.h"

class BinanceClient : public IExchangeClient
{
    Q_OBJECT

public:
    explicit BinanceClient(QObject *parent = nullptr);

    void    connect(const QString &symbol) override;
    void    disconnect()                   override;
    bool    isConnected()            const override;
    void    fetchHistoricalCandles(const QString &symbol, int intervalSec) override;
    QString exchangeName()           const override { return "Binance.US"; }

private slots:
    void onMessage(const QString &message);
    void onConnected();
    void onDisconnected();
    void onError(const QString &error);

private:
    void fetchDepthSnapshot();

    WebSocketClient       m_ws;
    QNetworkAccessManager m_nam;
    QTimer                m_reconnectTimer;
    QString               m_symbol;          // lower-case
    QString               m_lastError;
    int                   m_reconnectDelay        = 1000;
    bool                  m_intentionalDisconnect = false;
};
