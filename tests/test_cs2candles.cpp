#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include "util/CandleBuilder.h"

class TestCS2Candles : public QObject
{
    Q_OBJECT

private slots:
    void emptyArrayReturnsNoCandles();
    void zeroPriceIsSkipped();
    void missingTimestampIsSkipped();
    void singleSaleProducesOneCandle();
    void openHighLowCloseAreCorrect();
    void volumeCountsListings();
    void allCandlesAreMarkedComplete();
    void salesBinnedByInterval();
    void newestFirstInputSortedCorrectly();   // Skinport returns newest-first
    void isoStringTimestampParsed();
    void salesAcrossThreeBins();
    void closePriceIsLastInBin();
    void openPriceIsFirstInBin();
};

// ── helpers ──────────────────────────────────────────────────────────────────

// Build a sale object with a Unix timestamp (seconds)
static QJsonObject makeSale(double price, qint64 unixSec)
{
    QJsonObject s;
    s["sale_price"] = price;
    s["created_at"] = static_cast<double>(unixSec);
    return s;
}

// ── tests ─────────────────────────────────────────────────────────────────────

void TestCS2Candles::emptyArrayReturnsNoCandles()
{
    QVERIFY(buildCandlesFromSales(QJsonArray{}, 3600).empty());
}

void TestCS2Candles::zeroPriceIsSkipped()
{
    QJsonArray a;
    a.append(makeSale(0.0, 1000000));
    a.append(makeSale(-5.0, 1000001));
    QVERIFY(buildCandlesFromSales(a, 3600).empty());
}

void TestCS2Candles::missingTimestampIsSkipped()
{
    QJsonObject bad;
    bad["sale_price"] = 12.34;
    // no created_at field → toDouble() returns 0 → skipped
    QJsonArray a;
    a.append(bad);
    QVERIFY(buildCandlesFromSales(a, 3600).empty());
}

void TestCS2Candles::singleSaleProducesOneCandle()
{
    QJsonArray a;
    a.append(makeSale(14.99, 1700000000));
    const auto candles = buildCandlesFromSales(a, 3600);
    QCOMPARE(candles.size(), size_t(1));
}

void TestCS2Candles::openHighLowCloseAreCorrect()
{
    // Three sales in the same 1-hour bin: 10, 15, 12
    const qint64 base = 1700000000;  // already aligned to an hour boundary is fine
    QJsonArray a;
    a.append(makeSale(10.0, base));
    a.append(makeSale(15.0, base + 60));
    a.append(makeSale(12.0, base + 120));

    const auto candles = buildCandlesFromSales(a, 3600);
    QCOMPARE(candles.size(), size_t(1));
    const Candle &c = candles[0];
    QCOMPARE(c.open,  10.0);
    QCOMPARE(c.high,  15.0);
    QCOMPARE(c.low,   10.0);
    QCOMPARE(c.close, 12.0);
}

void TestCS2Candles::volumeCountsListings()
{
    const qint64 base = 1700000000;
    QJsonArray a;
    a.append(makeSale(10.0, base));
    a.append(makeSale(11.0, base + 30));
    a.append(makeSale(10.5, base + 60));

    const auto candles = buildCandlesFromSales(a, 3600);
    QCOMPARE(candles.size(), size_t(1));
    QCOMPARE(candles[0].volume, 3.0);
}

void TestCS2Candles::allCandlesAreMarkedComplete()
{
    // 3 sales in 3 separate 1-hour bins
    QJsonArray a;
    a.append(makeSale(10.0, 1700000000));
    a.append(makeSale(11.0, 1700000000 + 3600));
    a.append(makeSale(12.0, 1700000000 + 7200));

    for (const Candle &c : buildCandlesFromSales(a, 3600))
        QVERIFY(c.complete);
}

void TestCS2Candles::salesBinnedByInterval()
{
    // Two sales in the same 1-min bin, one sale in the next bin
    const qint64 bin0 = 1700000000;
    const qint64 bin1 = bin0 + 60;

    QJsonArray a;
    a.append(makeSale(10.0, bin0));
    a.append(makeSale(11.0, bin0 + 30));
    a.append(makeSale(20.0, bin1));

    const auto candles = buildCandlesFromSales(a, 60);
    QCOMPARE(candles.size(), size_t(2));
    QCOMPARE(candles[0].volume, 2.0);
    QCOMPARE(candles[1].volume, 1.0);
    QCOMPARE(candles[1].open,   20.0);
}

void TestCS2Candles::newestFirstInputSortedCorrectly()
{
    // Skinport returns newest-first: 300, 200, 100 (Unix seconds)
    // Oldest-first after sort → open = price at t=100
    QJsonArray a;
    a.append(makeSale(30.0, 1700000300));   // newest
    a.append(makeSale(20.0, 1700000200));
    a.append(makeSale(10.0, 1700000100));   // oldest

    const auto candles = buildCandlesFromSales(a, 3600);
    QCOMPARE(candles.size(), size_t(1));
    QCOMPARE(candles[0].open,  10.0);   // first in time
    QCOMPARE(candles[0].close, 30.0);   // last in time
}

void TestCS2Candles::isoStringTimestampParsed()
{
    QJsonObject s;
    s["sale_price"] = 55.0;
    s["created_at"] = QString("2023-11-14T22:13:20Z");   // ISO 8601

    QJsonArray a;
    a.append(s);
    const auto candles = buildCandlesFromSales(a, 3600);
    QCOMPARE(candles.size(), size_t(1));
    QCOMPARE(candles[0].open, 55.0);
}

void TestCS2Candles::salesAcrossThreeBins()
{
    const qint64 base = 1700000000;
    QJsonArray a;
    // bin 0 (base .. base+59): 2 sales
    a.append(makeSale(10.0, base));
    a.append(makeSale(12.0, base + 30));
    // bin 1 (base+60 .. base+119): 1 sale
    a.append(makeSale(15.0, base + 60));
    // bin 2 (base+120 .. base+179): 3 sales
    a.append(makeSale(8.0,  base + 120));
    a.append(makeSale(9.0,  base + 140));
    a.append(makeSale(7.0,  base + 150));

    const auto candles = buildCandlesFromSales(a, 60);
    QCOMPARE(candles.size(), size_t(3));
    QCOMPARE(candles[0].volume, 2.0);
    QCOMPARE(candles[1].volume, 1.0);
    QCOMPARE(candles[2].volume, 3.0);
    QCOMPARE(candles[2].high,   9.0);
    QCOMPARE(candles[2].low,    7.0);
}

void TestCS2Candles::closePriceIsLastInBin()
{
    const qint64 base = 1700000000;
    QJsonArray a;
    a.append(makeSale(10.0, base));
    a.append(makeSale(50.0, base + 1));
    a.append(makeSale(25.0, base + 2));   // last → close

    const auto candles = buildCandlesFromSales(a, 3600);
    QCOMPARE(candles[0].close, 25.0);
}

void TestCS2Candles::openPriceIsFirstInBin()
{
    const qint64 base = 1700000000;
    QJsonArray a;
    // Deliberately append in descending order to prove sorting
    a.append(makeSale(99.0, base + 2));
    a.append(makeSale(55.0, base + 1));
    a.append(makeSale(10.0, base));       // earliest → open

    const auto candles = buildCandlesFromSales(a, 3600);
    QCOMPARE(candles[0].open, 10.0);
}

QTEST_MAIN(TestCS2Candles)
#include "test_cs2candles.moc"
