#include <QtTest>
#include "util/Indicators.h"

class TestIndicators : public QObject
{
    Q_OBJECT

private slots:
    // computeEma
    void ema_empty();
    void ema_singleValue();
    void ema_seededFromFirstClose_notSma();
    void ema_period1_equalsInput();
    void ema_constantSeries_staysConstant();
    void ema_twoValues();
    void ema_convergesToConstant();

    // VwapAccumulator
    void vwap_emptyFallback();
    void vwap_singleTrade();
    void vwap_twoEqualSizeTrades_isAverage();
    void vwap_volumeWeighted();
    void vwap_midnightReset();
    void vwap_zeroVolumeIgnored();
    void vwap_reset();

    // tradeTier
    void tier_belowWarmup_isThree();
    void tier_atWarmup_classifies();
    void tier_r_above10_isFive();
    void tier_r_above4_isFour();
    void tier_r_above1_isThree();
    void tier_r_above0p3_isTwo();
    void tier_r_atOrBelow0p3_isOne();
    void tier_zeroEma_isThree();
    void tier_boundaries();

    // fpTickSize
    void fp_below100_isPointO1();
    void fp_exactly100_isPointO1();
    void fp_below1000_isPointO1();
    void fp_exactly1000_is1();
    void fp_below10000_is1();
    void fp_exactly10000_is10();
    void fp_above10000_is10();

    // computeValueArea
    void va_empty();
    void va_singleBin();
    void va_allVolumeInPoc_isJustPoc();
    void va_uniformDistribution_covers70Pct();
    void va_pocInMiddle_expandsBothSides();
    void va_heavyHighSide_skewsHigh();
    void va_customTargetPct();
};

// ── computeEma ────────────────────────────────────────────────────────────────

void TestIndicators::ema_empty()
{
    QVERIFY(computeEma({}, 9).empty());
}

void TestIndicators::ema_singleValue()
{
    auto r = computeEma({42.0}, 9);
    QCOMPARE(r.size(), std::size_t(1));
    QCOMPARE(r[0], 42.0);
}

void TestIndicators::ema_seededFromFirstClose_notSma()
{
    // With SMA seeding, first EMA value would be mean of first `period` closes.
    // With exponential seeding, ema[0] == closes[0] regardless of period.
    auto r = computeEma({100.0, 200.0, 300.0}, 3);
    QCOMPARE(r[0], 100.0);   // seeded from index 0
}

void TestIndicators::ema_period1_equalsInput()
{
    // k = 2/(1+1) = 1.0, so ema[i] = closes[i] * 1 + ema[i-1] * 0 = closes[i]
    std::vector<double> closes = {10.0, 20.0, 15.0, 25.0};
    auto r = computeEma(closes, 1);
    QCOMPARE(r.size(), closes.size());
    for (std::size_t i = 0; i < closes.size(); ++i)
        QCOMPARE(r[i], closes[i]);
}

void TestIndicators::ema_constantSeries_staysConstant()
{
    std::vector<double> closes(50, 100.0);
    auto r = computeEma(closes, 9);
    for (double v : r)
        QCOMPARE(v, 100.0);
}

void TestIndicators::ema_twoValues()
{
    // period=9 → k=0.2
    // ema[0]=100, ema[1]=110*0.2 + 100*0.8 = 22+80=102
    auto r = computeEma({100.0, 110.0}, 9);
    QCOMPARE(r.size(), std::size_t(2));
    QCOMPARE(r[0], 100.0);
    QVERIFY(qAbs(r[1] - 102.0) < 1e-9);
}

void TestIndicators::ema_convergesToConstant()
{
    // After many iterations of the same value, EMA converges to that value
    std::vector<double> closes(200, 50.0);
    auto r = computeEma(closes, 9);
    QVERIFY(qAbs(r.back() - 50.0) < 1e-6);
}

// ── VwapAccumulator ───────────────────────────────────────────────────────────

void TestIndicators::vwap_emptyFallback()
{
    VwapAccumulator v;
    QCOMPARE(v.value(99.0), 99.0);   // returns fallback when empty
}

void TestIndicators::vwap_singleTrade()
{
    VwapAccumulator v;
    v.add(100.0, 1.0, 0);
    QCOMPARE(v.value(), 100.0);
}

void TestIndicators::vwap_twoEqualSizeTrades_isAverage()
{
    VwapAccumulator v;
    v.add(100.0, 1.0, 0);
    v.add(200.0, 1.0, 0);
    QCOMPARE(v.value(), 150.0);
}

void TestIndicators::vwap_volumeWeighted()
{
    // 1 unit at 100 and 3 units at 200 → (100+600)/4 = 175
    VwapAccumulator v;
    v.add(100.0, 1.0, 0);
    v.add(200.0, 3.0, 0);
    QCOMPARE(v.value(), 175.0);
}

void TestIndicators::vwap_midnightReset()
{
    VwapAccumulator v;
    v.add(100.0, 1.0, 0);   // day 0
    QCOMPARE(v.value(), 100.0);
    v.add(200.0, 1.0, 1);   // day 1 → resets, then adds 200
    QCOMPARE(v.value(), 200.0);
}

void TestIndicators::vwap_zeroVolumeIgnored()
{
    VwapAccumulator v;
    v.add(100.0, 1.0, 0);
    v.add(999.0, 0.0, 0);   // zero volume: ignored
    QCOMPARE(v.value(), 100.0);
}

void TestIndicators::vwap_reset()
{
    VwapAccumulator v;
    v.add(100.0, 5.0, 0);
    v.reset();
    QCOMPARE(v.value(42.0), 42.0);   // back to fallback
}

// ── tradeTier ─────────────────────────────────────────────────────────────────

void TestIndicators::tier_belowWarmup_isThree()
{
    // Regardless of size vs EMA, tier is 3 during warmup
    QCOMPARE(tradeTier(1000.0, 0.001, 29), 3);
    QCOMPARE(tradeTier(0.0001, 9999.0, 0), 3);
}

void TestIndicators::tier_atWarmup_classifies()
{
    // At exactly warmup count (30), classification applies
    QCOMPARE(tradeTier(100.0, 1.0, 30), 5);   // r=100 > 10
}

void TestIndicators::tier_r_above10_isFive()
{
    QCOMPARE(tradeTier(10.1, 1.0, 30), 5);
    QCOMPARE(tradeTier(100.0, 1.0, 30), 5);
}

void TestIndicators::tier_r_above4_isFour()
{
    QCOMPARE(tradeTier(4.1, 1.0, 30), 4);
    QCOMPARE(tradeTier(10.0, 1.0, 30), 4);   // r==10 is NOT >10, so tier 4
}

void TestIndicators::tier_r_above1_isThree()
{
    QCOMPARE(tradeTier(1.1, 1.0, 30), 3);
    QCOMPARE(tradeTier(4.0, 1.0, 30), 3);   // r==4 is NOT >4
}

void TestIndicators::tier_r_above0p3_isTwo()
{
    QCOMPARE(tradeTier(0.31, 1.0, 30), 2);
    QCOMPARE(tradeTier(1.0,  1.0, 30), 2);   // r==1 is NOT >1
}

void TestIndicators::tier_r_atOrBelow0p3_isOne()
{
    QCOMPARE(tradeTier(0.3,  1.0, 30), 1);
    QCOMPARE(tradeTier(0.01, 1.0, 30), 1);
}

void TestIndicators::tier_zeroEma_isThree()
{
    // Guard: sizeEma==0 avoids division by zero, returns tier 3
    QCOMPARE(tradeTier(100.0, 0.0, 30), 3);
}

void TestIndicators::tier_boundaries()
{
    // Confirm every strict boundary
    QCOMPARE(tradeTier(10.0 + 1e-9, 1.0, 30), 5);
    QCOMPARE(tradeTier(10.0,        1.0, 30), 4);
    QCOMPARE(tradeTier(4.0 + 1e-9,  1.0, 30), 4);
    QCOMPARE(tradeTier(4.0,         1.0, 30), 3);
    QCOMPARE(tradeTier(1.0 + 1e-9,  1.0, 30), 3);
    QCOMPARE(tradeTier(1.0,         1.0, 30), 2);
    QCOMPARE(tradeTier(0.3 + 1e-9,  1.0, 30), 2);
    QCOMPARE(tradeTier(0.3,         1.0, 30), 1);
}

// ── fpTickSize ────────────────────────────────────────────────────────────────

void TestIndicators::fp_below100_isPointO1()
{
    QCOMPARE(fpTickSize(99.99), 0.01);
    QCOMPARE(fpTickSize(1.0),   0.01);
    QCOMPARE(fpTickSize(0.01),  0.01);
}

void TestIndicators::fp_exactly100_isPointO1()
{
    QCOMPARE(fpTickSize(100.0), 0.1);
}

void TestIndicators::fp_below1000_isPointO1()
{
    QCOMPARE(fpTickSize(500.0),  0.1);
    QCOMPARE(fpTickSize(999.99), 0.1);
}

void TestIndicators::fp_exactly1000_is1()
{
    QCOMPARE(fpTickSize(1000.0), 1.0);
}

void TestIndicators::fp_below10000_is1()
{
    QCOMPARE(fpTickSize(5000.0),  1.0);
    QCOMPARE(fpTickSize(9999.99), 1.0);
}

void TestIndicators::fp_exactly10000_is10()
{
    QCOMPARE(fpTickSize(10000.0), 10.0);
}

void TestIndicators::fp_above10000_is10()
{
    QCOMPARE(fpTickSize(50000.0), 10.0);
    QCOMPARE(fpTickSize(100000.0), 10.0);
}

// ── computeValueArea ─────────────────────────────────────────────────────────

void TestIndicators::va_empty()
{
    auto va = computeValueArea({});
    QCOMPARE(va.pocIdx, 0);
    QCOMPARE(va.loIdx,  0);
    QCOMPARE(va.hiIdx,  0);
}

void TestIndicators::va_singleBin()
{
    auto va = computeValueArea({5.0});
    QCOMPARE(va.pocIdx, 0);
    QCOMPARE(va.loIdx,  0);
    QCOMPARE(va.hiIdx,  0);
}

void TestIndicators::va_allVolumeInPoc_isJustPoc()
{
    // 100% of volume is in bin 2; VA covers it without expanding
    auto va = computeValueArea({0.0, 0.0, 10.0, 0.0, 0.0});
    QCOMPARE(va.pocIdx, 2);
    QCOMPARE(va.loIdx,  2);
    QCOMPARE(va.hiIdx,  2);
}

void TestIndicators::va_uniformDistribution_covers70Pct()
{
    // 10 equal bins, 10% each. POC = bin 0 (first max-tie).
    // Need 7 bins to cover 70%. Starting from bin 0, expansion goes hi only.
    std::vector<double> bins(10, 1.0);
    auto va = computeValueArea(bins);
    // VA must cover at least 7 bins (70% of 10)
    const int covered = va.hiIdx - va.loIdx + 1;
    QVERIFY(covered >= 7);
}

void TestIndicators::va_pocInMiddle_expandsBothSides()
{
    // POC in the middle; equal neighbours. Target=70% of 9=6.3.
    // tie-break rule: nextHi >= nextLo → always expand hi first.
    // accum: 5 → +1(hi,idx3)=6 → +1(hi,idx4)=7 ≥ 6.3. Done: lo=2, hi=4.
    std::vector<double> bins = {1.0, 1.0, 5.0, 1.0, 1.0};
    auto va = computeValueArea(bins);
    QCOMPARE(va.pocIdx, 2);
    QCOMPARE(va.loIdx,  2);
    QCOMPARE(va.hiIdx,  4);
}

void TestIndicators::va_heavyHighSide_skewsHigh()
{
    // Bins: [1, 1, 5, 8, 2] — heavy on the high side
    std::vector<double> bins = {1.0, 1.0, 5.0, 8.0, 2.0};
    auto va = computeValueArea(bins);
    QCOMPARE(va.pocIdx, 3);   // max is at index 3
    // VA starts at bin 3 (8 of 17 total = 47%) → must expand
    // nextHi=2 (idx 4), nextLo=5 (idx 2); nextLo > nextHi → expand lo
    // accum=8+5=13 (76% > 70%) → done. loIdx=2, hiIdx=3
    QCOMPARE(va.loIdx, 2);
    QCOMPARE(va.hiIdx, 3);
}

void TestIndicators::va_customTargetPct()
{
    // 50% target: single POC bin (100% of volume) is enough
    auto va = computeValueArea({0.0, 10.0, 0.0}, 0.50);
    QCOMPARE(va.pocIdx, 1);
    QCOMPARE(va.loIdx,  1);
    QCOMPARE(va.hiIdx,  1);
}

QTEST_MAIN(TestIndicators)
#include "test_indicators.moc"
