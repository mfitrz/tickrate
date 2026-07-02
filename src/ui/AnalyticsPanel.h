#pragma once

#include <QWidget>
#include <QLabel>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <deque>
#include "model/Analytics.h"

class AnalyticsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AnalyticsPanel(QWidget *parent = nullptr);
    void update(const AnalyticsSnapshot &snap);
    void setChartsVisible(bool visible);   // hide OFI/Spread charts in CS2 mode

private:
    static constexpr int kMaxPoints = 400; // ~27s at 15 updates/sec

    QLabel *m_ofiLabel;
    QLabel *m_spreadLabel;
    QLabel *m_latencyLabel;
    QLabel *m_midLabel;
    QLabel *m_imbalanceLabel;

    QChartView    *m_ofiChartView;
    QLineSeries   *m_ofiSeries;
    QLineSeries   *m_ofiZeroLine;
    QChart        *m_ofiChart;
    QDateTimeAxis *m_ofiAxisX;
    QValueAxis    *m_ofiAxisY;

    QChartView    *m_spreadChartView;
    QLineSeries   *m_spreadSeries;
    QChart        *m_spreadChart;
    QDateTimeAxis *m_spreadAxisX;
    QValueAxis    *m_spreadAxisY;

    QWidget *m_chartsWidget = nullptr;

    void setupCharts();
    void setupLabels();
};
