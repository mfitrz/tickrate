#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include "util/CandleBuilder.h"
#include "model/CS2Types.h"

class CS2PriceChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CS2PriceChartWidget(QWidget *parent = nullptr);

    // Feed a single wear's history (wearIdx 0–4)
    void seedWearHistory(int wearIdx, const std::vector<Candle> &candles);
    // Legacy: seeds wear 0 (backward compat)
    void seedHistory(const std::vector<Candle> &candles) { seedWearHistory(0, candles); }

    void addPricePoint(double price, qint64 tsMs);
    void setSkinName(const QString &name);
    void setStatusMessage(const QString &msg);
    void setActiveWear(int wearIdx);

    bool isEmpty()             const;
    bool isRendering()         const;
    bool isWearEmpty(int wearIdx) const;
    void clear();

signals:
    void marketplaceChangeRequested(const QString &marketplace);

protected:
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void applyRange(int rangeIdx);
    void rebuildAxes();
    void updateWearStyles();    // re-apply pen styles (active = bright, others = dim)
    void updateLegend();        // refresh wear legend label text
    void repositionLegend();    // place legend in the gap between button groups
    void updateRangeButtons();  // enable/disable each range button based on data availability
    void applyMarketTheme(int mktIdx);  // tint chart background to match selected market
    void updateCrosshairStyle();        // restyle crosshair box for the active wear

    QChartView    *m_view       = nullptr;
    QChart        *m_chart      = nullptr;
    QDateTimeAxis *m_axisX      = nullptr;
    QValueAxis    *m_axisY      = nullptr;
    QLabel        *m_overlay    = nullptr;
    QLabel        *m_crosshair  = nullptr;
    QLabel        *m_legend     = nullptr;  // wear colour legend
    QLabel        *m_titleLabel = nullptr;  // "PRICE HISTORY — skin" between button groups
    QFrame        *m_vLine      = nullptr;
    QLabel        *m_dot        = nullptr;

    // 5 series, one per wear; m_series is an alias to m_wearSeries[m_activeWear]
    QLineSeries   *m_wearSeries[kWearCount] = {};
    QLineSeries   *m_series                 = nullptr;
    int            m_activeWear             = 0;

    // Range buttons: 1D / 1W / 1M / ALL
    QPushButton   *m_rangeBtns[4] = {};
    int            m_activeRange  = 3;
    bool           m_userZoomed   = false;

    // Marketplace selector: STEAM | BUFF | SKINPORT | CSFLOAT | YOUPIN
    QPushButton   *m_mktBtns[5]   = {};
    int            m_activeMkt     = 0;

    // Multi-wear toggle: when true all wears with data are drawn; when false only active wear
    QPushButton   *m_allWearsBtn   = nullptr;
    bool           m_showAllWears  = false;

    // Per-wear master datasets
    std::vector<Candle> m_wearCandles[kWearCount];

    // Coalesced rebuild: multiple seedWearHistory() calls in the same event-loop
    // tick collapse into one applyRange() instead of N redundant redraws.
    bool m_rebuildPending = false;

    // Crosshair snap cache — avoids re-formatting text on every mouse-move event
    // when the nearest data point hasn't actually changed.
    QList<QPointF> m_cachedPts;       // mirror of active series; updated in applyRange
    int            m_lastSnapIdx  = -1;
    bool           m_crosshairWasVisible = false;

    static constexpr int kMaxPoints = 1500;
};
