#include "BinanceClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QDateTime>
#include <QDebug>
#include <algorithm>

BinanceClient::BinanceClient(QObject *parent)
    : IExchangeClient(parent)
{
    QObject::connect(&m_ws, &WebSocketClient::messageReceived, this, &BinanceClient::onMessage);
    QObject::connect(&m_ws, &WebSocketClient::connected,       this, &BinanceClient::onConnected);
    QObject::connect(&m_ws, &WebSocketClient::disconnected,    this, &BinanceClient::onDisconnected);
    QObject::connect(&m_ws, &WebSocketClient::error,           this, &BinanceClient::onError);

    m_reconnectTimer.setSingleShot(true);
    QObject::connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_symbol.isEmpty()) {
            emit statusChanged("Reconnecting...");
            QString url = QString("wss://stream.binance.us:9443/stream?streams="
                                  "%1@depth@100ms/%1@aggTrade").arg(m_symbol);
            m_ws.connectTo(QUrl(url));
        }
    });
}

void BinanceClient::connect(const QString &symbol)
{
    m_symbol                = symbol.toLower();
    m_lastError.clear();
    m_intentionalDisconnect = false;
    m_reconnectDelay        = 1000;
    m_reconnectTimer.stop();
    emit statusChanged("Connecting...");

    // Combined stream: order book deltas + aggregate trades
    QString url = QString("wss://stream.binance.us:9443/stream?streams="
                          "%1@depth@100ms/%1@aggTrade").arg(m_symbol);
    m_ws.connectTo(QUrl(url));
}

void BinanceClient::disconnect()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    m_ws.disconnect();
}

bool BinanceClient::isConnected() const
{
    return m_ws.isConnected();
}

// ── Depth snapshot ────────────────────────────────────────────────────────────
// The @depth stream sends true deltas; we seed the book with a REST snapshot
// on connect so the book is correct from the first frame.
void BinanceClient::fetchDepthSnapshot()
{
    QUrl url("https://api.binance.us/api/v3/depth");
    QUrlQuery q;
    q.addQueryItem("symbol", m_symbol.toUpper());
    q.addQueryItem("limit",  "50");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[Binance] Depth snapshot failed:" << reply->errorString();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull() || !doc.isObject()) return;

        const QJsonObject obj = doc.object();
        OrderBookUpdate snap;
        snap.timestampMs = QDateTime::currentMSecsSinceEpoch();
        snap.eventTimeMs = snap.timestampMs;

        for (const QJsonValue &v : obj["bids"].toArray()) {
            QJsonArray e = v.toArray();
            if (e.size() >= 2)
                snap.bids.push_back({e[0].toString().toDouble(),
                                     e[1].toString().toDouble()});
        }
        for (const QJsonValue &v : obj["asks"].toArray()) {
            QJsonArray e = v.toArray();
            if (e.size() >= 2)
                snap.asks.push_back({e[0].toString().toDouble(),
                                     e[1].toString().toDouble()});
        }

        emit orderBookUpdated(snap);
    });
}

// ── Historical candles ────────────────────────────────────────────────────────
// Binance.US only exposes klines at 1m and coarser. Sub-minute intervals
// (5s/15s/30s) fall back to fetching 1m candles so the chart always has
// historical context; live fine-grained candles append from the right.
void BinanceClient::fetchHistoricalCandles(const QString &symbol, int intervalSec)
{
    // Clamp to the coarsest available granularity that is <= intervalSec
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

    QUrl url("https://api.binance.us/api/v3/klines");
    QUrlQuery q;
    q.addQueryItem("symbol",   symbol.toUpper());
    q.addQueryItem("interval", iv);
    q.addQueryItem("limit",    "200");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[Binance] Klines failed:" << reply->errorString();
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
            // Field [9] = taker buy base asset volume (available in Binance kline)
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

void BinanceClient::onConnected()
{
    qDebug() << "[Binance] Connected to" << m_symbol;
    m_reconnectDelay = 1000;

    // Seed order book immediately via REST before stream deltas arrive
    fetchDepthSnapshot();

    emit statusChanged("Connected");
    emit connected();
}

void BinanceClient::onDisconnected()
{
    if (!m_intentionalDisconnect && !m_symbol.isEmpty()) {
        const int delaySec = m_reconnectDelay / 1000;
        qDebug() << "[Binance] Disconnected — reconnecting in" << delaySec << "s";
        emit statusChanged(QString("Reconnecting in %1s…").arg(delaySec));
        m_reconnectTimer.start(m_reconnectDelay);
        m_reconnectDelay = std::min(m_reconnectDelay * 2, 30000);
    } else {
        emit statusChanged("Disconnected");
    }

    m_lastError.clear();
    emit disconnected();
}

void BinanceClient::onError(const QString &error)
{
    qDebug() << "[Binance] Error:" << error;
    m_lastError = error;
}

void BinanceClient::onMessage(const QString &message)
{
    qint64 recvTime = QDateTime::currentMSecsSinceEpoch();

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull() || !doc.isObject()) return;

    const QJsonObject msg    = doc.object();
    const QString     stream = msg["stream"].toString();
    const QJsonObject data   = msg["data"].toObject();

    // ── Order book delta ──────────────────────────────────────────────────────
    if (stream.contains("depth")) {
        OrderBookUpdate update;
        update.timestampMs = recvTime;
        update.eventTimeMs = data["E"].toVariant().toLongLong();
        if (update.eventTimeMs == 0) update.eventTimeMs = recvTime;
        update.latencyMs   = recvTime - update.eventTimeMs;

        auto parseLevels = [](const QJsonArray &arr) {
            std::vector<PriceLevel> levels;
            levels.reserve(arr.size());
            for (const QJsonValue &v : arr) {
                QJsonArray e = v.toArray();
                if (e.size() >= 2)
                    levels.push_back({e[0].toString().toDouble(),
                                      e[1].toString().toDouble()});
            }
            return levels;
        };

        update.bids = parseLevels(data["b"].toArray());
        update.asks = parseLevels(data["a"].toArray());
        emit orderBookUpdated(update);
        return;
    }

    // ── Aggregate trades ──────────────────────────────────────────────────────
    if (stream.contains("aggTrade")) {
        TradeInfo t;
        t.price = data["p"].toString().toDouble();
        t.size  = data["q"].toString().toDouble();
        // m=true → buyer is maker → seller was aggressor → SELL trade
        t.isBuy       = !data["m"].toBool();
        t.timestampMs = data["T"].toVariant().toLongLong();
        if (t.timestampMs == 0) t.timestampMs = recvTime;
        emit tradeReceived(t);
        return;
    }
}
