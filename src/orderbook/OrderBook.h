#pragma once

#include <map>
#include "core/Types.h"

class OrderBook
{
public:
    void applyUpdate(const OrderBookUpdate &update);
    BookSnapshot snapshot(int depth = 20) const;
    void clear();

private:
    std::map<double, double, std::greater<double>> m_bids;
    std::map<double, double> m_asks;
};
