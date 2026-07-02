#include "OpenSkinClient.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QSslError>
#include <QTimer>
#include <QDebug>

static constexpr const char *kBase = "https://api.openskin.dev";

// Compact timestamp for debug output: "HH:mm:ss.zzz"
static QString ts() { return QDateTime::currentDateTime().toString("HH:mm:ss.zzz"); }
static constexpr qint64 kPriceTtlMs   =  5 * 60 * 1000;   //  5 minutes
static constexpr qint64 kHistoryTtlMs = 30 * 60 * 1000;   // 30 minutes

OpenSkinClient::OpenSkinClient(QObject *parent) : QObject(parent) {}

// ── Utility ───────────────────────────────────────────────────────────────────

QString OpenSkinClient::stripWear(const QString &full)
{
    for (int i = 0; i < kWearCount; ++i) {
        const QString sfx = QString(" %1").arg(kWearSuffix[i]);
        if (full.endsWith(sfx))
            return full.left(full.length() - sfx.length());
    }
    return full;
}

int OpenSkinClient::wearIndex(const QString &full)
{
    for (int i = 0; i < kWearCount; ++i) {
        const QString sfx = QString(" %1").arg(kWearSuffix[i]);
        if (full.endsWith(sfx)) return i;
    }
    return -1;
}

// ── Shared JSON parsing helpers ───────────────────────────────────────────────

static OpenSkinPrices parsePricesDoc(const QJsonObject &root)
{
    OpenSkinPrices out;
    out.item = root["item"].toString();

    const QJsonObject prices = root["prices"].toObject();

    auto readMarket = [](const QJsonObject &o, OpenSkinPrices::Market &m) {
        if (o.isEmpty()) return;
        m.valid  = true;
        m.ask    = o["ask"].toDouble();
        m.bid    = o["bid"].toDouble();
        m.median = o["median"].toDouble();
        m.askVol = o.contains("sell_order_count")
            ? o["sell_order_count"].toInt() : o["ask_volume"].toInt();
        m.bidVol = o.contains("buy_order_count")
            ? o["buy_order_count"].toInt() : o["bid_volume"].toInt();
    };

    const QJsonObject steamPricesObj    = prices["steam"].toObject();
    const QJsonObject skinportPricesObj = prices["skinport"].toObject();

    readMarket(steamPricesObj,              out.steam);
    readMarket(skinportPricesObj,           out.skinport);
    readMarket(prices["buff"].toObject(),   out.buff);
    readMarket(prices["youpin"].toObject(), out.youpin);
    readMarket(prices["csfloat"].toObject(), out.csfloat);

    // Steam-specific extra fields
    out.steamVolume24h  = steamPricesObj["volume_24h"].toInt();

    // Skinport-specific extra fields
    out.skinportMean      = skinportPricesObj["mean"].toDouble();
    out.skinportSuggested = skinportPricesObj["suggested_price"].toDouble();
    out.skinportMaxAsk    = skinportPricesObj["max_ask"].toDouble();

    const QJsonObject la = root["lowest_ask"].toObject();
    out.lowestAskMarket = la["marketplace"].toString();
    out.lowestAskPrice  = la["price"].toDouble();

    const QJsonObject metrics = root["metrics"].toObject();
    const QJsonObject stm = metrics["steam"].toObject();
    if (!stm.isEmpty()) {
        const QJsonObject spreadObj   = stm["spread"].toObject();
        const QJsonObject slippageObj = stm["slippage"].toObject();
        out.spreadPct      = spreadObj["percent"].toDouble();
        out.spreadAbs      = spreadObj["absolute"].toDouble();
        out.slippagePct    = slippageObj["percent"].toDouble();
        out.slippageAbs    = slippageObj["absolute"].toDouble();
        out.steamLiquidity = stm["liquidity_score"].toInt();

        const QJsonObject vol = stm["volatility"].toObject();
        out.volatility1d  = vol["1d"].toDouble();
        out.volatility7d  = vol["7d"].toDouble();
        out.volatility30d = vol["30d"].toDouble();
        out.volatility90d = vol["90d"].toDouble();
        out.volatilityAll = vol["all"].toDouble();

        const QJsonObject pi = stm["price_impact"].toObject();
        static const char *kKeys[4]  = {"1","5","10","25"};
        static const int   kUnits[4] = {1, 5, 10, 25};
        for (int i = 0; i < 4; ++i) {
            const QJsonObject lv = pi[kKeys[i]].toObject();
            if (!lv.isEmpty()) {
                out.priceImpact[i].units       = kUnits[i];
                out.priceImpact[i].avgPrice    = lv["avg_price"].toDouble();
                out.priceImpact[i].pctAboveAsk = lv["pct_above_ask"].toDouble();
                out.priceImpact[i].valid       = true;
            }
        }
    }

    const QJsonObject sprt = metrics["skinport"].toObject();
    if (!sprt.isEmpty()) {
        const QJsonObject prem = sprt["premium"].toObject();
        out.skinportPremiumPct = prem["percent"].toDouble();
        out.skinportPremiumAbs = prem["absolute"].toDouble();
        out.skinportLiquidity  = sprt["liquidity_score"].toInt();
        const QJsonObject spVol = sprt["volatility"].toObject();
        out.skinportVol1d  = spVol["1d"].toDouble();
        out.skinportVol7d  = spVol["7d"].toDouble();
        out.skinportVolAll = spVol["all"].toDouble();
    }

    out.updatedAt = QDateTime::fromString(root["updated_at"].toString(), Qt::ISODateWithMs);

    return out;
}

static QVector<OpenSkinHistoryPoint> parseHistoryDoc(const QJsonObject &root)
{
    const QJsonArray data = root["data"].toArray();
    QVector<OpenSkinHistoryPoint> out;
    out.reserve(data.size());
    for (const QJsonValue &v : data) {
        const QJsonObject pt = v.toObject();
        const QDateTime dt = QDateTime::fromString(
            pt["timestamp"].toString(), Qt::ISODateWithMs);
        if (!dt.isValid()) continue;
        OpenSkinHistoryPoint p;
        p.timestampMs = dt.toMSecsSinceEpoch();
        p.price       = pt["price"].toDouble();
        p.volume      = pt["volume"].toInt();
        if (p.price > 0) out.push_back(p);
    }
    return out;
}

// ── Shared infra ──────────────────────────────────────────────────────────────

QNetworkRequest OpenSkinClient::makeRequest(const QUrl &url) const
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) orderbook-visualizer/1.0");
    req.setRawHeader("Accept", "application/json");
    return req;
}

// ── Cancellation ──────────────────────────────────────────────────────────────

void OpenSkinClient::abortPendingPriceRequests()
{
    ++m_priceGen;
    for (auto *r : m_pendingPriceReplies) {
        r->disconnect();
        r->abort();
        r->deleteLater();
    }
    m_pendingPriceReplies.clear();
}

void OpenSkinClient::abortPendingHistoryRequests()
{
    ++m_historyGen;
    for (auto *r : m_pendingHistoryReplies) {
        r->disconnect();
        r->abort();
        r->deleteLater();
    }
    m_pendingHistoryReplies.clear();
}

// ── Single-item fetches ───────────────────────────────────────────────────────

void OpenSkinClient::fetchPrices(const QString &marketHashName)
{
    QUrl url(QString("%1/v1/prices").arg(kBase));
    QUrlQuery q;
    q.addQueryItem("item", marketHashName);
    url.setQuery(q);

    auto *reply = m_nam.get(makeRequest(url));
    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
        [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QString("[OpenSkin] prices: %1").arg(reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull() || !doc.isObject()) return;
        emit pricesReady(parsePricesDoc(doc.object()));
    });
}

void OpenSkinClient::fetchHistory(const QString &marketHashName,
                                   const QString &marketplace,
                                   const QString &interval)
{
    QNetworkRequest req = makeRequest(QUrl(QString("%1/v1/history").arg(kBase)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["item"]        = marketHashName;
    body["marketplace"] = marketplace;
    body["interval"]    = interval;
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    auto *reply = m_nam.post(req, payload);
    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
        [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QString("[OpenSkin] history: %1").arg(reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull() || !doc.isObject()) return;
        emit historyReady(parseHistoryDoc(doc.object()));
    });
}

// ── Batch multi-wear fetches ──────────────────────────────────────────────────

void OpenSkinClient::fetchAllWearPrices(const QString &baseName)
{
    abortPendingPriceRequests();   // cancel any previous batch
    const int myGen = m_priceGen;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    qDebug() << ts() << "[OpenSkin] fetchAllWearPrices baseName=" << baseName;

    for (int i = 0; i < kWearCount; ++i) {
        const QString name = QString("%1 %2").arg(baseName, kWearSuffix[i]);
        const int idx = i;

        // Cache hit?
        if (m_priceCache.contains(name)) {
            const CachedPrices &c = m_priceCache[name];
            if (c.expiresMs > nowMs) {
                qDebug() << ts() << "[OpenSkin] prices CACHE HIT idx=" << idx;
                QTimer::singleShot(0, this, [this, idx, myGen, prices = c.data]() {
                    if (m_priceGen == myGen) emit wearPricesReady(idx, prices);
                });
                continue;
            }
        }

        QUrl url(QString("%1/v1/prices").arg(kBase));
        QUrlQuery q; q.addQueryItem("item", name); url.setQuery(q);
        qDebug() << ts() << "[OpenSkin] GET" << url.toString();

        auto *reply = m_nam.get(makeRequest(url));
        m_pendingPriceReplies.append(reply);

        QObject::connect(reply, &QNetworkReply::sslErrors, reply,
            [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });

        QObject::connect(reply, &QNetworkReply::finished, this,
            [this, reply, idx, name, myGen]() {
                m_pendingPriceReplies.removeOne(reply);
                reply->deleteLater();
                if (m_priceGen != myGen) return;  // stale — a newer batch was started

                const int httpStatus =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (reply->error() != QNetworkReply::NoError || httpStatus >= 400) {
                    qDebug() << ts() << "[OpenSkin] prices ERROR idx=" << idx
                             << "http=" << httpStatus
                             << "err=" << reply->errorString();
                    if (httpStatus == 429)
                        emit error("[OpenSkin] Rate limited (429) — try again in a moment.");
                    emit wearPricesReady(idx, OpenSkinPrices{});
                    return;
                }
                const QByteArray raw = reply->readAll();
                qDebug() << ts() << "[OpenSkin] prices reply idx=" << idx
                         << "http=" << httpStatus << "bytes=" << raw.size();
                const QJsonDocument doc = QJsonDocument::fromJson(raw);
                if (doc.isNull() || !doc.isObject()) {
                    qDebug() << ts() << "[OpenSkin] prices JSON parse failed idx=" << idx;
                    emit wearPricesReady(idx, OpenSkinPrices{});
                    return;
                }
                const OpenSkinPrices prices = parsePricesDoc(doc.object());
                m_priceCache[name] = { prices, QDateTime::currentMSecsSinceEpoch() + kPriceTtlMs };
                qDebug() << ts() << "[OpenSkin] prices cached idx=" << idx << "steam.ask=" << prices.steam.ask;
                emit wearPricesReady(idx, prices);
            });
    }
}

void OpenSkinClient::fetchAllWearHistory(const QString &baseName,
                                          const QString &marketplace,
                                          const QString &interval)
{
    abortPendingHistoryRequests();   // cancel any previous batch — THIS is what prevents the crash
    const int myGen = m_historyGen;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    qDebug() << ts() << "[OpenSkin] fetchAllWearHistory baseName=" << baseName
             << "marketplace=" << marketplace;

    for (int i = 0; i < kWearCount; ++i) {
        const QString name = QString("%1 %2").arg(baseName, kWearSuffix[i]);
        const QString cacheKey = name + "|" + marketplace;
        const int idx = i;

        // Cache hit?
        if (m_historyCache.contains(cacheKey)) {
            const CachedHistory &c = m_historyCache[cacheKey];
            if (c.expiresMs > nowMs) {
                qDebug() << ts() << "[OpenSkin] history CACHE HIT idx=" << idx
                         << "points=" << c.data.size();
                QTimer::singleShot(0, this, [this, idx, myGen, pts = c.data]() {
                    if (m_historyGen == myGen) emit wearHistoryReady(idx, pts);
                });
                continue;
            }
        }

        QNetworkRequest req = makeRequest(QUrl(QString("%1/v1/history").arg(kBase)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject body;
        body["item"]        = name;
        body["marketplace"] = marketplace;
        body["interval"]    = interval;
        const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

        qDebug() << ts() << "[OpenSkin] POST /v1/history idx=" << idx << "item=" << name;

        auto *reply = m_nam.post(req, payload);
        m_pendingHistoryReplies.append(reply);

        QObject::connect(reply, &QNetworkReply::sslErrors, reply,
            [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });

        QObject::connect(reply, &QNetworkReply::finished, this,
            [this, reply, idx, cacheKey, myGen]() {
                m_pendingHistoryReplies.removeOne(reply);
                reply->deleteLater();
                if (m_historyGen != myGen) return;  // stale — a newer batch was started

                const int httpStatus =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                if (reply->error() != QNetworkReply::NoError || httpStatus >= 400) {
                    qDebug() << ts() << "[OpenSkin] history ERROR idx=" << idx
                             << "http=" << httpStatus
                             << "err=" << reply->errorString();
                    if (httpStatus == 429)
                        emit error("[OpenSkin] Rate limited (429) — try again in a moment.");
                    emit wearHistoryReady(idx, {});
                    return;
                }
                const QByteArray raw = reply->readAll();
                qDebug() << ts() << "[OpenSkin] history reply idx=" << idx
                         << "http=" << httpStatus << "bytes=" << raw.size();
                const QJsonDocument doc = QJsonDocument::fromJson(raw);
                if (doc.isNull() || !doc.isObject()) {
                    qDebug() << ts() << "[OpenSkin] history JSON parse failed idx=" << idx;
                    emit wearHistoryReady(idx, {});
                    return;
                }
                const auto pts = parseHistoryDoc(doc.object());
                m_historyCache[cacheKey] = { pts, QDateTime::currentMSecsSinceEpoch() + kHistoryTtlMs };
                qDebug() << ts() << "[OpenSkin] history cached idx=" << idx << "points=" << pts.size();
                emit wearHistoryReady(idx, pts);
            });
    }
}

// ── Single-wear history (for lazy / background loading) ──────────────────────

void OpenSkinClient::fetchWearHistory(const QString &baseName, int wearIdx,
                                       const QString &marketplace,
                                       const QString &interval)
{
    if (wearIdx < 0 || wearIdx >= kWearCount) return;

    const int     myGen    = m_historyGen;
    const qint64  nowMs    = QDateTime::currentMSecsSinceEpoch();
    const QString name     = QString("%1 %2").arg(baseName, kWearSuffix[wearIdx]);
    const QString cacheKey = name + "|" + marketplace;

    qDebug() << ts() << "[OpenSkin] fetchWearHistory idx=" << wearIdx << "item=" << name;

    // Cache hit?
    if (m_historyCache.contains(cacheKey)) {
        const CachedHistory &c = m_historyCache[cacheKey];
        if (c.expiresMs > nowMs) {
            qDebug() << ts() << "[OpenSkin] history CACHE HIT idx=" << wearIdx;
            QTimer::singleShot(0, this, [this, wearIdx, myGen, pts = c.data]() {
                if (m_historyGen == myGen) emit wearHistoryReady(wearIdx, pts);
            });
            return;
        }
    }

    QNetworkRequest req = makeRequest(QUrl(QString("%1/v1/history").arg(kBase)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["item"]        = name;
    body["marketplace"] = marketplace;
    body["interval"]    = interval;
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    auto *reply = m_nam.post(req, payload);
    m_pendingHistoryReplies.append(reply);

    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
        [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });

    QObject::connect(reply, &QNetworkReply::finished, this,
        [this, reply, wearIdx, cacheKey, myGen]() {
            m_pendingHistoryReplies.removeOne(reply);
            reply->deleteLater();
            if (m_historyGen != myGen) return;

            const int httpStatus =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || httpStatus >= 400) {
                qDebug() << ts() << "[OpenSkin] fetchWearHistory ERROR idx=" << wearIdx
                         << "http=" << httpStatus;
                if (httpStatus == 429)
                    emit error("[OpenSkin] Rate limited (429) — slow down.");
                emit wearHistoryReady(wearIdx, {});
                return;
            }

            const QByteArray raw = reply->readAll();
            qDebug() << ts() << "[OpenSkin] fetchWearHistory reply idx=" << wearIdx
                     << "bytes=" << raw.size();
            const QJsonDocument doc = QJsonDocument::fromJson(raw);
            if (doc.isNull() || !doc.isObject()) {
                emit wearHistoryReady(wearIdx, {});
                return;
            }
            const auto pts = parseHistoryDoc(doc.object());
            m_historyCache[cacheKey] = { pts, QDateTime::currentMSecsSinceEpoch() + kHistoryTtlMs };
            qDebug() << ts() << "[OpenSkin] fetchWearHistory cached idx=" << wearIdx
                     << "pts=" << pts.size();
            emit wearHistoryReady(wearIdx, pts);
        });
}

// ── Health ────────────────────────────────────────────────────────────────────

void OpenSkinClient::fetchHealth()
{
    QUrl url(QString("%1/health").arg(kBase));
    auto *reply = m_nam.get(makeRequest(url));
    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
        [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << ts() << "[OpenSkin] health ERROR:" << reply->errorString();
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull() || !doc.isObject()) return;

        const QJsonObject root = doc.object();
        OpenSkinHealth h;
        h.valid         = true;
        h.overallStatus = root["status"].toString();

        const QJsonObject stats   = root["stats"].toObject();
        h.totalItems = stats["items"].toInt();

        auto readSrc = [](const QJsonObject &o, OpenSkinHealth::SourceStatus &s) {
            if (o.isEmpty()) return;
            s.valid     = true;
            s.status    = o["status"].toString();
            s.updatedAt = QDateTime::fromString(o["updated_at"].toString(), Qt::ISODate);
        };

        const QJsonObject sources = root["sources"].toObject();
        readSrc(sources["buff"].toObject(),     h.buff);
        readSrc(sources["csfloat"].toObject(),  h.csfloat);
        readSrc(sources["skinport"].toObject(), h.skinport);
        readSrc(sources["steam"].toObject(),    h.steam);
        readSrc(sources["youpin"].toObject(),   h.youpin);

        qDebug() << ts() << "[OpenSkin] health status=" << h.overallStatus
                 << "items=" << h.totalItems;
        emit healthReady(h);
    });
}

// ── Item search ───────────────────────────────────────────────────────────────

void OpenSkinClient::searchItems(const QString &query)
{
    if (query.length() < 2) return;

    QUrl url(QString("%1/v1/items").arg(kBase));
    QUrlQuery q;
    q.addQueryItem("q", query);
    q.addQueryItem("limit", "20");
    url.setQuery(q);

    auto *reply = m_nam.get(makeRequest(url));
    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
        [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull()) return;

        QJsonArray items;
        if (doc.isObject())
            items = doc.object()["items"].toArray();
        else if (doc.isArray())
            items = doc.array();

        QStringList names;
        names.reserve(items.size());
        for (const QJsonValue &v : items) {
            QString name;
            if (v.isObject())
                name = v.toObject()["market_hash_name"].toString();
            else if (v.isString())
                name = v.toString();
            if (!name.isEmpty()) names << name;
        }
        if (!names.isEmpty())
            emit searchResults(names);
    });
}
