#pragma once
#include <QObject>
#include <QString>
#include "network/IExchangeClient.h"

// CS2MarketClient is a thin CS2-mode session state machine.
// All actual data is fetched by OpenSkinClient.
class CS2MarketClient : public IExchangeClient
{
    Q_OBJECT
public:
    explicit CS2MarketClient(QObject *parent = nullptr);
    void connect(const QString &symbol) override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }
    void fetchHistoricalCandles(const QString &, int) override {}
    QString exchangeName() const override { return "OpenSkin"; }

private:
    bool    m_connected = false;
    QString m_symbol;
};
