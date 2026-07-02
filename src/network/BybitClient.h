#pragma once

#include <QTimer>
#include <QNetworkAccessManager>
#include "IExchangeClient.h"
#include "WebSocketClient.h"

class BybitClient : public IExchangeClient
{
    Q_OBJECT

public:
    explicit BybitClient(QObject *parent = nullptr);

    void    connect(const QString &symbol) override;
    void    disconnect()                   override;
    bool    isConnected()            const override;
    void    fetchHistoricalCandles(const QString &symbol, int intervalSec) override;
    QString exchangeName()           const override { return "Bybit"; }

private slots:
    void onMessage(const QString &message);
    void onConnected();
    void onDisconnected();
    void onError(const QString &error);

private:
    void fetchKlinesFrom(const QString &baseUrl, const QString &symbol,
                         int intervalSec, bool isFallback);

    WebSocketClient       m_ws;
    QNetworkAccessManager m_nam;
    QTimer                m_pingTimer;
    QTimer                m_reconnectTimer;
    QString               m_symbol;
    QString               m_lastError;
    int                   m_reconnectDelay        = 1000;
    bool                  m_intentionalDisconnect = false;
};
