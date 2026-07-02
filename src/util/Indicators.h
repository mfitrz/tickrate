#pragma once
#include <vector>

// EMA seeded from first close (exponential warm-up, not SMA seed)
inline std::vector<double> computeEma(const std::vector<double> &closes, int period)
{
    if (closes.empty()) return {};
    const double k = 2.0 / (period + 1);
    std::vector<double> out(closes.size());
    out[0] = closes[0];
    for (std::size_t i = 1; i < closes.size(); ++i)
        out[i] = closes[i] * k + out[i - 1] * (1.0 - k);
    return out;
}

// Running VWAP accumulator — resets automatically at UTC midnight
struct VwapAccumulator {
    double num = 0.0, den = 0.0;
    int    day = -1;

    void add(double price, double volume, int utcDay) {
        if (day < 0 || utcDay != day) { num = 0.0; den = 0.0; day = utcDay; }
        if (volume <= 0.0) return;
        num += price * volume;
        den += volume;
    }
    double value(double fallback = 0.0) const { return den > 0.0 ? num / den : fallback; }
    void   reset() { num = 0.0; den = 0.0; day = -1; }
};

// Trade-size tier (1–5) relative to a running size EMA; tier 3 during warmup
inline int tradeTier(double size, double sizeEma, int tradeCount, int warmup = 30)
{
    if (tradeCount < warmup || sizeEma <= 0.0) return 3;
    const double r = size / sizeEma;
    if      (r > 10.0) return 5;
    else if (r >  4.0) return 4;
    else if (r >  1.0) return 3;
    else if (r >  0.3) return 2;
    else               return 1;
}

// Auto-select footprint tick-size from price level
inline double fpTickSize(double price)
{
    if      (price >= 10000.0) return 10.0;
    else if (price >=  1000.0) return  1.0;
    else if (price >=   100.0) return  0.1;
    else                       return  0.01;
}

// Value Area: POC index + the lo/hi bin indices covering targetPct of volume
struct ValueArea { int pocIdx = 0, loIdx = 0, hiIdx = 0; };

inline ValueArea computeValueArea(const std::vector<double> &binVols,
                                  double targetPct = 0.70)
{
    if (binVols.empty()) return {};
    const int n = static_cast<int>(binVols.size());

    int pocIdx = 0;
    for (int i = 1; i < n; ++i)
        if (binVols[i] > binVols[pocIdx]) pocIdx = i;

    double total = 0.0;
    for (double v : binVols) total += v;
    const double target = total * targetPct;

    int    loIdx = pocIdx, hiIdx = pocIdx;
    double accum = binVols[pocIdx];

    while (accum < target) {
        const double nextLo = (loIdx > 0)     ? binVols[loIdx - 1] : 0.0;
        const double nextHi = (hiIdx < n - 1) ? binVols[hiIdx + 1] : 0.0;
        if (nextLo == 0.0 && nextHi == 0.0) break;
        if (nextHi >= nextLo && hiIdx < n - 1) { ++hiIdx; accum += nextHi; }
        else if (loIdx > 0)                     { --loIdx; accum += nextLo; }
        else                                    { ++hiIdx; accum += nextHi; }
    }

    return {pocIdx, loIdx, hiIdx};
}
