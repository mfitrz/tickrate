#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QApplication>
#include <QCompleter>
#include <QStringListModel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QLineEdit>
#include <QDateTime>
#include <QSettings>
#include <vector>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_bybit(new BybitClient(this))
    , m_binance(new BinanceClient(this))
    , m_cs2(new CS2MarketClient(this))
    , m_openSkin(new OpenSkinClient(this))
    , m_exchange(m_bybit)
    , m_model(new MarketDataModel(this))
{
    ui->setupUi(this);
    setupUi();

    connectExchangeSignals(m_bybit);
    connectExchangeSignals(m_binance);
    connectExchangeSignals(m_cs2);

    m_renderTimer.setInterval(33);
    connect(&m_renderTimer, &QTimer::timeout, this, &MainWindow::renderFrame);
    m_renderTimer.start();

    m_clockTimer.setInterval(1000);
    connect(&m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    m_clockTimer.start();
    updateClock();

    // ── Restore persisted settings ─────────────────────────────────────────
    QSettings cfg("OrderbookViz", "settings");

    restoreGeometry(cfg.value("geometry").toByteArray());

    m_candleIntervalSec = cfg.value("candleInterval", 60).toInt();
    if (m_candleIntervalSec < 60) m_candleIntervalSec = 60;
    m_candle->setInterval(m_candleIntervalSec);

    QString savedSym = cfg.value("symbol", "BTCUSDT").toString().toUpper();
    // Guard: a CS2 skin name (contains " | " or "(") is not a valid crypto pair.
    if (savedSym.contains(QLatin1String(" | ")) || savedSym.contains('('))
        savedSym = "BTCUSDT";
    m_symbolCombo->setCurrentText(savedSym);
    m_currentSymbol = savedSym;

    const QString savedExch = cfg.value("exchange", "cs2").toString();
    if (savedExch == "binance") {
        m_exchange = m_binance;
        const auto btns = m_exchangeGroup->buttons();
        if (btns.size() >= 2) btns[1]->setChecked(true);
    } else if (savedExch == "cs2") {
        m_cs2Btn->setChecked(true);
        const QString cs2Sym = cfg.value("cs2Symbol",
            "AK-47 | Redline (Field-Tested)").toString();
        m_savedCryptoSymbol = m_currentSymbol;
        enterCS2Mode();
        m_exchange = m_cs2;
        switchSymbol(cs2Sym);
    }

    // Restore splitter layouts (fall back to first-run defaults if absent)
    if (cfg.contains("mainSplitter"))    m_mainSplitter->restoreState(cfg.value("mainSplitter").toByteArray());
    if (cfg.contains("contentSplitter")) m_contentSplitter->restoreState(cfg.value("contentSplitter").toByteArray());
    if (cfg.contains("leftSplitter"))    m_leftSplitter->restoreState(cfg.value("leftSplitter").toByteArray());
    if (cfg.contains("rightSplitter"))   m_rightSplitter->restoreState(cfg.value("rightSplitter").toByteArray());
    if (cfg.contains("bottomBar"))       m_bottomBar->restoreState(cfg.value("bottomBar").toByteArray());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::connectExchangeSignals(IExchangeClient *ex)
{
    // Route raw market data into the model
    connect(ex, &IExchangeClient::orderBookUpdated,
            m_model, &MarketDataModel::applyOrderBookUpdate);
    connect(ex, &IExchangeClient::tradeReceived,
            m_model, &MarketDataModel::applyTrade);

    // Connection lifecycle stays in MainWindow (UI concern)
    connect(ex, &IExchangeClient::connected,     this, &MainWindow::onConnected);
    connect(ex, &IExchangeClient::disconnected,  this, &MainWindow::onDisconnected);
    connect(ex, &IExchangeClient::statusChanged, this, &MainWindow::onStatusChanged);
    connect(ex, &IExchangeClient::historicalCandlesReady,
            this, [this](const std::vector<Candle> &c) {
                if (m_mode == MarketMode::CS2)
                    m_cs2PriceChart->seedHistory(c);
                else
                    m_candle->seedCandles(c);
            });
}

// ── UI setup ───────────────────────────────────────────────────────────────────

void MainWindow::setupUi()
{
    setWindowTitle("Order Book Visualizer");
    setMinimumSize(1400, 920);
    setStyleSheet(
        "QMainWindow { background: #080c12; }"
        "QSplitter::handle { background: #1a2030; }"

"QPushButton#connectBtn {"
        "  background: #0f3d18; color: #4ac868;"
        "  border: 1px solid #1e6030; padding: 7px 22px;"
        "  border-radius: 4px; font-size: 17px; font-weight: 500; }"
        "QPushButton#connectBtn:hover { background: #145220; color: #6ae888; }"

        "QLabel { color: #c0ccd8; }"
    );

    // ── Toolbar ────────────────────────────────────────────────────────────────
    auto *toolbar = new QWidget();
    toolbar->setFixedHeight(60);
    toolbar->setStyleSheet("background: #050810; border-bottom: 1px solid #141c28;");

    auto *tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(16, 0, 16, 0);
    tbLayout->setSpacing(6);

    auto *titleLabel = new QLabel("ORDERBOOK");
    titleLabel->setStyleSheet(
        "color: #3a6090; font-size: 14px; font-weight: 700; letter-spacing: 2px; margin-right: 8px;");
    tbLayout->addWidget(titleLabel);

    // ── Exchange toggle buttons ────────────────────────────────────────────────
    m_exchangeGroup = new QButtonGroup(this);
    m_exchangeGroup->setExclusive(true);

    // Per-exchange brand colors: checked color / checked bg / checked border / hover color
    struct ExchInfo {
        const char       *label;
        IExchangeClient  *client;
        bool              isCS2;
        const char       *checkedCol;
        const char       *checkedBg;
        const char       *checkedBdr;
        const char       *hoverCol;
    };
    static const ExchInfo kExchanges[] = {
        {"BYBIT",      nullptr, false, "#e07030", "#1a0800", "#703010", "#a05020"},
        {"BINANCE.US", nullptr, false, "#d4a020", "#161000", "#705010", "#a07818"},
        {"CS2",        nullptr, true,  "#b878f8", "#1c0e38", "#6030c0", "#8850c8"},
    };

    static const QString kExchBase =
        "QPushButton#exchBtn {"
        "  background: #0f1520; color: #5a6a80;"
        "  border: 1px solid #1e2a3a; padding: 5px 14px;"
        "  border-radius: 4px; font-size: 13px; font-weight: 600; }";

    IExchangeClient *clients[] = { m_bybit, m_binance, m_cs2 };
    for (int i = 0; i < 3; ++i) {
        const ExchInfo &e = kExchanges[i];
        auto *btn = new QPushButton(e.label);
        btn->setObjectName("exchBtn");
        btn->setCheckable(true);
        btn->setStyleSheet(kExchBase
            + QString("QPushButton#exchBtn:checked { background:%1; color:%2; border-color:%3; }")
                .arg(e.checkedBg).arg(e.checkedCol).arg(e.checkedBdr)
            + QString("QPushButton#exchBtn:hover   { background:#141820; color:%1; }")
                .arg(e.hoverCol));
        m_exchangeGroup->addButton(btn);
        tbLayout->addWidget(btn);
        m_exchBtns[i] = btn;

        IExchangeClient *exPtr = clients[i];
        if (e.isCS2) {
            m_cs2Btn = btn;
            connect(btn, &QPushButton::clicked, this, [this]() {
                if (m_mode != MarketMode::CS2)
                    switchExchange(m_cs2);
            });
        } else {
            connect(btn, &QPushButton::clicked, this,
                    [this, exPtr]() { switchExchange(exPtr); });
        }
    }
    m_exchBtns[0]->setChecked(true);

    auto *sep0 = new QFrame();
    sep0->setFrameShape(QFrame::VLine);
    sep0->setFixedWidth(1); sep0->setFixedHeight(20);
    sep0->setStyleSheet("background: #1a2030;");
    tbLayout->addWidget(sep0);
    tbLayout->addSpacing(6);

    // ── Symbol combo box + ▾ button (visually fused, zero gap) ───────────────
    m_symbolCombo = new QComboBox();
    m_symbolCombo->setEditable(true);
    m_symbolCombo->setInsertPolicy(QComboBox::NoInsert);
    m_symbolCombo->setFixedWidth(110);
    m_symbolCombo->setFixedHeight(34);
    m_symbolCombo->addItems({
        "BTCUSDT", "ETHUSDT", "SOLUSDT", "XRPUSDT",
        "BNBUSDT", "DOGEUSDT", "ADAUSDT", "AVAXUSDT",
        "DOTUSDT", "LINKUSDT"
    });
    // Styled via applyExchangeAccent() — leave blank here, accent applied after buttons are built

    // ▾ button — stored so applyExchangeAccent() can recolor it
    m_comboArrow = new QPushButton("▾");
    m_comboArrow->setFixedSize(22, 34);
    m_comboArrow->setCursor(Qt::PointingHandCursor);
    auto *comboArrow = m_comboArrow;
    // Styled via applyExchangeAccent()
    connect(comboArrow, &QPushButton::clicked,
            m_symbolCombo, &QComboBox::showPopup);

    // Activate on dropdown selection or Enter key
    // switchSymbol() handles uppercasing for crypto vs. case-preservation for CS2
    connect(m_symbolCombo, QOverload<int>::of(&QComboBox::activated),
            this, [this]() {
        switchSymbol(m_symbolCombo->currentText().trimmed());
    });
    connect(m_symbolCombo->lineEdit(), &QLineEdit::returnPressed,
            this, [this]() {
        switchSymbol(m_symbolCombo->currentText().trimmed());
    });

    // Wrap both in a zero-spacing container so they look like one control
    m_comboWrap = new QWidget();
    m_comboWrap->setFixedHeight(34);
    auto *comboRow = new QHBoxLayout(m_comboWrap);
    comboRow->setContentsMargins(0, 0, 0, 0);
    comboRow->setSpacing(0);
    comboRow->addWidget(m_symbolCombo);
    comboRow->addWidget(comboArrow);

    tbLayout->addWidget(m_comboWrap);
    applyExchangeAccent(0);   // combo + arrow now exist — apply BYBIT accent
    tbLayout->addStretch();

    // ── Book imbalance bar ─────────────────────────────────────────────────────
    m_deltaBar = new DeltaBarWidget();
    tbLayout->addWidget(m_deltaBar);

    // ── Clock ──────────────────────────────────────────────────────────────────
    m_clockLabel = new QLabel();
    m_clockLabel->setStyleSheet(
        "color: #4a6880; font-family: Consolas; font-size: 17px; margin-right: 6px;");
    tbLayout->addWidget(m_clockLabel);

    auto *sep1 = new QFrame();
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFixedWidth(1); sep1->setFixedHeight(30);
    sep1->setStyleSheet("background: #1a2030;");
    tbLayout->addWidget(sep1);
    tbLayout->addSpacing(8);

    // ── Status / connect ───────────────────────────────────────────────────────
    m_statusLabel = new QLabel("Disconnected");
    m_statusLabel->setStyleSheet("color: #3a4a5a; font-size: 17px; margin-right: 12px;");

    m_connectBtn = new QPushButton("Connect");
    m_connectBtn->setObjectName("connectBtn");
    m_connectBtn->setStyleSheet(          // explicit initial style — don't rely on cascade
        "QPushButton { background:#0f3d18; color:#4ac868; border:1px solid #1e6030;"
        "  padding:7px 22px; border-radius:4px; font-size:17px; font-weight:500; }"
        "QPushButton:hover { background:#145220; color:#6ae888; }");
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);

    tbLayout->addWidget(m_statusLabel);
    tbLayout->addWidget(m_connectBtn);

    // ── Widgets ───────────────────────────────────────────────────────────────
    m_candle = new CandlestickWidget();
    connect(m_candle, &CandlestickWidget::intervalChanged, this, [this](int sec) {
        m_candleIntervalSec = sec;
        if (m_exchange->isConnected())
            m_exchange->fetchHistoricalCandles(m_currentSymbol, m_candleIntervalSec);
    });
    connect(m_candle, &CandlestickWidget::alertTriggered,
            this, &MainWindow::onAlertTriggered);

    m_ladder          = new OrderBookLadder();
    m_heatmap         = new HeatmapWidget();
    m_depthChart      = new DepthChartWidget();
    m_tape            = new TapeWidget();
    m_analyticsPanel  = new AnalyticsPanel();
    m_volProfile      = new VolumeProfileWidget();
    m_cs2PriceChart   = new CS2PriceChartWidget();
    m_cs2MarketStats  = new CS2MarketStatsWidget();
    m_cs2Marketplace  = new CS2MarketplaceWidget();

    // CS2-specific widgets are hidden until CS2 mode is entered
    m_cs2PriceChart->setVisible(false);
    m_cs2MarketStats->setVisible(false);
    m_cs2Marketplace->setVisible(false);

    // ── Layout tree (crypto mode) ──────────────────────────────────────────────
    //
    //  ┌─────────────────────────────────────┬──────────────┐
    //  │  CandlestickWidget                  │ OrderBook    │
    //  │                                     │ Ladder       │
    //  ├─────────────────────────────────────┤              │
    //  │  HeatmapWidget                      ├──────────────┤
    //  │                                     │ Time & Sales │
    //  ├──────────┬──────────────────────────┴──────────────┤
    //  │ Analytics│ Depth Chart     │ Volume Profile         │
    //  └──────────┴─────────────────┴────────────────────────┘
    //
    // CS2 mode: left = price chart, right = marketplace rows, bottom = market stats

    // Right column: crypto = [ladder, tape]; CS2 = [marketplace] (others hidden)
    m_rightSplitter = new QSplitter(Qt::Vertical);
    m_rightSplitter->addWidget(m_ladder);
    m_rightSplitter->addWidget(m_tape);
    m_rightSplitter->addWidget(m_cs2Marketplace);
    m_rightSplitter->setSizes({480, 340, 0});
    m_rightSplitter->setHandleWidth(1);
    m_rightSplitter->setMinimumWidth(180);

    // Left column: candlestick/heatmap (crypto) or price chart (CS2)
    m_leftSplitter = new QSplitter(Qt::Vertical);
    m_leftSplitter->addWidget(m_candle);
    m_leftSplitter->addWidget(m_heatmap);
    m_leftSplitter->addWidget(m_cs2PriceChart);
    m_leftSplitter->setSizes({560, 260, 0});
    m_leftSplitter->setHandleWidth(1);

    // Content row: left column | right column
    m_contentSplitter = new QSplitter(Qt::Horizontal);
    m_contentSplitter->addWidget(m_leftSplitter);
    m_contentSplitter->addWidget(m_rightSplitter);
    m_contentSplitter->setSizes({1020, 260});
    m_contentSplitter->setHandleWidth(1);

    // Bottom bar: analytics | depth chart | volume profile | CS2 market stats
    m_bottomBar = new QSplitter(Qt::Horizontal);
    m_bottomBar->addWidget(m_analyticsPanel);
    m_bottomBar->addWidget(m_depthChart);
    m_bottomBar->addWidget(m_volProfile);
    m_bottomBar->addWidget(m_cs2MarketStats);
    m_bottomBar->setSizes({680, 380, 220, 0});
    m_bottomBar->setHandleWidth(1);
    m_bottomBar->setMaximumHeight(140);
    m_bottomBar->setMinimumHeight(90);

    // Outer vertical split: content | bottom bar
    m_mainSplitter = new QSplitter(Qt::Vertical);
    m_mainSplitter->addWidget(m_contentSplitter);
    m_mainSplitter->addWidget(m_bottomBar);
    m_mainSplitter->setSizes({790, 110});
    m_mainSplitter->setHandleWidth(1);

    auto *central = new QWidget();
    auto *layout  = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(m_mainSplitter, 1);

    setCentralWidget(central);

    // ── Wire model → widgets ───────────────────────────────────────────────────
    // frameReady fires at render-timer rate (~30 fps), not at WebSocket rate
    connect(m_model, &MarketDataModel::frameReady,
            this, [this](const BookSnapshot &snap, qint64 ts,
                         const AnalyticsSnapshot &analytics)
    {
        m_ladder->setSnapshot(snap);
        m_heatmap->pushSnapshot(snap, ts);
        m_depthChart->setSnapshot(snap);
        m_analyticsPanel->update(analytics);
        m_deltaBar->setValue(analytics.orderFlowImbalance);
        m_volProfile->setSnapshot(snap);

        if (m_mode == MarketMode::CS2) {
            // In CS2 mode mid price = best ask; feed it to the price history chart
            if (snap.midPrice > 0)
                m_cs2PriceChart->addPricePoint(snap.midPrice, ts);
        } else {
            m_candle->addMidPrice(snap.midPrice, ts);
        }
    });

    // tradeArrived fires immediately on every tick
    connect(m_model, &MarketDataModel::tradeArrived,
            this, [this](const TradeInfo &trade)
    {
        m_tape->addTrade(trade);
        m_candle->addTrade(trade);
        m_volProfile->addTrade(trade);
        m_ladder->addTrade(trade);
    });
}

// ── Data ───────────────────────────────────────────────────────────────────────
// Raw data now flows:  exchange → MarketDataModel → widgets (via signals)
// MainWindow only drives the render-timer flush.

void MainWindow::onOrderBookUpdated(const OrderBookUpdate &) {}   // handled by model

void MainWindow::onTradeReceived(const TradeInfo &) {}   // handled by model

void MainWindow::renderFrame()
{
    m_model->flushFrame();   // emits frameReady() if a new update arrived
}

void MainWindow::updateClock()
{
    m_clockLabel->setText(QDateTime::currentDateTime().toString("HH:mm:ss  ddd dd MMM"));
}

// ── Connection ─────────────────────────────────────────────────────────────────

void MainWindow::onConnected()
{
    if (sender() != m_exchange) return;   // ignore background reconnects from inactive clients
    m_connectBtn->setText("Disconnect");
    m_connectBtn->setStyleSheet(
        "background: #2a0d0d; color: #e05050; border: 1px solid #4a1a1a;"
        "padding: 7px 22px; border-radius: 4px; font-size: 17px; font-weight: 500;");
    m_statusLabel->setStyleSheet("color: #3aaa58; font-size: 17px; margin-right: 12px;");
    setWindowTitle(QString("Order Book \xe2\x80\x94 %1  [%2]")
                   .arg(m_currentSymbol, m_exchange->exchangeName()));
    if (m_mode == MarketMode::CS2) {
        m_cs2MarketStats->setLoading(true);
        m_cs2Marketplace->setLoading(true);
        m_cs2PriceChart->setStatusMessage(
            QString("Loading %1 price history\xe2\x80\xa6")
            .arg(m_cs2HistoryMkt.toUpper()));
        const QString base = OpenSkinClient::stripWear(m_currentSymbol);
        m_openSkin->fetchAllWearPrices(base);
        m_openSkin->fetchHealth();
        // Lazy: fetch only active wear first; background-load the rest after it arrives
        std::fill(std::begin(m_wearHistoryRequested), std::end(m_wearHistoryRequested), false);
        m_wearHistoryRequested[m_cs2ActiveWearIdx] = true;
        m_openSkin->fetchWearHistory(base, m_cs2ActiveWearIdx, m_cs2HistoryMkt, "daily");
    } else {
        m_exchange->fetchHistoricalCandles(m_currentSymbol, m_candleIntervalSec);
    }
}

void MainWindow::onDisconnected()
{
    if (sender() != m_exchange) return;   // ignore background reconnects from inactive clients
    m_connectBtn->setText("Connect");
    m_connectBtn->setStyleSheet(
        "QPushButton { background:#0f3d18; color:#4ac868; border:1px solid #1e6030;"
        "  padding:7px 22px; border-radius:4px; font-size:17px; font-weight:500; }"
        "QPushButton:hover { background:#145220; color:#6ae888; }");
    m_statusLabel->setStyleSheet("color: #3a4a5a; font-size: 17px; margin-right: 12px;");
    m_model->reset();

    if (m_mode == MarketMode::CS2) {
        m_cs2MarketStats->setIdle();
        m_cs2Marketplace->setIdle();
        m_cs2PriceChart->clear();
        m_cs2PriceChart->setStatusMessage("Connect to view price history.");
    }

    if (!m_pendingSymbol.isEmpty()) {
        const QString sym = m_pendingSymbol;
        m_pendingSymbol.clear();
        m_exchange->connect(sym);
    }
}

void MainWindow::onStatusChanged(const QString &status)
{
    if (sender() != m_exchange) return;   // only the active client drives the status label
    m_statusLabel->setText(status);
    // Amber while connecting, gray for everything else (connected/disconnected have their own handlers)
    const bool connecting = status.contains("onnect", Qt::CaseInsensitive) && !m_exchange->isConnected();
    m_statusLabel->setStyleSheet(connecting
        ? "color:#c08020; font-size:17px; margin-right:12px;"
        : "color:#3a4a5a; font-size:17px; margin-right:12px;");
}

void MainWindow::onConnectClicked()
{
    if (m_exchange->isConnected())
        m_exchange->disconnect();
    else
        m_exchange->connect(m_currentSymbol);
}

// ── CS2 skin name helpers ─────────────────────────────────────────────────────

static QString stripWear(const QString &name)
{
    static const QStringList kSuffixes = {
        " (Factory New)", " (Minimal Wear)", " (Field-Tested)",
        " (Well-Worn)", " (Battle-Scarred)"
    };
    for (const QString &s : kSuffixes)
        if (name.endsWith(s)) return name.left(name.size() - s.size());
    return name;
}

// ── Symbol / exchange switching ────────────────────────────────────────────────

void MainWindow::switchSymbol(const QString &symbol)
{
    const QString normalised = (m_mode == MarketMode::CS2)
        ? symbol.trimmed()
        : symbol.toUpper().trimmed();

    if (normalised.isEmpty() || m_currentSymbol == normalised) return;
    m_currentSymbol = normalised;

    // Keep base skin in sync when the full name changes (e.g., from wear button click)
    if (m_mode == MarketMode::CS2) {
        m_cs2BaseSkin = stripWear(normalised);
        m_cs2PriceChart->setSkinName(normalised);
        m_cs2PriceChart->clear();
        m_cs2PriceChart->setStatusMessage("Connect to view price history.");
        m_cs2MarketStats->setSkinName(normalised);
        m_cs2Marketplace->setSkinName(normalised);

        if (m_exchange->isConnected()) {
            // Abort any in-flight requests from the previous skin before starting new ones
            m_openSkin->abortPendingPriceRequests();
            m_openSkin->abortPendingHistoryRequests();
            m_cs2MarketStats->setLoading(true);
            m_cs2Marketplace->setLoading(true);
            m_cs2PriceChart->setStatusMessage(
                QString("Loading %1 price history\xe2\x80\xa6").arg(m_cs2HistoryMkt.toUpper()));
            const QString base = OpenSkinClient::stripWear(normalised);
            m_openSkin->fetchAllWearPrices(base);
            // Lazy: load only active wear first
            std::fill(std::begin(m_wearHistoryRequested), std::end(m_wearHistoryRequested), false);
            m_wearHistoryRequested[m_cs2ActiveWearIdx] = true;
            m_openSkin->fetchWearHistory(base, m_cs2ActiveWearIdx, m_cs2HistoryMkt, "daily");
        } else {
            m_cs2MarketStats->setIdle();
            m_cs2Marketplace->setIdle();
        }

        // Show only the base name (no wear suffix) in the editable combo field
        if (m_symbolCombo->lineEdit()) {
            m_symbolCombo->lineEdit()->blockSignals(true);
            m_symbolCombo->lineEdit()->setText(m_cs2BaseSkin);
            m_symbolCombo->lineEdit()->blockSignals(false);
        }
    } else {
        // Sync combobox without triggering another switchSymbol
        m_symbolCombo->blockSignals(true);
        m_symbolCombo->setCurrentText(normalised);
        m_symbolCombo->blockSignals(false);
    }

    m_volProfile->clear();
    m_model->reset();

    if (m_exchange->isConnected()) {
        m_pendingSymbol = normalised;
        m_exchange->disconnect();
    }
}

void MainWindow::applyExchangeAccent(int idx)
{
    // accent: col / borderDim / borderBright / text
    static const char *kAccentCol[] = { "#e07030", "#d4a020", "#b878f8" };
    static const char *kAccentBdr[] = { "#502010", "#503810", "#401880" };
    static const char *kAccentBdrHov[] = { "#904020", "#806020", "#7040b0" };
    static const char *kAccentText[]   = { "#c06020", "#b08018", "#9060d8" };

    if (idx < 0 || idx > 2) return;
    const char *col    = kAccentCol[idx];
    const char *bdr    = kAccentBdr[idx];
    const char *bdrHov = kAccentBdrHov[idx];
    const char *txt    = kAccentText[idx];

    if (m_symbolCombo) {
        m_symbolCombo->setStyleSheet(
            QString("QComboBox {"
            "  background: #0f1520; color: %1;"
            "  border: 1px solid %2; border-right: none;"
            "  padding: 2px 6px 2px 8px;"
            "  border-top-left-radius: 4px; border-bottom-left-radius: 4px;"
            "  border-top-right-radius: 0; border-bottom-right-radius: 0;"
            "  font-size: 13px; font-weight: 600; }"
            "QComboBox:hover { background: #141c2c; color: %3; border-color: %4; }"
            "QComboBox::drop-down { border: none; width: 0; }"
            "QComboBox::down-arrow { width: 0; height: 0; }"
            "QComboBox QAbstractItemView {"
            "  background: #0d1520; border: 1px solid %2;"
            "  color: #8898aa; selection-background-color: #162030;"
            "  selection-color: #c0d0e0; outline: none; }")
            .arg(txt).arg(bdr).arg(col).arg(bdrHov));
    }
    if (m_comboArrow) {
        m_comboArrow->setStyleSheet(
            QString("QPushButton {"
            "  background: #0c1218; color: %1;"
            "  border: 1px solid %2;"
            "  border-top-right-radius: 4px; border-bottom-right-radius: 4px;"
            "  border-top-left-radius: 0; border-bottom-left-radius: 0;"
            "  font-size: 14px; padding: 0; margin: 0; }"
            "QPushButton:hover { background: #141c2c; color: %3; border-color: %4; }"
            "QPushButton:pressed { background: #0a0f18; }")
            .arg(txt).arg(bdr).arg(col).arg(bdrHov));
    }
}

void MainWindow::switchExchange(IExchangeClient *ex)
{
    if (ex == m_exchange) return;

    // Derive accent index directly from the exchange pointer — reliable even when
    // called programmatically (e.g. state restore) before buttons are checked.
    IExchangeClient *const kClients[] = { m_bybit, m_binance, m_cs2 };
    for (int i = 0; i < 3; ++i) {
        if (kClients[i] == ex) { applyExchangeAccent(i); break; }
    }

    const bool wasConnected = m_exchange->isConnected();
    m_pendingSymbol.clear();  // don't carry a pending symbol across exchange switches
    m_exchange->disconnect();
    m_exchange = ex;
    m_model->reset();
    m_volProfile->clear();

    if (ex == m_cs2)
        enterCS2Mode();
    else if (m_mode == MarketMode::CS2)
        exitCS2Mode();

    if (wasConnected)
        m_exchange->connect(m_currentSymbol);
}

// ── CS2 mode ─────────────────────────────────────────────────────────────────

void MainWindow::enterCS2Mode()
{
    m_savedCryptoSymbol = m_currentSymbol;
    m_mode = MarketMode::CS2;
    m_cs2HistoryMkt = "steam";
    applyExchangeAccent(2);   // CS2 purple — regardless of how we entered this mode

    // ── Show CS2-specific widgets ─────────────────────────────────────────────
    m_cs2PriceChart->setVisible(true);
    m_cs2MarketStats->setVisible(true);
    m_cs2Marketplace->setVisible(true);

    // ── Hide crypto-specific widgets ──────────────────────────────────────────
    m_candle->setVisible(false);
    m_heatmap->setVisible(false);
    m_ladder->setVisible(false);
    m_tape->setVisible(false);
    m_depthChart->setVisible(false);
    m_volProfile->setVisible(false);
    m_deltaBar->setVisible(false);
    m_analyticsPanel->setVisible(false);

    // Left: price chart (full height); Right: marketplace rows (full height)
    // Bottom: CS2 market stats
    m_leftSplitter->setSizes({0, 0, 820});
    m_rightSplitter->setSizes({0, 0, 820});
    m_bottomBar->setSizes({0, 0, 0, 1040});

    // Marketplace change on the price chart → track selection, fetch if connected
    connect(m_cs2PriceChart, &CS2PriceChartWidget::marketplaceChangeRequested,
            this, [this](const QString &mkt) {
                m_cs2HistoryMkt = mkt;
                if (!m_exchange->isConnected()) {
                    m_cs2PriceChart->setStatusMessage(
                        "Connect to view price history.");
                    return;
                }
                m_openSkin->abortPendingHistoryRequests();
                m_cs2PriceChart->setStatusMessage(
                    QString("Loading %1 price history\xe2\x80\xa6").arg(mkt.toUpper()));
                const QString base = OpenSkinClient::stripWear(m_currentSymbol);
                // Lazy: load only active wear first, background-load the rest
                std::fill(std::begin(m_wearHistoryRequested), std::end(m_wearHistoryRequested), false);
                m_wearHistoryRequested[m_cs2ActiveWearIdx] = true;
                m_openSkin->fetchWearHistory(base, m_cs2ActiveWearIdx, mkt, "daily");
            });

    // Helper lambda: convert history points to candles
    auto ptsToCandles = [](const QVector<OpenSkinHistoryPoint> &pts) {
        std::vector<Candle> out;
        out.reserve(static_cast<size_t>(pts.size()));
        for (const OpenSkinHistoryPoint &pt : pts) {
            Candle c;
            c.openTimeMs = pt.timestampMs;
            c.open = c.close = c.high = c.low = pt.price;
            c.volume   = pt.volume > 0 ? static_cast<double>(pt.volume) : 1.0;
            c.complete = true;
            out.push_back(c);
        }
        return out;
    };

    // ── OpenSkin: health ──────────────────────────────────────────────────────
    connect(m_openSkin, &OpenSkinClient::healthReady,
            this, [this](const OpenSkinHealth &health) {
                m_cs2MarketStats->setHealth(health);
            });

    // ── OpenSkin: per-wear prices ─────────────────────────────────────────────
    connect(m_openSkin, &OpenSkinClient::wearPricesReady,
            this, [this](int wearIdx, const OpenSkinPrices &prices) {
                m_cs2MarketStats->setWearPrices(wearIdx, prices);
                m_cs2Marketplace->setWearPrices(wearIdx, prices);
            });

    // Legacy single-item pricesReady kept for safety
    connect(m_openSkin, &OpenSkinClient::pricesReady,
            this, [this](const OpenSkinPrices &prices) {
                if (prices.item != m_currentSymbol) return;
                m_cs2MarketStats->setOpenSkinPrices(prices);
                m_cs2Marketplace->setOpenSkinPrices(prices);
            });

    // Per-wear history → seed the correct chart series
    connect(m_openSkin, &OpenSkinClient::wearHistoryReady,
            this, [this, ptsToCandles](int wearIdx, const QVector<OpenSkinHistoryPoint> &pts) {
                if (OpenSkinClient::stripWear(m_currentSymbol) != m_cs2BaseSkin) return;
                if (!pts.isEmpty()) {
                    const auto candles = ptsToCandles(pts);
                    if (!candles.empty())
                        m_cs2PriceChart->seedWearHistory(wearIdx, candles);
                }
                // Background-load next unloaded wear after a short delay
                QTimer::singleShot(300, this, &MainWindow::fetchNextWearHistory);
            });

    // Legacy single-item historyReady (not used by connect flow anymore, kept for safety)
    connect(m_openSkin, &OpenSkinClient::historyReady,
            this, [this, ptsToCandles](const QVector<OpenSkinHistoryPoint> &pts) {
                if (pts.isEmpty()) {
                    m_cs2PriceChart->setStatusMessage(
                        QString("No %1 price history available for this item.")
                        .arg(m_cs2HistoryMkt.toUpper()));
                    return;
                }
                const auto candles = ptsToCandles(pts);
                if (candles.empty()) {
                    m_cs2PriceChart->setStatusMessage("No valid price data returned.");
                    return;
                }
                m_cs2PriceChart->seedHistory(candles);
            });

    // Wear selector: update symbol and title (no API refetch — data pre-fetched for all wears)
    connect(m_cs2Marketplace, &CS2MarketplaceWidget::wearSelected,
            this, [this](const QString &wearFull) {
        m_cs2Wear = wearFull;
        const bool hasWear = m_cs2BaseSkin.contains(" | ") || m_cs2BaseSkin.startsWith("\xe2\x98\x85");
        m_currentSymbol = hasWear
            ? QString("%1 (%2)").arg(m_cs2BaseSkin, wearFull)
            : m_cs2BaseSkin;
        setWindowTitle(QString("Order Book \xe2\x80\x94 %1  [%2]")
                       .arg(m_currentSymbol, m_exchange->exchangeName()));
        m_cs2MarketStats->setSkinName(m_currentSymbol);
    });

    // Active wear change → sync stats widget and chart highlighted line
    connect(m_cs2Marketplace, &CS2MarketplaceWidget::activeWearChanged,
            this, [this](int wearIdx) {
        m_cs2ActiveWearIdx = wearIdx;
        m_cs2MarketStats->setActiveWear(wearIdx);
        m_cs2PriceChart->setActiveWear(wearIdx);
        // If this wear's history hasn't been fetched yet, request it immediately
        if (!m_wearHistoryRequested[wearIdx] && m_cs2PriceChart->isWearEmpty(wearIdx)
                && m_exchange->isConnected()) {
            m_wearHistoryRequested[wearIdx] = true;
            m_openSkin->fetchWearHistory(
                OpenSkinClient::stripWear(m_currentSymbol),
                wearIdx, m_cs2HistoryMkt, "daily");
        }
    });

    // Refresh button re-fetches all wear prices (only when connected)
    connect(m_cs2Marketplace, &CS2MarketplaceWidget::refreshRequested,
            this, [this]() {
        if (!m_exchange->isConnected()) return;
        m_cs2Marketplace->setLoading(true);
        m_cs2MarketStats->setLoading(true);
        const QString base = OpenSkinClient::stripWear(m_currentSymbol);
        m_openSkin->fetchAllWearPrices(base);
    });

    // ── Symbol combo: editable with OpenSkin item search ─────────────────────
    m_symbolCombo->setFixedWidth(300);
    m_comboWrap->setFixedWidth(322);
    m_symbolCombo->setEditable(true);
    m_symbolCombo->setInsertPolicy(QComboBox::NoInsert);

    // Seeds: base names only — no wear suffix. User picks wear via the wear buttons.
    static const QStringList kSeeds = {
        "AK-47 | Redline",
        "AK-47 | Vulcan",
        "AK-47 | Fire Serpent",
        "AK-47 | Case Hardened",
        "AWP | Dragon Lore",
        "AWP | Asiimov",
        "AWP | Lightning Strike",
        "AWP | Wildfire",
        "M4A4 | Howl",
        "M4A4 | The Emperor",
        "M4A1-S | Hot Rod",
        "Glock-18 | Fade",
        "Glock-18 | Water Elemental",
        "USP-S | Kill Confirmed",
        "Desert Eagle | Blaze",
        "\xe2\x98\x85 Karambit | Fade",
        "\xe2\x98\x85 Karambit | Tiger Tooth",
        "\xe2\x98\x85 Butterfly Knife | Fade",
        "\xe2\x98\x85 Butterfly Knife | Doppler",
        "\xe2\x98\x85 M9 Bayonet | Doppler",
    };

    m_symbolCombo->blockSignals(true);
    m_symbolCombo->clear();
    m_symbolCombo->addItems(kSeeds);
    m_symbolCombo->blockSignals(false);

    if (!m_cs2SearchModel)
        m_cs2SearchModel = new QStringListModel(this);
    m_cs2SearchModel->setStringList(kSeeds);

    auto *completer = new QCompleter(m_cs2SearchModel, m_symbolCombo);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setMaxVisibleItems(15);
    m_symbolCombo->setCompleter(completer);

    // On autocomplete pick: set base skin, build full name with current wear, switch
    connect(completer, QOverload<const QString &>::of(&QCompleter::activated),
            this, [this](const QString &s) {
        m_cs2BaseSkin = s;
        const bool hasWear = s.contains(" | ") || s.startsWith("\xe2\x98\x85");
        switchSymbol(hasWear ? QString("%1 (%2)").arg(s, m_cs2Wear) : s);
    });

    // On Enter: same treatment
    connect(m_symbolCombo->lineEdit(), &QLineEdit::returnPressed,
            this, [this]() {
        const QString s = m_symbolCombo->currentText().trimmed();
        if (s.isEmpty()) return;
        m_cs2BaseSkin = stripWear(s);
        const bool hasWear = m_cs2BaseSkin.contains(" | ") || m_cs2BaseSkin.startsWith("\xe2\x98\x85");
        switchSymbol(hasWear ? QString("%1 (%2)").arg(m_cs2BaseSkin, m_cs2Wear) : m_cs2BaseSkin);
    });

    // Debounce typing → OpenSkin item search → update completer
    m_cs2SearchTimer.setSingleShot(true);
    m_cs2SearchTimer.setInterval(350);
    connect(m_symbolCombo->lineEdit(), &QLineEdit::textEdited,
            &m_cs2SearchTimer, QOverload<>::of(&QTimer::start));
    connect(&m_cs2SearchTimer, &QTimer::timeout, this, [this]() {
        const QString q = m_symbolCombo->lineEdit()->text();
        if (q.length() >= 2) m_openSkin->searchItems(q);
    });

    // OpenSkin returns base names (no wear suffix) — load directly into completer
    connect(m_openSkin, &OpenSkinClient::searchResults,
            this, [this](const QStringList &names) {
        m_cs2SearchModel->setStringList(names);
    });

    // ── Restore last skin ─────────────────────────────────────────────────────
    QSettings cfg("OrderbookViz", "settings");
    // Default: Glock-18 | Fade (Factory New) — exists in FN and shows data well
    const QString kDefaultFull = "Glock-18 | Fade (Factory New)";
    QString lastFull = cfg.value("cs2Symbol", kDefaultFull).toString();

    m_cs2BaseSkin = stripWear(lastFull);

    // Reconstruct full name: if saved value had wear, extract it; otherwise use m_cs2Wear
    if (lastFull != m_cs2BaseSkin) {
        const int lp = lastFull.lastIndexOf('(');
        const int rp = lastFull.lastIndexOf(')');
        if (lp > 0 && rp > lp)
            m_cs2Wear = lastFull.mid(lp + 1, rp - lp - 1);
    } else {
        // Saved value had no wear suffix — reconstruct with current wear default
        lastFull = QString("%1 (%2)").arg(m_cs2BaseSkin, m_cs2Wear);
    }

    // Show base name (no wear) in combo edit field
    m_symbolCombo->lineEdit()->blockSignals(true);
    m_symbolCombo->lineEdit()->setText(m_cs2BaseSkin);
    m_symbolCombo->lineEdit()->blockSignals(false);

    m_currentSymbol = lastFull;  // full market hash name (with wear suffix)

    // Seed names; show idle state until user connects
    m_cs2PriceChart->setSkinName(lastFull);
    m_cs2PriceChart->setStatusMessage("Connect to view price history.");
    m_cs2MarketStats->setSkinName(lastFull);
    m_cs2MarketStats->setIdle();
    m_cs2Marketplace->setSkinName(lastFull);
    m_cs2Marketplace->setIdle();

    // Sync active wear across all widgets so onConnected() fetches history for the right wear
    static const char *kWearFulls[] = {
        "Factory New","Minimal Wear","Field-Tested","Well-Worn","Battle-Scarred"
    };
    for (int i = 0; i < 5; ++i) {
        if (m_cs2Wear == kWearFulls[i]) {
            m_cs2ActiveWearIdx = i;
            m_cs2Marketplace->setActiveWear(i);
            m_cs2PriceChart->setActiveWear(i);
            m_cs2MarketStats->setActiveWear(i);
            break;
        }
    }
}

void MainWindow::exitCS2Mode()
{
    QSettings cfg("OrderbookViz", "settings");
    cfg.setValue("cs2Symbol", m_currentSymbol);

    m_mode = MarketMode::Crypto;

    // Restore accent for whichever crypto exchange is now active
    IExchangeClient *const kClients[] = { m_bybit, m_binance, m_cs2 };
    for (int i = 0; i < 2; ++i) {   // only check crypto exchanges (0,1)
        if (kClients[i] == m_exchange) { applyExchangeAccent(i); break; }
    }

    // ── Disconnect CS2-specific signals ───────────────────────────────────────
    disconnect(m_openSkin, &OpenSkinClient::pricesReady,      this, nullptr);
    disconnect(m_openSkin, &OpenSkinClient::historyReady,     this, nullptr);
    disconnect(m_openSkin, &OpenSkinClient::wearPricesReady,  this, nullptr);
    disconnect(m_openSkin, &OpenSkinClient::wearHistoryReady, this, nullptr);
    disconnect(m_openSkin, &OpenSkinClient::searchResults,    this, nullptr);
    disconnect(m_openSkin, &OpenSkinClient::healthReady,      this, nullptr);
    disconnect(m_cs2PriceChart, &CS2PriceChartWidget::marketplaceChangeRequested, this, nullptr);
    disconnect(m_cs2Marketplace, &CS2MarketplaceWidget::wearSelected,     this, nullptr);
    disconnect(m_cs2Marketplace, &CS2MarketplaceWidget::activeWearChanged, this, nullptr);
    disconnect(m_cs2Marketplace, &CS2MarketplaceWidget::refreshRequested,  this, nullptr);

    // ── Restore crypto widgets ────────────────────────────────────────────────
    m_candle->setVisible(true);
    m_heatmap->setVisible(true);
    m_ladder->setVisible(true);
    m_tape->setVisible(true);
    m_depthChart->setVisible(true);
    m_volProfile->setVisible(true);
    m_deltaBar->setVisible(true);
    m_analyticsPanel->setVisible(true);
    m_analyticsPanel->setChartsVisible(true);

    // ── Hide CS2 widgets and clear their state ────────────────────────────────
    m_cs2PriceChart->setVisible(false);
    m_cs2PriceChart->clear();
    m_cs2MarketStats->setVisible(false);
    m_cs2MarketStats->clear();
    m_cs2Marketplace->setVisible(false);
    m_cs2Marketplace->clear();

    // Restore splitter sizes for crypto layout
    m_leftSplitter->setSizes({560, 260, 0});
    m_rightSplitter->setSizes({480, 340, 0});
    m_bottomBar->setSizes({680, 380, 220, 0});

    // ── Symbol combo: back to non-editable crypto pairs ──────────────────────
    m_cs2SearchTimer.stop();
    disconnect(&m_cs2SearchTimer, nullptr, this, nullptr);
    if (m_symbolCombo->lineEdit())
        disconnect(m_symbolCombo->lineEdit(), nullptr, this, nullptr);

    // Must clear completer BEFORE setEditable(false) — Qt forbids setting a
    // completer (even nullptr) on a non-editable combo and logs a warning.
    m_symbolCombo->setCompleter(nullptr);
    m_symbolCombo->setEditable(false);
    m_symbolCombo->setFixedWidth(110);
    m_comboWrap->setFixedWidth(132);

    static const QStringList kCryptoPairs = {
        "BTCUSDT", "ETHUSDT", "SOLUSDT", "XRPUSDT",
        "BNBUSDT", "DOGEUSDT", "ADAUSDT", "AVAXUSDT",
        "DOTUSDT", "LINKUSDT"
    };
    m_symbolCombo->blockSignals(true);
    m_symbolCombo->clear();
    m_symbolCombo->addItems(kCryptoPairs);
    m_symbolCombo->blockSignals(false);

    if (m_savedCryptoSymbol.isEmpty())
        m_savedCryptoSymbol = "BTCUSDT";
    m_currentSymbol = m_savedCryptoSymbol;
    m_symbolCombo->setCurrentText(m_currentSymbol);
}

// ── Alerts ────────────────────────────────────────────────────────────────────

void MainWindow::onAlertTriggered(double price)
{
    QApplication::beep();

    const QString msg = QString("ALERT \xe2\x96\xb6 %1").arg(price, 0, 'f', 2);
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet(
        "color: #ff8000; font-size: 12px; font-weight: bold; margin-right: 10px;");

    QTimer::singleShot(3000, this, [this]() {
        const bool con = m_exchange->isConnected();
        m_statusLabel->setStyleSheet(
            con ? "color: #3aaa58; font-size: 11px; margin-right: 10px;"
                : "color: #3a4a5a; font-size: 11px; margin-right: 10px;");
        m_statusLabel->setText(con ? "Connected" : "Disconnected");
    });
}

// ── Persistence ───────────────────────────────────────────────────────────────

void MainWindow::fetchNextWearHistory()
{
    if (!m_exchange || !m_exchange->isConnected()) return;
    if (OpenSkinClient::stripWear(m_currentSymbol) != m_cs2BaseSkin) return;

    const QString base = m_cs2BaseSkin;
    for (int i = 0; i < kWearCount; ++i) {
        if (!m_wearHistoryRequested[i]) {
            m_wearHistoryRequested[i] = true;
            m_openSkin->fetchWearHistory(base, i, m_cs2HistoryMkt, "daily");
            return;   // one at a time; next will be triggered by wearHistoryReady
        }
    }
    // All wears requested — nothing left to do
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    QSettings cfg("OrderbookViz", "settings");
    cfg.setValue("geometry",       saveGeometry());
    // Always save the *crypto* symbol, never the CS2 skin name.
    // If we're currently in CS2 mode, m_savedCryptoSymbol holds the last crypto pair.
    cfg.setValue("symbol", m_mode == MarketMode::CS2
        ? (m_savedCryptoSymbol.isEmpty() ? "BTCUSDT" : m_savedCryptoSymbol)
        : m_currentSymbol);
    cfg.setValue("candleInterval", m_candleIntervalSec);
    if (m_exchange == m_bybit)        cfg.setValue("exchange", "bybit");
    else if (m_exchange == m_binance) cfg.setValue("exchange", "binance");
    else                              cfg.setValue("exchange", "cs2");
    if (m_mode == MarketMode::CS2)
        cfg.setValue("cs2Symbol", m_currentSymbol);
    cfg.setValue("mainSplitter",    m_mainSplitter->saveState());
    cfg.setValue("contentSplitter", m_contentSplitter->saveState());
    cfg.setValue("leftSplitter",    m_leftSplitter->saveState());
    cfg.setValue("rightSplitter",   m_rightSplitter->saveState());
    cfg.setValue("bottomBar",       m_bottomBar->saveState());
    QMainWindow::closeEvent(e);
}
