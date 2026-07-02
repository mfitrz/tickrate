#pragma once

#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <deque>
#include <vector>
#include <map>
#include <unordered_map>
#include "core/Types.h"
#include "util/Indicators.h"

class CandlestickWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CandlestickWidget(QWidget *parent = nullptr);
    void addMidPrice(double mid, qint64 timestampMs);
    void addTrade(const TradeInfo &trade);
    void seedCandles(const std::vector<Candle> &candles);
    int  currentIntervalSec() const { return m_intervalSec; }

signals:
    void intervalChanged(int seconds);
    void alertTriggered(double price);

public slots:
    void setInterval(int seconds);   // 60, 300, 900, 3600

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    static constexpr int kMaxCandles = 720;

    std::deque<Candle> m_candles;
    Candle   m_current;
    bool     m_hasCurrent  = false;
    int      m_intervalSec = 60;

    // Zoom & pan
    int    m_candleW    = 8;    // pixels per candle slot [2..40]
    int    m_panOffset  = 0;    // candles scrolled from right (0 = live edge)
    bool   m_isDragging = false;
    QPoint m_dragOrigin;
    int    m_panAtDrag  = 0;

    // Crosshair
    QPoint m_mousePos;
    bool   m_mouseIn    = false;
    int    m_hoveredBtn = -1;

    void closeCandle();
    QString formatTime(qint64 ms) const;
    void checkAlertCrossings(double prevMid, double newMid);

    std::vector<double> m_alerts;
    double              m_lastMid = 0.0;

    VwapAccumulator m_vwap;

    // Seed/live boundary — openTimeMs of the last REST-seeded candle, 0 = none
    qint64  m_seedBoundaryMs = 0;

    // ── Indicator visibility toggles (legend click) ───────────────────────────
    // [0]=EMA9  [1]=EMA21  [2]=EMA50  [3]=BB(20,2)  [4]=VWAP
    bool  m_legendEnabled[5] = {true, true, true, true, true};
    QRect m_legendRects[5];
    int   m_legendCount = 0;

    // ── Footprint candle data ─────────────────────────────────────────────────
    // Per-candle (keyed by openTimeMs), per-price-level buy/sell volume.
    // Only populated for live candles (REST-seeded candles have no tick data).
    struct FpRow { double buy = 0.0, sell = 0.0; };
    std::unordered_map<qint64, std::map<double, FpRow>> m_footprints;
    double m_fpTickSize = 1.0;   // price bucketing grid, auto-detected from trades

    bool m_skelActive = true;
    void paintSkeleton(QPainter &p);
};
