#include "ShimmerTimer.h"

ShimmerTimer &ShimmerTimer::instance()
{
    static ShimmerTimer s;
    return s;
}

ShimmerTimer::ShimmerTimer()
{
    connect(&m_timer, &QTimer::timeout, this, &ShimmerTimer::tick);
    m_timer.start(40);   // 25 fps
}
