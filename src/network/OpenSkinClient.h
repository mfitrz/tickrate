#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVector>
#include <QString>
#include <QHash>
#include <QList>
#include <QDateTime>
#include "model/CS2Types.h"

struct PriceImpactLevel {
    int    units       = 0;
    double avgPrice    = 0;
    double pctAboveAsk = 0;
    bool   valid       = false;
};

// Prices for one item across all five marketplaces tracked by openskin.dev
struct OpenSkinPrices {
    struct Market {
        double ask    = 0;
        double bid    = 0;
        double median = 0;
        int    askVol = 0;
        int    bidVol = 0;
        bool   valid  = false;
    };

    QString item;
    Market  steam;
    Market  skinport;
    Market  buff;
    Market  youpin;
    Market  csfloat;

    QString lowestAskMarket;
    double  lowestAskPrice = 0;

    // Steam market metrics
    double spreadPct      = 0;
    double spreadAbs      = 0;
    double slippagePct    = 0;
    double slippageAbs    = 0;
    int    steamLiquidity = 0;
    int    steamVolume24h = 0;
    double volatility1d   = 0;
    double volatility7d   = 0;
    double volatility30d  = 0;
    double volatility90d  = 0;
    double volatilityAll  = 0;
    PriceImpactLevel priceImpact[4];  // 1, 5, 10, 25 units on Steam

    // Skinport-specific metrics
    double skinportPremiumPct = 0;   // negative = cheaper than Steam
    double skinportPremiumAbs = 0;
    int    skinportLiquidity  = 0;
    double skinportMean       = 0;
    double skinportSuggested  = 0;
    double skinportMaxAsk     = 0;
    double skinportVol1d      = 0;
    double skinportVol7d      = 0;
    double skinportVolAll     = 0;

    QDateTime updatedAt;
};

// Health status from GET /health
struct OpenSkinHealth {
    struct SourceStatus {
        QString   status;
        QDateTime updatedAt;
        bool      valid = false;
    };

    QString      overallStatus;
    SourceStatus buff;
    SourceStatus csfloat;
    SourceStatus skinport;
    SourceStatus steam;
    SourceStatus youpin;
    int          totalItems = 0;
    bool         valid      = false;
};

struct OpenSkinHistoryPoint {
    qint64 timestampMs = 0;
    double price       = 0;
    int    volume      = 0;
};

class OpenSkinClient : public QObject
{
    Q_OBJECT
public:
    explicit OpenSkinClient(QObject *parent = nullptr);

    // GET /v1/prices — multi-source prices + full metrics for one item
    void fetchPrices(const QString &marketHashName);

    // POST /v1/history — price history for a given marketplace
    void fetchHistory(const QString &marketHashName,
                      const QString &marketplace = "steam",
                      const QString &interval    = "daily");

    // Batch variants — fire one request per wear, emit indexed signals
    void fetchAllWearPrices(const QString &baseName);
    void fetchAllWearHistory(const QString &baseName,
                              const QString &marketplace = "steam",
                              const QString &interval    = "daily");

    // Single-wear history (no abort — for lazy/background loading)
    void fetchWearHistory(const QString &baseName, int wearIdx,
                          const QString &marketplace = "steam",
                          const QString &interval    = "daily");

    // Cancel all in-flight requests (call before switching skin or marketplace)
    void abortPendingPriceRequests();
    void abortPendingHistoryRequests();

    // GET /health — data source status
    void fetchHealth();

    // GET /v1/items — skin name autocomplete search
    void searchItems(const QString &query);

    // Utility: strip wear suffix → base name ("AK-47 | Fire Serpent (FN)" → "AK-47 | Fire Serpent")
    static QString stripWear(const QString &full);
    // Returns wear index [0-4] or -1 if no suffix recognised
    static int wearIndex(const QString &full);

signals:
    void pricesReady(const OpenSkinPrices &prices);
    void historyReady(const QVector<OpenSkinHistoryPoint> &points);
    // Per-wear variants (wearIdx 0-4)
    void wearPricesReady(int wearIdx, const OpenSkinPrices &prices);
    void wearHistoryReady(int wearIdx, const QVector<OpenSkinHistoryPoint> &points);
    void searchResults(const QStringList &names);
    void healthReady(const OpenSkinHealth &health);
    void error(const QString &msg);

private:
    QNetworkRequest makeRequest(const QUrl &url) const;

    QNetworkAccessManager m_nam;

    // In-flight request tracking (for cancellation)
    QList<QNetworkReply*> m_pendingPriceReplies;
    QList<QNetworkReply*> m_pendingHistoryReplies;

    // Generation counters — bumped on every new batch; stale replies are silently dropped
    int m_priceGen   = 0;
    int m_historyGen = 0;

    // In-memory cache
    struct CachedPrices {
        OpenSkinPrices data;
        qint64         expiresMs = 0;
    };
    struct CachedHistory {
        QVector<OpenSkinHistoryPoint> data;
        qint64                        expiresMs = 0;
    };
    QHash<QString, CachedPrices>  m_priceCache;   // key: full item name
    QHash<QString, CachedHistory> m_historyCache;  // key: "item|marketplace"
};
