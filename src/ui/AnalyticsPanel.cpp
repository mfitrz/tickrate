#include "AnalyticsPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QDateTime>

AnalyticsPanel::AnalyticsPanel(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet("background: #080c12;");

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // ── Header bar ─────────────────────────────────────────────────────────────
    auto *header = new QWidget();
    header->setFixedHeight(32);
    header->setStyleSheet("background: #050810; border-bottom: 1px solid #141c28;");
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 0, 10, 0);
    auto *hTitle = new QLabel("ANALYTICS");
    hTitle->setStyleSheet(
        "color: #486480; font-size: 11px; font-weight: 700; letter-spacing: 1.5px;");
    hl->addWidget(hTitle);
    hl->addStretch();
    mainLayout->addWidget(header);

    // ── Stat cards ─────────────────────────────────────────────────────────────
    auto *cardRow = new QWidget();
    cardRow->setFixedHeight(60);
    cardRow->setStyleSheet("background: #050810;");
    auto *cardLayout = new QHBoxLayout(cardRow);
    cardLayout->setContentsMargins(8, 6, 8, 6);
    cardLayout->setSpacing(8);

    auto makeCard = [&](QLabel *&label, const QString &defaultText) {
        auto *card = new QWidget();
        card->setStyleSheet(
            "background: #0c1220; border: 1px solid #141e2c; border-radius: 4px;");
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(8, 4, 8, 4);
        cl->setSpacing(0);
        label = new QLabel(defaultText);
        label->setStyleSheet("color: #4a6080; font-size: 13px; font-family: Consolas;");
        cl->addWidget(label, 0, Qt::AlignCenter);
        return card;
    };

    cardLayout->addWidget(makeCard(m_midLabel,       "Mid: --"));
    cardLayout->addWidget(makeCard(m_spreadLabel,    "Spread: --"));
    cardLayout->addWidget(makeCard(m_imbalanceLabel, "Book: --"));
    cardLayout->addWidget(makeCard(m_ofiLabel,       "OFI: --"));
    cardLayout->addWidget(makeCard(m_latencyLabel,   "Lat: -- ms"));
    mainLayout->addWidget(cardRow);

    // Divider
    auto *div = new QFrame();
    div->setFixedHeight(1);
    div->setStyleSheet("background: #141c28;");
    mainLayout->addWidget(div);

    // ── Charts ─────────────────────────────────────────────────────────────────
    auto *chartsWidget = new QWidget();
    auto *chartsLayout = new QVBoxLayout(chartsWidget);
    chartsLayout->setContentsMargins(6, 6, 6, 6);
    chartsLayout->setSpacing(6);

    setupCharts();
    chartsLayout->addWidget(m_ofiChartView, 1);
    chartsLayout->addWidget(m_spreadChartView, 1);
    mainLayout->addWidget(chartsWidget, 1);
    m_chartsWidget = chartsWidget;
}

void AnalyticsPanel::setChartsVisible(bool visible)
{
    if (m_chartsWidget)
        m_chartsWidget->setVisible(visible);
}

void AnalyticsPanel::setupLabels()
{
    // Labels are created inline in the constructor via makeCard lambda
}

void AnalyticsPanel::setupCharts()
{
    auto makeChart = [](const QString &title, QColor lineColor,
                        QLineSeries *&series, QChart *&chart,
                        QDateTimeAxis *&axisX, QValueAxis *&axisY,
                        QChartView *&view)
    {
        series = new QLineSeries();
        series->setColor(lineColor);
        series->setPen(QPen(lineColor, 1.5));

        chart = new QChart();
        chart->addSeries(series);
        chart->setTitle(title);
        chart->setBackgroundBrush(QBrush(QColor(0x0a, 0x10, 0x1a)));
        chart->setTitleBrush(QBrush(QColor(0x30, 0x48, 0x60)));
        chart->setTitleFont([]{ QFont f; f.setFamily("Segoe UI"); f.setPointSize(10); return f; }());
        chart->legend()->hide();
        chart->setMargins(QMargins(2, 2, 2, 2));
        chart->setBackgroundRoundness(0);

        axisX = new QDateTimeAxis();
        axisX->setFormat("HH:mm:ss");
        axisX->setTickCount(5);
        axisX->setLabelsColor(QColor(0x48, 0x60, 0x7e));
        axisX->setLabelsFont([]{ QFont f; f.setPointSize(9); return f; }());
        axisX->setGridLineColor(QColor(0x12, 0x1c, 0x28));
        axisX->setLinePen(QPen(QColor(0x14, 0x1e, 0x2c)));
        chart->addAxis(axisX, Qt::AlignBottom);
        series->attachAxis(axisX);

        axisY = new QValueAxis();
        axisY->setLabelsColor(QColor(0x48, 0x60, 0x7e));
        axisY->setLabelsFont([]{ QFont f; f.setPointSize(9); return f; }());
        axisY->setGridLineColor(QColor(0x12, 0x1c, 0x28));
        axisY->setLinePen(QPen(QColor(0x14, 0x1e, 0x2c)));
        chart->addAxis(axisY, Qt::AlignLeft);
        series->attachAxis(axisY);

        view = new QChartView(chart);
        view->setRenderHint(QPainter::Antialiasing);
        view->setStyleSheet("background: #0a101a; border: 1px solid #141e2c; border-radius: 3px;");
        view->setMinimumHeight(90);
    };

    makeChart("Order Flow Imbalance", QColor(0x00, 0xcc, 0x88),
              m_ofiSeries, m_ofiChart, m_ofiAxisX, m_ofiAxisY, m_ofiChartView);

    // Zero reference line on OFI chart
    m_ofiZeroLine = new QLineSeries();
    m_ofiZeroLine->setPen(QPen(QColor(0x30, 0x44, 0x5a, 160), 1, Qt::DashLine));
    m_ofiChart->addSeries(m_ofiZeroLine);
    m_ofiZeroLine->attachAxis(m_ofiAxisX);
    m_ofiZeroLine->attachAxis(m_ofiAxisY);

    makeChart("Spread (USD)", QColor(0xd0, 0xa8, 0x30),
              m_spreadSeries, m_spreadChart, m_spreadAxisX, m_spreadAxisY, m_spreadChartView);
}

void AnalyticsPanel::update(const AnalyticsSnapshot &snap)
{
    // Auto decimal places for mid price
    int midDec = (snap.midPrice >= 1000) ? 2 : (snap.midPrice >= 10) ? 3 : 4;
    m_midLabel->setText(QString("Mid  $%1").arg(snap.midPrice, 0, 'f', midDec));
    m_midLabel->setStyleSheet("color: #8898a8; font-size: 13px; font-family: Consolas;");

    // Spread: show both absolute and relative (bps)
    m_spreadLabel->setText(QString("Sprd  $%1  ·  %2 bps")
                               .arg(snap.spread, 0, 'f', 2)
                               .arg(snap.spreadBps, 0, 'f', 1));
    m_spreadLabel->setStyleSheet("color: #b09030; font-size: 12px; font-family: Consolas;");

    // Book imbalance: bid % vs ask % at top-10 levels
    {
        const int bidPct = qRound(snap.bookImbalance * 100.0);
        const int askPct = 100 - bidPct;
        const bool bidHeavy = snap.bookImbalance >= 0.5;
        const QString col = bidHeavy ? "#28cc70" : "#e05050";
        m_imbalanceLabel->setText(
            QString("Book  %1% Bid  ·  %2% Ask").arg(bidPct).arg(askPct));
        m_imbalanceLabel->setStyleSheet(
            QString("color: %1; font-size: 12px; font-family: Consolas;").arg(col));
    }

    double ofi = snap.orderFlowImbalance;
    QString ofiColor = ofi > 0 ? "#28cc70" : "#e05050";
    m_ofiLabel->setText(QString("OFI  %1%2")
                            .arg(ofi >= 0 ? "+" : "")
                            .arg(ofi, 0, 'f', 3));
    m_ofiLabel->setStyleSheet(
        QString("color: %1; font-size: 13px; font-family: Consolas;").arg(ofiColor));

    // Adjusted latency is 0-based (clock-skew removed), so thresholds are tighter
    QString latColor = snap.latencyMs < 10  ? "#28cc70"
                     : snap.latencyMs < 50  ? "#d0a030"
                                            : "#e05050";
    m_latencyLabel->setText(QString("Lat  %1ms").arg(snap.latencyMs));
    m_latencyLabel->setStyleSheet(
        QString("color: %1; font-size: 13px; font-family: Consolas;").arg(latColor));

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_ofiSeries->append(nowMs, ofi);
    m_spreadSeries->append(nowMs, snap.spread);

    if (m_ofiSeries->count() > kMaxPoints) {
        m_ofiSeries->remove(0);
        m_spreadSeries->remove(0);
    }

    // X range: rolling window of kMaxPoints samples
    if (m_ofiSeries->count() >= 2) {
        const qreal oldestX = m_ofiSeries->points().first().x();
        const qreal newestX = m_ofiSeries->points().last().x();
        m_ofiAxisX->setRange(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(oldestX)),
                             QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(newestX)));
        m_spreadAxisX->setRange(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(oldestX)),
                                QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(newestX)));
        // Keep zero line spanning the full visible X range
        m_ofiZeroLine->clear();
        m_ofiZeroLine->append(oldestX, 0.0);
        m_ofiZeroLine->append(newestX, 0.0);
    }

    // OFI Y auto-scale
    if (m_ofiSeries->count() > 0) {
        double ofiMax = 0.01;
        for (const auto &pt : m_ofiSeries->points())
            ofiMax = std::max(ofiMax, std::abs(pt.y()));
        double pad = ofiMax * 0.20;
        m_ofiAxisY->setRange(-ofiMax - pad, ofiMax + pad);
    }

    // Spread Y auto-scale
    if (m_spreadSeries->count() > 1) {
        double minS = snap.spread, maxS = snap.spread;
        for (const auto &pt : m_spreadSeries->points()) {
            minS = std::min(minS, pt.y());
            maxS = std::max(maxS, pt.y());
        }
        double pad = std::max((maxS - minS) * 0.15, 0.01);
        m_spreadAxisY->setRange(minS - pad, maxS + pad);
    }
}
