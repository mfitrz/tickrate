#include "CS2PriceChartWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QFrame>
#include <QLocale>
#include <QDateTime>
#include <QTimer>
#include <algorithm>
#include <cmath>

static const char *kRangeLabels[]    = {"1W", "1M", "1Y", "ALL"};
static const qint64 kRangeLookback[] = {
    7LL  * 86400 * 1000,
    30LL * 86400 * 1000,
    365LL* 86400 * 1000,
    0LL
};

// Button layout constants — shared between resizeEvent and repositionLegend
static constexpr int kBtnH     = 30;
static constexpr int kBtnGap   = 4;
static constexpr int kMarginT  = 6;
static constexpr int kMarginL  = 8;
static constexpr int kMarginR  = 8;
static constexpr int kMktW     = 84;
static constexpr int kRangeW   = 50;

// Per-wear line colours (FN→BS)
static const QColor kWearColors[kWearCount] = {
    QColor(0x40, 0xff, 0xa0),  // FN: mint green
    QColor(0x60, 0xc0, 0xff),  // MW: sky blue
    QColor(0xff, 0xd0, 0x40),  // FT: gold
    QColor(0xff, 0x80, 0x40),  // WW: orange
    QColor(0xff, 0x50, 0x50),  // BS: red
};

// ── Constructor ───────────────────────────────────────────────────────────────

CS2PriceChartWidget::CS2PriceChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet("background:#080c12;");

    m_chart = new QChart();
    m_chart->setBackgroundBrush(QBrush(QColor(0x08, 0x0c, 0x12)));
    m_chart->setPlotAreaBackgroundBrush(QBrush(QColor(0x08, 0x0c, 0x12)));
    m_chart->setPlotAreaBackgroundVisible(true);
    m_chart->legend()->hide();
    m_chart->setMargins(QMargins(6, 6, 6, 4));
    m_chart->setBackgroundRoundness(0);
    m_chart->setTitle("");

    QFont axisFont;
    axisFont.setFamily("Consolas");
    axisFont.setPointSize(10);

    m_axisX = new QDateTimeAxis();
    m_axisX->setFormat("MMM d");
    m_axisX->setTickCount(6);
    m_axisX->setLabelsColor(QColor(0x30, 0x48, 0x60));
    m_axisX->setLabelsFont(axisFont);
    m_axisX->setGridLineColor(QColor(0x10, 0x18, 0x24));
    m_axisX->setLinePen(QPen(QColor(0x14, 0x1e, 0x2c)));
    m_axisX->setShadesBrush(Qt::NoBrush);
    m_axisX->setShadesVisible(false);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    m_axisY = new QValueAxis();
    m_axisY->setLabelFormat("$%.2f");
    m_axisY->setTickCount(5);
    m_axisY->setLabelsColor(QColor(0x30, 0x48, 0x60));
    m_axisY->setLabelsFont(axisFont);
    m_axisY->setGridLineColor(QColor(0x10, 0x18, 0x24));
    m_axisY->setLinePen(QPen(QColor(0x14, 0x1e, 0x2c)));
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    // Create all 5 wear series
    for (int i = 0; i < kWearCount; ++i) {
        m_wearSeries[i] = new QLineSeries(this);
        m_chart->addSeries(m_wearSeries[i]);
        m_wearSeries[i]->attachAxis(m_axisX);
        m_wearSeries[i]->attachAxis(m_axisY);
    }
    m_series = m_wearSeries[m_activeWear];
    updateWearStyles();

    // ── Chart view ────────────────────────────────────────────────────────────
    m_view = new QChartView(m_chart, this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setStyleSheet("background:#080c12; border:none;");
    m_view->setContentsMargins(0, 0, 0, 0);
    m_view->viewport()->installEventFilter(this);
    m_view->viewport()->setMouseTracking(true);

    // ── Range buttons ─────────────────────────────────────────────────────────
    static const QString kBtnStyle =
        "QPushButton {"
        "  background:#0a1020; color:#2a4060; border:1px solid #141e2e;"
        "  border-radius:3px; font-size:13px; font-family:Consolas;"
        "  font-weight:700; padding:1px 6px; }"
        "QPushButton:checked  { background:#0e1c30; color:#4a90d0; border-color:#2a5080; }"
        "QPushButton:hover    { background:#0c1628; color:#3a6090; }"
        "QPushButton:disabled { background:#070c18; color:#141e2c; border-color:#0c1220; }";

    for (int i = 0; i < 4; ++i) {
        m_rangeBtns[i] = new QPushButton(kRangeLabels[i], this);
        m_rangeBtns[i]->setCheckable(true);
        m_rangeBtns[i]->setFixedSize(kRangeW, kBtnH);
        m_rangeBtns[i]->setStyleSheet(kBtnStyle);
        m_rangeBtns[i]->setCursor(Qt::PointingHandCursor);
        const int idx = i;
        connect(m_rangeBtns[i], &QPushButton::clicked, this, [this, idx]() {
            m_activeRange = idx;
            m_userZoomed  = false;
            for (int j = 0; j < 4; ++j)
                m_rangeBtns[j]->setChecked(j == idx);
            m_view->setUpdatesEnabled(false);
            applyRange(idx);
            m_view->setUpdatesEnabled(true);
            m_view->update();
        });
    }
    m_rangeBtns[m_activeRange]->setChecked(true);

    // ── All-wears toggle ──────────────────────────────────────────────────────
    static const QString kAllWearsStyle =
        "QPushButton {"
        "  background:#120e04; color:#c08828; border:1px solid #503810;"
        "  border-radius:3px; font-size:12px; font-family:Consolas;"
        "  font-weight:700; padding:1px 5px; }"
        "QPushButton:checked { background:#1e1608; color:#ffe090; border-color:#c08828; }"
        "QPushButton:hover   { background:#1a1206; color:#e0a030; border-color:#7a5010; }";
    m_allWearsBtn = new QPushButton("DISPLAY ALL GRAPHS", this);
    m_allWearsBtn->setCheckable(true);
    m_allWearsBtn->setChecked(m_showAllWears);
    m_allWearsBtn->setFixedHeight(kBtnH);
    m_allWearsBtn->setStyleSheet(kAllWearsStyle);
    m_allWearsBtn->setCursor(Qt::PointingHandCursor);
    connect(m_allWearsBtn, &QPushButton::clicked, this, [this](bool checked) {
        m_showAllWears = checked;
        m_view->setUpdatesEnabled(false);
        updateWearStyles();
        rebuildAxes();
        m_view->setUpdatesEnabled(true);
        m_lastSnapIdx = -1;
        m_view->update();
        updateLegend();
    });

    // ── Marketplace selector ──────────────────────────────────────────────────
    static const char *kMktLabels[] = {"STEAM", "BUFF", "SKINPORT", "CSFLOAT", "YOUPIN"};
    static const char *kMktKeys[]   = {"steam", "buff", "skinport", "csfloat", "youpin"};
    // Brand colors — always visible; brighten on selected, dim slightly on hover
    // default text = medium brand tint  |  checked text = bright brand  |  checked border = brand
    static const char *kMktDefBg[]   = {"#060e18", "#180e02", "#0e0618", "#06180e", "#180606"};
    static const char *kMktDefCol[]  = {"#3a6898", "#c07818", "#7040a0", "#18a070", "#b04040"};
    static const char *kMktDefBdr[]  = {"#1a3060", "#603810", "#381858", "#0e5038", "#581818"};
    static const char *kMktSelCol[]  = {"#6aacf8", "#f0c040", "#c080f0", "#40e8c0", "#f07070"};
    static const char *kMktSelBdr[]  = {"#4a8cd8", "#f0a020", "#a060d0", "#20c8a0", "#e05050"};
    static const char *kMktHovCol[]  = {"#5a9ce8", "#d08820", "#9050c0", "#28b888", "#c85050"};

    for (int i = 0; i < 5; ++i) {
        m_mktBtns[i] = new QPushButton(kMktLabels[i], this);
        m_mktBtns[i]->setCheckable(true);
        m_mktBtns[i]->setFixedSize(kMktW, kBtnH);
        const QString style =
            QString("QPushButton {"
            "  background:%1; color:%2; border:1px solid %3;"
            "  border-radius:3px; font-size:12px; font-family:Consolas;"
            "  font-weight:700; padding:1px 5px; }")
                .arg(kMktDefBg[i]).arg(kMktDefCol[i]).arg(kMktDefBdr[i])
            + QString("QPushButton:checked { background:%1; color:%2; border:2px solid %3; }")
                .arg(kMktDefBg[i]).arg(kMktSelCol[i]).arg(kMktSelBdr[i])
            + QString("QPushButton:hover   { background:%1; color:%2; border-color:%3; }")
                .arg(kMktDefBg[i]).arg(kMktHovCol[i]).arg(kMktDefBdr[i]);
        m_mktBtns[i]->setStyleSheet(style);
        m_mktBtns[i]->setCursor(Qt::PointingHandCursor);
        const int   idx = i;
        const char *key = kMktKeys[i];
        connect(m_mktBtns[i], &QPushButton::clicked, this, [this, idx, key]() {
            m_activeMkt = idx;
            for (int j = 0; j < 5; ++j)
                m_mktBtns[j]->setChecked(j == idx);
            applyMarketTheme(idx);
            // Clear all wear data — will be refilled by mainwindow
            for (int j = 0; j < kWearCount; ++j) {
                m_wearCandles[j].clear();
                m_wearSeries[j]->clear();
            }
            m_overlay->setVisible(true);
            emit marketplaceChangeRequested(QString(key));
        });
    }
    m_mktBtns[m_activeMkt]->setChecked(true);
    applyMarketTheme(m_activeMkt);   // apply initial market theme

    // ── Overlays ──────────────────────────────────────────────────────────────
    m_overlay = new QLabel(this);
    m_overlay->setAlignment(Qt::AlignCenter);
    m_overlay->setWordWrap(true);
    m_overlay->setStyleSheet(
        "color:#486480; font-size:16px; font-family:'Segoe UI';"
        "background:transparent; padding:20px;");
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    setStatusMessage("Connect to view price history.");

    m_crosshair = new QLabel(this);
    m_crosshair->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_crosshair->setAttribute(Qt::WA_TransparentForMouseEvents);
    updateCrosshairStyle();   // sets initial wear-themed colors (FN = mint green)
    m_crosshair->hide();

    // Wear colour legend
    m_legend = new QLabel(this);
    m_legend->setTextFormat(Qt::RichText);
    m_legend->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_legend->setStyleSheet(
        "background:#0a1420cc; padding:3px 8px;"
        "border:1px solid #141e2e; border-radius:3px;");
    m_legend->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_legend->hide();   // shown when any wear has data

    // Title label — floats between the two button groups
    m_titleLabel = new QLabel("PRICE HISTORY", this);
    m_titleLabel->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
    m_titleLabel->setStyleSheet(
        "color:#486480; font-size:11pt; font-family:'Segoe UI'; font-weight:bold;"
        "background:transparent;");
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_vLine = new QFrame(this);
    m_vLine->setFixedWidth(1);
    m_vLine->setStyleSheet("background: rgba(160,200,232,50);");
    m_vLine->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_vLine->hide();

    m_dot = new QLabel(this);
    m_dot->setFixedSize(14, 14);
    m_dot->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dot->hide();
    updateWearStyles();  // set initial dot colour
}

// ── Public API ────────────────────────────────────────────────────────────────

void CS2PriceChartWidget::setSkinName(const QString &name)
{
    if (!m_titleLabel) return;
    Q_UNUSED(name);
    m_titleLabel->setText("PRICE HISTORY");
}

void CS2PriceChartWidget::seedWearHistory(int wearIdx, const std::vector<Candle> &candles)
{
    if (wearIdx < 0 || wearIdx >= kWearCount) return;
    m_userZoomed = false;
    m_wearCandles[wearIdx] = candles;

    // Coalesce: if multiple wears arrive in the same event-loop tick (e.g. all
    // five cache hits fire their QTimer::singleShot(0) together), only rebuild once.
    if (!m_rebuildPending) {
        m_rebuildPending = true;
        QTimer::singleShot(0, this, [this]() {
            m_rebuildPending = false;
            m_view->setUpdatesEnabled(false);   // freeze view before any range fallback fires
            updateRangeButtons();               // may change m_activeRange via internal fallback
            applyRange(m_activeRange);
            updateWearStyles();
            m_view->setUpdatesEnabled(true);
            m_view->update();
            updateLegend();
        });
    }
}

void CS2PriceChartWidget::setActiveWear(int wearIdx)
{
    if (wearIdx < 0 || wearIdx >= kWearCount) return;
    const bool changed = (wearIdx != m_activeWear);
    m_activeWear  = wearIdx;
    m_series      = m_wearSeries[m_activeWear];
    m_cachedPts   = m_wearSeries[m_activeWear]->points();
    m_lastSnapIdx = -1;
    if (changed) {
        updateRangeButtons();
        m_view->setUpdatesEnabled(false);
        updateWearStyles();
        rebuildAxes();
        m_view->setUpdatesEnabled(true);
        m_view->update();
        updateCrosshairStyle();
    }
    updateLegend();   // always re-sync — legend can be stale even when wear index hasn't changed
}

void CS2PriceChartWidget::addPricePoint(double price, qint64 tsMs)
{
    if (price <= 0.0) return;

    Candle c;
    c.openTimeMs = tsMs;
    c.open = c.close = c.high = c.low = price;
    c.volume = 1.0;
    c.complete = true;
    m_wearCandles[m_activeWear].push_back(c);

    auto &cv = m_wearCandles[m_activeWear];
    if (cv.size() > 1 && cv.back().openTimeMs < cv[cv.size()-2].openTimeMs)
        std::sort(cv.begin(), cv.end(), [](const Candle &a, const Candle &b){ return a.openTimeMs < b.openTimeMs; });

    if (!m_userZoomed)
        applyRange(m_activeRange);
}

void CS2PriceChartWidget::setStatusMessage(const QString &msg)
{
    m_overlay->setText(msg);
}

bool CS2PriceChartWidget::isEmpty() const
{
    for (int i = 0; i < kWearCount; ++i)
        if (!m_wearCandles[i].empty()) return false;
    return true;
}

bool CS2PriceChartWidget::isWearEmpty(int wearIdx) const
{
    if (wearIdx < 0 || wearIdx >= kWearCount) return true;
    return m_wearCandles[wearIdx].empty();
}

bool CS2PriceChartWidget::isRendering() const
{
    return m_series && m_series->count() >= 2;
}

void CS2PriceChartWidget::clear()
{
    m_userZoomed = false;
    for (int i = 0; i < kWearCount; ++i) {
        m_wearCandles[i].clear();
        m_wearSeries[i]->clear();
    }
    m_crosshair->hide();
    m_vLine->hide();
    m_dot->hide();
    m_legend->hide();
    m_overlay->setVisible(true);
}

// ── Range filtering ───────────────────────────────────────────────────────────

void CS2PriceChartWidget::applyRange(int rangeIdx)
{
    const bool anyData = !isEmpty();
    if (!anyData) {
        m_overlay->setVisible(true);
        return;
    }

    const qint64 cutoff = kRangeLookback[rangeIdx] > 0
        ? QDateTime::currentMSecsSinceEpoch() - kRangeLookback[rangeIdx]
        : 0LL;

    // Build all point lists first, then replace() each series once (one repaint per series
    // instead of one repaint per point).
    for (int i = 0; i < kWearCount; ++i) {
        const auto &candles = m_wearCandles[i];
        QList<QPointF> pts;
        pts.reserve(static_cast<int>(candles.size()));
        for (const Candle &c : candles) {
            if (c.openTimeMs >= cutoff)
                pts.append(QPointF(static_cast<qreal>(c.openTimeMs), c.close));
        }
        // Downsample to kMaxPoints keeping the most recent data
        if (pts.size() > kMaxPoints)
            pts = pts.mid(pts.size() - kMaxPoints);
        m_wearSeries[i]->replace(pts);
        // Keep a local copy of the active series so the eventFilter never calls points()
        if (i == m_activeWear) {
            m_cachedPts  = pts;
            m_lastSnapIdx = -1;   // invalidate snap cache after data change
        }
    }

    rebuildAxes();
}

void CS2PriceChartWidget::rebuildAxes()
{
    // Find global bounds across all non-empty wear series
    double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
    bool hasAny = false;

    for (int i = 0; i < kWearCount; ++i) {
        // When showing only the active wear, skip all others for axis scaling
        if (!m_showAllWears && i != m_activeWear) continue;
        const auto &pts = m_wearSeries[i]->points();
        if (pts.size() < 2) continue;
        hasAny = true;
        for (const QPointF &pt : pts) {
            if (pt.x() < minX) minX = pt.x();
            if (pt.x() > maxX) maxX = pt.x();
            if (pt.y() < minY) minY = pt.y();
            if (pt.y() > maxY) maxY = pt.y();
        }
    }

    if (!hasAny) {
        // Don't show the stale "Loading…" text — make the empty-range reason explicit
        m_overlay->setText("No price data in this range.");
        m_overlay->setVisible(true);
        return;
    }
    m_overlay->setVisible(false);

    if (maxX - minX < 3600000.0) maxX = minX + 3600000.0;
    const double padY = (maxY - minY) * 0.12 + 0.01;
    m_axisX->setRange(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(minX)),
                      QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(maxX)));
    m_axisY->setRange(std::max(0.0, minY - padY), maxY + padY);

    // Axis format and tick density based on the active time range
    if (m_activeRange >= 3) {          // ALL  — show years
        m_axisX->setFormat("yyyy");
        m_axisX->setTickCount(8);
    } else if (m_activeRange == 2) {   // 1Y   — month + year
        m_axisX->setFormat("MMM ''yy");
        m_axisX->setTickCount(7);
    } else if (m_activeRange == 1) {   // 1M   — day + month
        m_axisX->setFormat("MMM d");
        m_axisX->setTickCount(7);
    } else {                           // 1W   — day + month
        m_axisX->setFormat("MMM d");
        m_axisX->setTickCount(8);
    }
}

// ── Style helpers ─────────────────────────────────────────────────────────────

void CS2PriceChartWidget::updateWearStyles()
{
    for (int i = 0; i < kWearCount; ++i) {
        if (!m_wearSeries[i]) continue;
        const bool isEmpty = m_wearSeries[i]->points().isEmpty();
        // Hide entirely if: no data, OR all-wears mode is off and this is not the active wear
        m_wearSeries[i]->setVisible(!isEmpty && (m_showAllWears || i == m_activeWear));

        QPen pen;
        if (i == m_activeWear) {
            pen = QPen(kWearColors[i]);
            pen.setWidthF(2.0);
        } else {
            QColor dim = kWearColors[i];
            dim.setAlpha(60);
            pen = QPen(dim);
            pen.setWidthF(1.0);
        }
        m_wearSeries[i]->setPen(pen);
    }

    // Update snap dot: solid wear-color fill with a bright white outer ring for contrast
    if (m_dot) {
        const QColor &ac = kWearColors[m_activeWear];
        m_dot->setStyleSheet(QString(
            "background:%1; border-radius:7px; border:2px solid #ffffff;")
            .arg(ac.name()));
    }
}

void CS2PriceChartWidget::applyMarketTheme(int mktIdx)
{
    // Per-market color tables — STEAM / BUFF / SKINPORT / CSFLOAT / YOUPIN
    static const char *kWidgetBg[]  = {"#070e18","#160e04","#100916","#071210","#160909"};
    static const char *kPlotBg[]    = {"#0a1828","#201408","#180c22","#0a1a18","#200c0c"};
    static const char *kGridLine[]  = {"#0d1826","#241808","#180c26","#082018","#260c0c"};
    static const char *kAxisLine[]  = {"#142030","#2c2008","#1e1030","#0c2820","#2e1010"};
    static const char *kAxisText[]  = {"#2a4870","#706010","#584080","#206050","#703030"};
    static const char *kVLine[]     = {"rgba(160,200,232,50)","rgba(200,160,80,50)",
                                       "rgba(160,100,200,50)","rgba(80,200,160,50)",
                                       "rgba(200,100,100,50)"};
    static const char *kOverlayFg[] = {"#3070b0","#b07010","#8040b0","#209878","#b04040"};

    if (mktIdx < 0 || mktIdx >= 5) return;

    setStyleSheet(QString("background:%1;").arg(kWidgetBg[mktIdx]));
    m_view->setStyleSheet(QString("background:%1; border:none;").arg(kWidgetBg[mktIdx]));

    const QColor plot(kPlotBg[mktIdx]);
    m_chart->setBackgroundBrush(QBrush(plot));
    m_chart->setPlotAreaBackgroundBrush(QBrush(plot));

    const QColor grid(kGridLine[mktIdx]);
    const QColor axln(kAxisLine[mktIdx]);
    const QColor text(kAxisText[mktIdx]);

    m_axisX->setGridLineColor(grid);
    m_axisX->setLinePen(QPen(axln));
    m_axisX->setLabelsColor(text);

    m_axisY->setGridLineColor(grid);
    m_axisY->setLinePen(QPen(axln));
    m_axisY->setLabelsColor(text);

    // Title label
    if (m_titleLabel)
        m_titleLabel->setStyleSheet(
            QString("color:%1; font-size:11pt; font-family:'Segoe UI';"
                    " font-weight:bold; background:transparent;")
            .arg(kAxisText[mktIdx]));

    // Legend
    if (m_legend)
        m_legend->setStyleSheet(
            QString("background:%1cc; padding:3px 8px;"
                    " border:1px solid %2; border-radius:3px;")
            .arg(kPlotBg[mktIdx])
            .arg(kAxisLine[mktIdx]));

    // Crosshair info box — wear-themed, not market-themed
    updateCrosshairStyle();

    // Vertical crosshair line
    if (m_vLine)
        m_vLine->setStyleSheet(QString("background:%1;").arg(kVLine[mktIdx]));

    // Status/loading overlay text
    if (m_overlay)
        m_overlay->setStyleSheet(
            QString("color:%1; font-size:16px; font-family:'Segoe UI';"
                    " background:transparent; padding:20px;")
            .arg(kOverlayFg[mktIdx]));
}

void CS2PriceChartWidget::updateCrosshairStyle()
{
    if (!m_crosshair) return;
    const QColor &c = kWearColors[m_activeWear];
    // Dark tint background (wear color at ~8% luminance)
    const QColor bg(c.red() / 10, c.green() / 10, c.blue() / 10);
    // Mid-brightness border (wear color at ~45% luminance)
    const QColor bdr(c.red() * 45 / 100, c.green() * 45 / 100, c.blue() * 45 / 100);
    m_crosshair->setStyleSheet(
        QString("color:%1; background:%2; padding:8px 14px;"
                " font-family:Consolas; font-size:11pt; line-height:160%;"
                " border:1px solid %3; border-radius:4px;")
        .arg(c.name())
        .arg(bg.name())
        .arg(bdr.name()));
}

void CS2PriceChartWidget::updateRangeButtons()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const auto  &cv    = m_wearCandles[m_activeWear];

    // Find the oldest and newest candle timestamps across all wears (not just active)
    // so buttons reflect the full dataset, not just what is currently displayed.
    qint64 oldestMs = std::numeric_limits<qint64>::max();
    qint64 newestMs = 0;
    for (int i = 0; i < kWearCount; ++i) {
        for (const Candle &c : m_wearCandles[i]) {
            if (c.openTimeMs < oldestMs) oldestMs = c.openTimeMs;
            if (c.openTimeMs > newestMs) newestMs = c.openTimeMs;
        }
    }
    const bool anyData = newestMs > 0;

    for (int i = 0; i < 4; ++i) {
        bool hasData = false;
        if (anyData) {
            if (kRangeLookback[i] == 0) {
                hasData = true;   // ALL — always available if there's any data
            } else {
                const qint64 cutoff = nowMs - kRangeLookback[i];
                // Available if any candle falls within this range
                for (const Candle &c : cv) {
                    if (c.openTimeMs >= cutoff) { hasData = true; break; }
                }
            }
        }
        m_rangeBtns[i]->setEnabled(hasData);
    }

    // If the active range is now disabled, fall back to the widest available one
    if (!m_rangeBtns[m_activeRange]->isEnabled()) {
        for (int i = 3; i >= 0; --i) {
            if (m_rangeBtns[i]->isEnabled()) {
                m_activeRange = i;
                for (int j = 0; j < 4; ++j)
                    m_rangeBtns[j]->setChecked(j == i);
                applyRange(i);
                break;
            }
        }
    }
}

void CS2PriceChartWidget::repositionLegend()
{
    if (!m_legend) return;
    const int topOfChart = kMarginT + kBtnH + 8;
    const int lgW = m_legend->sizeHint().width();
    const int lgH = m_legend->sizeHint().height();
    // Top-right corner of the chart plot area
    m_legend->setGeometry(width() - kMarginR - lgW - 6, topOfChart + 6, lgW, lgH);
}

void CS2PriceChartWidget::updateLegend()
{
    bool any = false;
    QString html = "<span style='font-family:Consolas;font-size:12pt;white-space:nowrap;'>";
    for (int i = 0; i < kWearCount; ++i) {
        if (m_wearCandles[i].empty()) continue;
        // In single-wear mode only show the active wear in the legend
        if (!m_showAllWears && i != m_activeWear) continue;
        if (any) html += "&nbsp;&nbsp;";
        html += QString("<font color='%1'>\xe2\x96\xa0 %2</font>")
            .arg(kWearColors[i].name())
            .arg(kWearShort[i]);
        any = true;
    }
    html += "</span>";

    if (any) {
        m_legend->setText(html);
        m_legend->adjustSize();
        m_legend->show();
        repositionLegend();   // always position after size is known
        m_legend->raise();
    } else {
        m_legend->hide();
    }
}

// ── Layout ────────────────────────────────────────────────────────────────────

void CS2PriceChartWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);

    int mx = kMarginL;
    for (int i = 0; i < 5; ++i) {
        m_mktBtns[i]->setGeometry(mx, kMarginT, kMktW, kBtnH);
        mx += kMktW + kBtnGap;
    }

    // Layout (right to left): [ALL GRAPHS]  gap  [1W][1M][1Y][ALL]
    static constexpr int kAllWearsW   = 148;
    static constexpr int kAllWearsGap = 14;  // visible separator between date and overlay button
    const int totalRangeW = 4 * kRangeW + 3 * kBtnGap;
    // Range buttons are pushed left to make room for the overlay button + gap
    int bx = width() - kMarginR - kAllWearsW - kAllWearsGap - totalRangeW;
    for (int i = 0; i < 4; ++i) {
        m_rangeBtns[i]->setGeometry(bx, kMarginT, kRangeW, kBtnH);
        bx += kRangeW + kBtnGap;
    }
    // "ALL GRAPHS" toggle sits at the far right
    if (m_allWearsBtn)
        m_allWearsBtn->setGeometry(width() - kMarginR - kAllWearsW, kMarginT, kAllWearsW, kBtnH);

    m_view->setGeometry(rect());
    m_overlay->setGeometry(rect());

    // Title label fills the gap between the two button groups
    const int leftEnd    = kMarginL + 5 * kMktW + 4 * kBtnGap + 4;
    const int rightStart = width() - kMarginR - kAllWearsW - kAllWearsGap - totalRangeW;
    if (m_titleLabel)
        m_titleLabel->setGeometry(leftEnd, kMarginT, rightStart - leftEnd, kBtnH);

    repositionLegend();

    if (m_vLine)      m_vLine->raise();
    if (m_dot)        m_dot->raise();
    if (m_crosshair)  m_crosshair->raise();
    if (m_legend)     m_legend->raise();
    if (m_titleLabel) m_titleLabel->raise();
    for (auto *btn : m_mktBtns)   btn->raise();
    for (auto *btn : m_rangeBtns)  btn->raise();
}

// ── Event filter: hover crosshair ────────────────────────────────────────────

bool CS2PriceChartWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj != m_view->viewport())
        return QWidget::eventFilter(obj, event);

    switch (event->type()) {

    case QEvent::MouseMove: {
        if (!m_series || m_series->count() < 2 || m_cachedPts.size() < 2) {
            m_crosshair->hide(); m_vLine->hide(); m_dot->hide();
            m_crosshairWasVisible = false;
            break;
        }

        auto *me = static_cast<QMouseEvent *>(event);
        const QPointF val = m_chart->mapToValue(
            m_view->mapToScene(me->pos()), m_series);
        const double mouseX = val.x();

        // Use the cached point list — no O(n) copy from the series
        const QList<QPointF> &pts = m_cachedPts;
        if (mouseX < pts.front().x() || mouseX > pts.back().x()) {
            m_crosshair->hide(); m_vLine->hide(); m_dot->hide();
            m_crosshairWasVisible = false;
            break;
        }

        auto it = std::lower_bound(pts.begin(), pts.end(), mouseX,
            [](const QPointF &p, double x) { return p.x() < x; });
        int snapIdx = static_cast<int>(it - pts.begin());
        if (snapIdx > 0) {
            const int prev = snapIdx - 1;
            if (std::abs(pts[prev].x() - mouseX) < std::abs(pts[snapIdx].x() - mouseX))
                snapIdx = prev;
        }
        snapIdx = std::clamp(snapIdx, 0, static_cast<int>(pts.size()) - 1);
        const QPointF &nearest = pts[snapIdx];

        // Only reformat the label when the nearest point actually changed
        if (snapIdx != m_lastSnapIdx) {
            m_lastSnapIdx = snapIdx;

            const QDateTime dt    = QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(nearest.x()));
            const double    price = nearest.y();

            double vol = 0.0;
            {
                const qint64 ts = static_cast<qint64>(nearest.x());
                const auto &cv = m_wearCandles[m_activeWear];
                auto cit = std::lower_bound(cv.begin(), cv.end(), ts,
                    [](const Candle &c, qint64 t) { return c.openTimeMs < t; });
                if (cit != cv.end() && cit->openTimeMs == ts)
                    vol = cit->volume;
            }

            static const QLocale kLocale;
            const QString datePart = (m_activeRange == 0)
                ? dt.toString("MMM d  hh:mm")
                : dt.toString("MMM d, yyyy");
            QString volPart;
            if (vol >= 1.0)
                volPart = QString("\n%1 sold").arg(kLocale.toString(static_cast<qint64>(vol)));

            m_crosshair->setText(
                QString("%1\n$%2%3")
                    .arg(datePart)
                    .arg(price, 0, 'f', 2)
                    .arg(volPart));
            m_crosshair->adjustSize();
        }

        const QPointF snappedScene = m_chart->mapToPosition(nearest, m_series);
        const QPoint  snapView     = m_view->mapFromScene(snappedScene);
        const QPoint  snapW        = m_view->mapTo(this, snapView);

        m_vLine->setGeometry(snapW.x(), 0, 1, height());
        m_vLine->show();
        m_dot->move(snapW.x() - 7, snapW.y() - 7);
        m_dot->show();

        const QSize lsz = m_crosshair->size();
        int lx = snapW.x() + 14;
        if (lx + lsz.width() + 4 > width()) lx = snapW.x() - lsz.width() - 10;
        const int ly = std::clamp(snapW.y() - lsz.height() / 2, 4,
                                  height() - lsz.height() - 4);
        m_crosshair->move(lx, ly);
        m_crosshair->show();

        // Only re-raise the z-order when the crosshair first becomes visible.
        // When it's already up, the relative z-order hasn't changed.
        if (!m_crosshairWasVisible) {
            m_crosshairWasVisible = true;
            m_vLine->raise();
            m_dot->raise();
            m_crosshair->raise();
            for (auto *btn : m_mktBtns)   btn->raise();
            for (auto *btn : m_rangeBtns) btn->raise();
        }
        break;
    }

    case QEvent::Leave:
        m_crosshair->hide();
        m_vLine->hide();
        m_dot->hide();
        m_crosshairWasVisible = false;
        m_lastSnapIdx = -1;
        break;

    default:
        break;
    }

    return QWidget::eventFilter(obj, event);
}
