#pragma once

#include <QObject>
#include <QWebSocket>
#include <QUrl>
#include <QTimer>

class WebSocketClient : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketClient(QObject *parent = nullptr);
    void connectTo(const QUrl &url);
    void sendMessage(const QString &message);
    void disconnect();
    bool isConnected() const;

signals:
    void messageReceived(const QString &message);
    void connected();
    void disconnected();
    void error(const QString &errorString);

private slots:
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError socketError);

private:
    QWebSocket m_socket;
    QTimer     m_connectTimer;
};
