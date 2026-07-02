#include "CandleBuilder.h"
#include <QJsonObject>
#include <QDateTime>
#include <algorithm>

std::vector<Candle> buildCandlesFromSales(const QJsonArray &sales, int intervalSec)
{
    if (sales.isEmpty() || intervalSec <= 0) return {};

    struct Sale { qint64 timeMs; double price; };
    std::vector<Sale> sorted;
    sorted.reserve(static_cast<size_t>(sales.size()));

    for (const QJsonValue &v : sales) {
        const QJsonObject s = v.toObject();

        const double price = s["sale_price"].toDouble();
        if (price <= 0.0) continue;

        qint64 timeMs = 0;
        const QJsonValue ts = s["created_at"];
        if (ts.isString()) {
            const QDateTime dt = QDateTime::fromString(ts.toString(), Qt::ISODate);
            timeMs = dt.isValid() ? dt.toMSecsSinceEpoch() : 0;
        } else {
            // Unix timestamp in seconds (integer or double)
            timeMs = static_cast<qint64>(ts.toDouble()) * 1000LL;
        }
        if (timeMs <= 0) continue;

        sorted.push_back({timeMs, price});
    }

    if (sorted.empty()) return {};

    std::sort(sorted.begin(), sorted.end(),
              [](const Sale &a, const Sale &b) { return a.timeMs < b.timeMs; });

    const qint64 binMs = static_cast<qint64>(intervalSec) * 1000LL;
    std::vector<Candle> candles;
    Candle cur;

    for (const Sale &s : sorted) {
        const qint64 binStart = (s.timeMs / binMs) * binMs;

        if (cur.openTimeMs == 0) {
            cur.openTimeMs = binStart;
            cur.open = cur.high = cur.low = cur.close = s.price;
            cur.volume = 1.0;
        } else if (binStart == cur.openTimeMs) {
            cur.high   = std::max(cur.high, s.price);
            cur.low    = std::min(cur.low,  s.price);
            cur.close  = s.price;
            cur.volume += 1.0;
        } else {
            cur.complete = true;
            candles.push_back(cur);
            cur = {};
            cur.openTimeMs = binStart;
            cur.open = cur.high = cur.low = cur.close = s.price;
            cur.volume = 1.0;
        }
    }
    if (cur.openTimeMs != 0) {
        cur.complete = true;
        candles.push_back(cur);
    }

    return candles;
}
