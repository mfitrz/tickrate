#pragma once
#include <QObject>
#include <QTimer>
#include <QDateTime>

// Singleton 25 fps tick used by all skeleton-loading widgets.
// Use phase() to read the current sweep position [0, 1) — it is wall-clock based
// so every widget that reads it on the same frame gets an identical value,
// meaning all shimmer animations advance in perfect lock-step.
class ShimmerTimer : public QObject
{
    Q_OBJECT
public:
    static ShimmerTimer &instance();

    // Shared sweep phase [0, 1) derived from current millisecond — no stored state.
    static double phase() {
        return static_cast<double>(QDateTime::currentMSecsSinceEpoch() % 1600) / 1600.0;
    }

signals:
    void tick();

private:
    ShimmerTimer();
    QTimer m_timer;
};
