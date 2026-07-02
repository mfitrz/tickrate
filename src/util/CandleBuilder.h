#pragma once

#include <QJsonArray>
#include <QMetaType>
#include <vector>
#include "core/Types.h"

// Register std::vector<Candle> with Qt's meta-object system so that signals
// carrying this type don't produce "Unknown" type warnings at connect time.
Q_DECLARE_METATYPE(std::vector<Candle>)

// Converts a Skinport-style sales history JSON array into OHLC candles.
//
// Expected object shape per element:
//   { "sale_price": <number>,
//     "created_at": <ISO-8601 string>  |  <Unix timestamp (seconds, int)> }
//
// Sales may arrive in any order; the function sorts by time internally.
// Elements with zero/missing price or unparseable timestamp are silently skipped.
std::vector<Candle> buildCandlesFromSales(const QJsonArray &sales, int intervalSec);
