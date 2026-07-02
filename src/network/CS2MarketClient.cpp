#include "CS2MarketClient.h"

CS2MarketClient::CS2MarketClient(QObject *parent)
    : IExchangeClient(parent) {}

void CS2MarketClient::connect(const QString &symbol)
{
    m_symbol    = symbol;
    m_connected = true;
    emit statusChanged("Connected");
    emit connected();
}

void CS2MarketClient::disconnect()
{
    m_connected = false;
    emit statusChanged("Disconnected");
    emit disconnected();
}
