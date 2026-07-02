#include "BybitClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QDateTime>
#include <QDebug>
#include <algorithm>

BybitClient::BybitClient(QObject *parent)
    : IExchangeClient(parent)
{
    QObject::connect(&m_ws, &WebSocketClient::messageReceived, this, &BybitClient::onMessage);
    QObject::connect(&m_ws, &WebSocketClient::connected,       this, &BybitClient::onConnected);
    QObject::connect(&m_ws, &WebSocketClient::disconnected,    this, &BybitClient::onDisconnected);
    QObject::connect(&m_ws, &WebSocketClient::error,           this, &BybitClient::onError);

    // Bybit drops connections silently after ~30s without a ping
    m_pingTimer.setInterval(20000);
    QObject::connect(&m_pingTimer, &QTimer::timeout, this, [this]() {
        m_ws.sendMessage(R"({"op":"ping"})");
    });

    m_reconnectTimer.setSingleShot(true);
    QObject::connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_symbol.isEmpty()) {
            emit statusChanged("Reconnecting...");
            m_ws.connectTo(QUrl("wss://stream.bybit.com/v5/public/spot"));
        }
    });
}

void BybitClient::connect(const QString &symbol)
{
    m_symbol                = symbol.toUpper();
    m_lastError.clear();
    m_intentionalDisconnect = false;
    m_reconnectDelay        = 1000;
    m_reconnectTimer.stop();
    emit statusChanged("Connecting...");
    m_ws.connectTo(QUrl("wss://stream.bybit.com/v5/public/spot"));
}

void BybitClient::disconnect()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    m_pingTimer.stop();
    m_ws.disconnect();
}

bool BybitClient::isConnected() const
{
    return m_ws.isConnected();
}

// ── Historical candles ────────────────────────────────────────────────────────
// Primary: Bybit's own V5 kline endpoint (correct data source, no buy/sell split).
// Fallback: Binance public API (has taker-buy volume in field [9]).
// Bybit returns the list newest-first; we reverse before emitting.
void BybitClient::fetchHistoricalCandles(const QString &symbol, int intervalSec)
{
    // Bybit interval strings: 1,3,5,15,30,60,120,240,360,720,D,W,M
    const int apiInterval = intervalSec < 60 ? 60 : intervalSec;
    const QString iv = [apiInterval]() -> QString {
        switch (apiInterval) {
            case 60:   return "1";
            case 300:  return "5";
            case 900:  return "15";
            case 3600: return "60";
            default:   return {};
        }
    }();
    if (iv.isEmpty()) return;

    QUrl url("https://api.bybit.com/v5/market/kline");
    QUrlQuery q;
    q.addQueryItem("category", "spot");
    q.addQueryItem("symbol",   symbol.toUpper());
    q.addQueryItem("interval", iv);
    q.addQueryItem("limit",    "200");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, symbol, intervalSec]()
    {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[Bybit/klines] Bybit endpoint failed:"
                     << reply->errorString() << "— falling back to Binance.";
            fetchKlinesFrom("https://api.binance.com", symbol, intervalSec, false);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull() || !doc.isObject()) {
            fetchKlinesFrom("https://api.binance.com", symbol, intervalSec, false);
            return;
        }

        const QJsonObject root = doc.object();
        if (root["retCode"].toInt() != 0) {
            qDebug() << "[Bybit/klines] retCode"
                     << root["retCode"].toInt() << "— falling back to Binance.";
            fetchKlinesFrom("https://api.binance.com", symbol, intervalSec, false);
            return;
        }

        const QJsonArray list = root["result"].toObject()["list"].toArray();
        std::vector<Candle> candles;
        candles.reserve(static_cast<size_t>(list.size()));

        // Bybit returns newest-first; iterate reversed so candles are oldest-first
        for (int i = list.size() - 1; i >= 0; --i) {
            const QJsonArray e = list[i].toArray();
            if (e.size() < 6) continue;
            Candle c;
            c.openTimeMs = e[0].toString().toLongLong();
            c.open   = e[1].toString().toDouble();
            c.high   = e[2].toString().toDouble();
            c.low    = e[3].toString().toDouble();
            c.close  = e[4].toString().toDouble();
            c.volume = e[5].toString().toDouble();
            // Bybit klines do not expose buy/sell split; volume bars will
            // render in candle-direction colour instead of the split palette.
            c.complete = true;
            candles.push_back(c);
        }

        if (!candles.empty()) {
            emit historicalCandlesReady(candles);
        } else {
            fetchKlinesFrom("https://api.binance.com", symbol, intervalSec, false);
        }
    });
}

void BybitClient::fetchKlinesFrom(const QString &baseUrl, const QString &symbol,
                                  int intervalSec, bool isFallback)
{
    const int apiInterval = intervalSec < 60 ? 60 : intervalSec;
    const QString iv = [apiInterval]() -> QString {
        switch (apiInterval) {
            case 60:   return "1m";
            case 300:  return "5m";
            case 900:  return "15m";
            case 3600: return "1h";
            default:   return {};
        }
    }();
    if (iv.isEmpty()) return;

    QUrl url(baseUrl + "/api/v3/klines");
    QUrlQuery q;
    q.addQueryItem("symbol",   symbol.toUpper());
    q.addQueryItem("interval", iv);
    q.addQueryItem("limit",    "200");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, symbol, intervalSec, isFallback]()
    {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (!isFallback) {
                qDebug() << "[Bybit/klines] binance.com blocked, retrying binance.us";
                fetchKlinesFrom("https://api.binance.us", symbol, intervalSec, true);
            } else {
                qDebug() << "[Bybit/klines] All endpoints failed:"
                         << reply->errorString() << "— chart builds from live data.";
            }
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull() || !doc.isArray()) return;

        std::vector<Candle> candles;
        const QJsonArray list = doc.array();
        candles.reserve(static_cast<size_t>(list.size()));

        for (const QJsonValue &v : list) {
            const QJsonArray e = v.toArray();
            if (e.size() < 6) continue;
            Candle c;
            c.openTimeMs = e[0].toVariant().toLongLong();
            c.open   = e[1].toString().toDouble();
            c.high   = e[2].toString().toDouble();
            c.low    = e[3].toString().toDouble();
            c.close  = e[4].toString().toDouble();
            c.volume = e[5].toString().toDouble();
            // Binance field [9] = taker buy base asset volume
            if (e.size() >= 10) {
                c.buyVol  = e[9].toString().toDouble();
                c.sellVol = c.volume - c.buyVol;
            }
            c.complete = true;
            candles.push_back(c);
        }

        if (!candles.empty())
            emit historicalCandlesReady(candles);
    });
}

// ── WebSocket handlers ────────────────────────────────────────────────────────

void BybitClient::onConnected()
{
    qDebug() << "[Bybit] Connected, subscribing to" << m_symbol;
    m_reconnectDelay = 1000;
    m_pingTimer.start();

    QJsonObject sub;
    sub["op"] = "subscribe";
    QJsonArray args;
    args.append(QString("orderbook.50.%1").arg(m_symbol));
    args.append(QString("publicTrade.%1").arg(m_symbol));
    sub["args"] = args;
    m_ws.sendMessage(QJsonDocument(sub).toJson(QJsonDocument::Compact));

    emit statusChanged("Connected");
    emit connected();
}

void BybitClient::onDisconnected()
{
    m_pingTimer.stop();

    if (!m_intentionalDisconnect && !m_symbol.isEmpty()) {
        const int delaySec = m_reconnectDelay / 1000;
        qDebug() << "[Bybit] Disconnected — reconnecting in" << delaySec << "s";
        emit statusChanged(QString("Reconnecting in %1s…").arg(delaySec));
        m_reconnectTimer.start(m_reconnectDelay);
        m_reconnectDelay = std::min(m_reconnectDelay * 2, 30000);
    } else {
        emit statusChanged("Disconnected");
    }

    m_lastError.clear();
    emit disconnected();
}

void BybitClient::onError(const QString &error)
{
    qDebug() << "[Bybit] Error:" << error;
    m_lastError = error;
}

void BybitClient::onMessage(const QString &message)
{
    qint64 recvTime = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull() || !doc.isObject()) return;

    QJsonObject obj = doc.object();
    if (obj.contains("op") && obj["op"].toString() == "pong") return;
    if (!obj.contains("topic")) return;

    const QString topic = obj["topic"].toString();
    const QString type  = obj["type"].toString();

    if (topic.startsWith("orderbook")) {
        if (type != "snapshot" && type != "delta") return;

        QJsonObject data = obj["data"].toObject();
        OrderBookUpdate update;
        update.timestampMs = recvTime;
        update.eventTimeMs = obj["ts"].toVariant().toLongLong();
        update.latencyMs   = recvTime - update.eventTimeMs;

        auto parseLevels = [](const QJsonArray &arr) {
            std::vector<PriceLevel> levels;
            levels.reserve(arr.size());
            for (const QJsonValue &v : arr) {
                QJsonArray entry = v.toArray();
                if (entry.size() < 2) continue;
                levels.push_back({entry[0].toString().toDouble(),
                                  entry[1].toString().toDouble()});
            }
            return levels;
        };

        update.bids = parseLevels(data["b"].toArray());
        update.asks = parseLevels(data["a"].toArray());
        emit orderBookUpdated(update);
        return;
    }

    if (topic.startsWith("publicTrade")) {
        QJsonArray trades = obj["data"].toArray();
        for (const QJsonValue &tv : trades) {
            QJsonObject td = tv.toObject();
            TradeInfo t;
            t.price       = td["p"].toString().toDouble();
            t.size        = td["v"].toString().toDouble();
            t.isBuy       = (td["S"].toString() == "Buy");
            t.timestampMs = td["T"].toVariant().toLongLong();
            if (t.timestampMs == 0) t.timestampMs = recvTime;
            emit tradeReceived(t);
        }
        return;
    }
}
