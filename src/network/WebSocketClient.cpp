#include "WebSocketClient.h"
#include <QSslConfiguration>
#include <QDebug>

WebSocketClient::WebSocketClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QWebSocket::connected,           this, &WebSocketClient::onConnected);
    connect(&m_socket, &QWebSocket::disconnected,        this, &WebSocketClient::onDisconnected);
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &WebSocketClient::messageReceived);
    connect(&m_socket, &QWebSocket::errorOccurred,       this, &WebSocketClient::onError);

    m_connectTimer.setSingleShot(true);
    m_connectTimer.setInterval(10000);
    connect(&m_connectTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "[WS] Connect timeout fired";
        emit error("Connection timed out (10 s) — check firewall or internet access");
        m_socket.abort();
    });
}

void WebSocketClient::connectTo(const QUrl &url)
{
    // Ignore SSL cert errors — lets us connect even if the OpenSSL trust store
    // doesn't include the CA chain (common on Windows with bundled OpenSSL).
    m_socket.ignoreSslErrors();

    QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_socket.setSslConfiguration(conf);

    m_connectTimer.start();
    m_socket.open(url);
}

void WebSocketClient::sendMessage(const QString &message)
{
    m_socket.sendTextMessage(message);
}

void WebSocketClient::disconnect()
{
    m_socket.close();
}

bool WebSocketClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

void WebSocketClient::onConnected()
{
    m_connectTimer.stop();
    emit connected();
}

void WebSocketClient::onDisconnected()
{
    m_connectTimer.stop();
    emit disconnected();
}

void WebSocketClient::onError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError)
    m_connectTimer.stop();
    emit error(m_socket.errorString());
}

