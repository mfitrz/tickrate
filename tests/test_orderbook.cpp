#include <QtTest>
#include "orderbook/OrderBook.h"

class TestOrderBook : public QObject
{
    Q_OBJECT

private slots:
    void snapshotApplied();
    void midPriceAndSpread();
    void deltaModifiesLevel();
    void deltaZeroQtyRemovesLevel();
    void snapshotDepthCapped();
    void clearEmptiesBook();
    void bidsDescendingAsksAscending();

    void deltaAddsNewLevel();

    // CS2 mode: bid side may be absent
    void asksOnlyMidPriceEqualsBestAsk();
    void asksOnlySpreadIsZero();
    void bidsOnlyMidPriceEqualsBestBid();
    void bothSidesEmptyMidPriceIsZero();
};

static OrderBookUpdate makeSnapshot(
    std::vector<PriceLevel> bids,
    std::vector<PriceLevel> asks)
{
    OrderBookUpdate u;
    u.bids = std::move(bids);
    u.asks = std::move(asks);
    return u;
}

void TestOrderBook::snapshotApplied()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot({{100.0, 1.0}, {99.0, 2.0}},
                                {{101.0, 1.5}, {102.0, 0.5}}));
    auto s = ob.snapshot(10);
    QCOMPARE(s.bids.size(), size_t(2));
    QCOMPARE(s.asks.size(), size_t(2));
    QCOMPARE(s.bids[0].price, 100.0);
    QCOMPARE(s.asks[0].price, 101.0);
}

void TestOrderBook::midPriceAndSpread()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot({{100.0, 1.0}}, {{102.0, 1.0}}));
    auto s = ob.snapshot(5);
    QCOMPARE(s.bestBid,  100.0);
    QCOMPARE(s.bestAsk,  102.0);
    QCOMPARE(s.spread,   2.0);
    QCOMPARE(s.midPrice, 101.0);
}

void TestOrderBook::deltaModifiesLevel()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot({{100.0, 1.0}}, {{101.0, 1.0}}));

    // Delta: change bid qty at 100.0
    OrderBookUpdate delta;
    delta.bids = {{100.0, 5.0}};
    ob.applyUpdate(delta);

    auto s = ob.snapshot(5);
    QCOMPARE(s.bids[0].quantity, 5.0);
}

void TestOrderBook::deltaZeroQtyRemovesLevel()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot({{100.0, 1.0}, {99.0, 2.0}},
                                {{101.0, 1.0}}));

    // Delta: zero quantity removes the 100.0 bid
    OrderBookUpdate delta;
    delta.bids = {{100.0, 0.0}};
    ob.applyUpdate(delta);

    auto s = ob.snapshot(10);
    QCOMPARE(s.bids.size(), size_t(1));
    QCOMPARE(s.bids[0].price, 99.0);
}

void TestOrderBook::snapshotDepthCapped()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot(
        {{100.0,1},{99.0,1},{98.0,1},{97.0,1},{96.0,1}},
        {{101.0,1},{102.0,1},{103.0,1}}));
    auto s = ob.snapshot(2);
    QCOMPARE(s.bids.size(), size_t(2));
    QCOMPARE(s.asks.size(), size_t(2));
}

void TestOrderBook::clearEmptiesBook()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot({{100.0, 1.0}}, {{101.0, 1.0}}));
    ob.clear();
    auto s = ob.snapshot(10);
    QVERIFY(s.bids.empty());
    QVERIFY(s.asks.empty());
    QCOMPARE(s.midPrice, 0.0);
}

void TestOrderBook::bidsDescendingAsksAscending()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot(
        {{98.0,1},{100.0,1},{99.0,1}},
        {{103.0,1},{101.0,1},{102.0,1}}));
    auto s = ob.snapshot(10);
    QCOMPARE(s.bids[0].price, 100.0);
    QCOMPARE(s.bids[1].price, 99.0);
    QCOMPARE(s.asks[0].price, 101.0);
    QCOMPARE(s.asks[1].price, 102.0);
}

void TestOrderBook::deltaAddsNewLevel()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot({{100.0, 1.0}}, {{101.0, 1.0}}));

    // Delta introduces a previously absent bid at 99.0
    OrderBookUpdate delta;
    delta.bids = {{99.0, 3.5}};
    ob.applyUpdate(delta);

    auto s = ob.snapshot(10);
    QCOMPARE(s.bids.size(), size_t(2));
    QCOMPARE(s.bids[1].price,    99.0);
    QCOMPARE(s.bids[1].quantity, 3.5);
}

// ── CS2 mode: one-sided book ──────────────────────────────────────────────────

void TestOrderBook::asksOnlyMidPriceEqualsBestAsk()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot({}, {{105.0, 3.0}, {106.0, 1.0}}));
    auto s = ob.snapshot(10);
    QCOMPARE(s.bestBid,  0.0);
    QCOMPARE(s.bestAsk,  105.0);
    // midPrice falls back to bestAsk when there are no bids
    QCOMPARE(s.midPrice, 105.0);
}

void TestOrderBook::asksOnlySpreadIsZero()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot({}, {{200.0, 1.0}}));
    auto s = ob.snapshot(10);
    // spread is undefined without both sides — reported as 0
    QCOMPARE(s.spread, 0.0);
}

void TestOrderBook::bidsOnlyMidPriceEqualsBestBid()
{
    OrderBook ob;
    ob.applyUpdate(makeSnapshot({{98.0, 2.0}, {97.0, 5.0}}, {}));
    auto s = ob.snapshot(10);
    QCOMPARE(s.bestAsk,  0.0);
    QCOMPARE(s.bestBid,  98.0);
    QCOMPARE(s.midPrice, 98.0);
    QCOMPARE(s.spread,   0.0);
}

void TestOrderBook::bothSidesEmptyMidPriceIsZero()
{
    OrderBook ob;  // freshly constructed — both sides empty
    auto s = ob.snapshot(10);
    QCOMPARE(s.midPrice, 0.0);
    QCOMPARE(s.spread,   0.0);
}

QTEST_MAIN(TestOrderBook)
#include "test_orderbook.moc"
