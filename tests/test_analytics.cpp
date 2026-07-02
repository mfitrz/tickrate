#include <QtTest>
#include "model/Analytics.h"

class TestAnalytics : public QObject
{
    Q_OBJECT

private slots:
    void ofi_bidHeavy_isPositive();
    void ofi_askHeavy_isNegative();
    void ofi_balanced_isZero();
    void spread_reflectsSnapshot();
    void spreadBps_calculation();
    void bidAndAskVolume_populated();
    void bookImbalance_bidHeavy();
    void bookImbalance_askHeavy();
    void bookImbalance_equal();
    void bookImbalance_emptyBook_isHalf();
    void latency_skewAdjusted();
    // Cumulative Delta
    void delta_zeroInitially();
    void delta_pureByTrades_positive();
    void delta_pureSellTrades_negative();
    void delta_mixedTrades();
    void delta_resetClearsAccumulator();
};

static BookSnapshot makeSnap(double bidQty, double askQty,
                              double bid = 100.0, double ask = 101.0)
{
    BookSnapshot s;
    s.bids     = {{bid, bidQty}};
    s.asks     = {{ask, askQty}};
    s.bestBid  = bid;
    s.bestAsk  = ask;
    s.spread   = ask - bid;
    s.midPrice = (bid + ask) / 2.0;
    return s;
}

void TestAnalytics::ofi_bidHeavy_isPositive()
{
    Analytics a;
    a.update(makeSnap(10.0, 2.0), 0);
    QVERIFY(a.latest().orderFlowImbalance > 0.0);
}

void TestAnalytics::ofi_askHeavy_isNegative()
{
    Analytics a;
    a.update(makeSnap(2.0, 10.0), 0);
    QVERIFY(a.latest().orderFlowImbalance < 0.0);
}

void TestAnalytics::ofi_balanced_isZero()
{
    Analytics a;
    a.update(makeSnap(5.0, 5.0), 0);
    QCOMPARE(a.latest().orderFlowImbalance, 0.0);
}

void TestAnalytics::spread_reflectsSnapshot()
{
    Analytics a;
    a.update(makeSnap(1.0, 1.0, 100.0, 103.5), 0);
    QCOMPARE(a.latest().spread, 3.5);
}

void TestAnalytics::spreadBps_calculation()
{
    // spread = 2.0 on midPrice 101.0 → 2/101 * 10000 ≈ 198.0 bps
    Analytics a;
    a.update(makeSnap(1.0, 1.0, 100.0, 102.0), 0);
    const double expected = 2.0 / 101.0 * 10000.0;
    QVERIFY(qAbs(a.latest().spreadBps - expected) < 1e-9);
}

void TestAnalytics::bidAndAskVolume_populated()
{
    Analytics a;
    a.update(makeSnap(7.5, 3.0), 0);
    QCOMPARE(a.latest().bidVolume, 7.5);
    QCOMPARE(a.latest().askVolume, 3.0);
}

void TestAnalytics::bookImbalance_bidHeavy()
{
    Analytics a;
    a.update(makeSnap(9.0, 1.0), 0);
    QVERIFY(a.latest().bookImbalance > 0.5);
}

void TestAnalytics::bookImbalance_askHeavy()
{
    Analytics a;
    a.update(makeSnap(1.0, 9.0), 0);
    QVERIFY(a.latest().bookImbalance < 0.5);
}

void TestAnalytics::bookImbalance_equal()
{
    Analytics a;
    a.update(makeSnap(5.0, 5.0), 0);
    QCOMPARE(a.latest().bookImbalance, 0.5);
}

void TestAnalytics::bookImbalance_emptyBook_isHalf()
{
    // No update called — snapshot has empty sides → imbalance defaults to 0.5
    Analytics a;
    QCOMPARE(a.latest().bookImbalance, 0.5);
}

void TestAnalytics::latency_skewAdjusted()
{
    Analytics a;
    for (int i = 0; i < 5; ++i)
        a.update(makeSnap(1, 1), 10);
    a.update(makeSnap(1, 1), 40);
    QCOMPARE(a.latest().latencyMs, qint64(30));
}

// ── Cumulative Delta tests ────────────────────────────────────────────────────

void TestAnalytics::delta_zeroInitially()
{
    Analytics a;
    QCOMPARE(a.latest().cumulativeDelta, 0.0);
}

void TestAnalytics::delta_pureByTrades_positive()
{
    Analytics a;
    a.addTrade(5.0);
    a.addTrade(3.0);
    QCOMPARE(a.latest().cumulativeDelta, 8.0);
}

void TestAnalytics::delta_pureSellTrades_negative()
{
    Analytics a;
    a.addTrade(-4.0);
    QCOMPARE(a.latest().cumulativeDelta, -4.0);
}

void TestAnalytics::delta_mixedTrades()
{
    Analytics a;
    a.addTrade(10.0);   // +10
    a.addTrade(-3.0);   // -3  → net +7
    QCOMPARE(a.latest().cumulativeDelta, 7.0);
}

void TestAnalytics::delta_resetClearsAccumulator()
{
    Analytics a;
    a.addTrade(10.0);
    a.resetDelta();
    QCOMPARE(a.latest().cumulativeDelta, 0.0);
}

QTEST_MAIN(TestAnalytics)
#include "test_analytics.moc"
