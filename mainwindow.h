#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QSplitter>
#include <QStringListModel>
#include <QTimer>
#include <QCloseEvent>
#include "src/network/IExchangeClient.h"
#include "src/network/BybitClient.h"
#include "src/network/BinanceClient.h"
#include "src/network/CS2MarketClient.h"
#include "src/network/OpenSkinClient.h"
#include "src/model/MarketDataModel.h"
#include "src/ui/HeatmapWidget.h"
#include "src/ui/AnalyticsPanel.h"
#include "src/ui/TapeWidget.h"
#include "src/ui/OrderBookLadder.h"
#include "src/ui/DepthChartWidget.h"
#include "src/ui/CandlestickWidget.h"
#include "src/ui/VolumeProfileWidget.h"
#include "src/ui/DeltaBarWidget.h"
#include "src/ui/CS2PriceChartWidget.h"
#include "src/ui/CS2MarketStatsWidget.h"
#include "src/ui/CS2MarketplaceWidget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

enum class MarketMode { Crypto, CS2 };

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onOrderBookUpdated(const OrderBookUpdate &update);
    void onTradeReceived(const TradeInfo &trade);
    void onConnected();
    void onDisconnected();
    void onStatusChanged(const QString &status);
    void onConnectClicked();
    void renderFrame();
    void updateClock();

private:
    Ui::MainWindow *ui;

    BybitClient     *m_bybit;
    BinanceClient   *m_binance;
    CS2MarketClient *m_cs2;
    OpenSkinClient  *m_openSkin;
    IExchangeClient *m_exchange;   // non-owning pointer to active client

    MarketDataModel *m_model;

    HeatmapWidget       *m_heatmap        = nullptr;
    AnalyticsPanel      *m_analyticsPanel = nullptr;
    TapeWidget          *m_tape           = nullptr;
    OrderBookLadder     *m_ladder         = nullptr;
    DepthChartWidget    *m_depthChart     = nullptr;
    CandlestickWidget   *m_candle         = nullptr;
    VolumeProfileWidget *m_volProfile     = nullptr;
    DeltaBarWidget      *m_deltaBar        = nullptr;
    CS2PriceChartWidget  *m_cs2PriceChart    = nullptr;
    CS2MarketStatsWidget *m_cs2MarketStats   = nullptr;
    CS2MarketplaceWidget *m_cs2Marketplace   = nullptr;

    QStringListModel *m_cs2SearchModel  = nullptr;
    QTimer            m_cs2SearchTimer;

    // Layout tree (all Qt::Horizontal/Vertical splitters):
    //  m_mainSplitter (V) → [m_contentSplitter, m_bottomBar]
    //  m_contentSplitter (H) → [m_leftSplitter, m_rightSplitter]
    //  m_leftSplitter (V) → [m_candle, m_heatmap]
    //  m_rightSplitter (V) → [m_ladder, m_tape]
    //  m_bottomBar (H) → [m_analyticsPanel, m_depthChart, m_volProfile]
    QSplitter *m_mainSplitter    = nullptr;
    QSplitter *m_contentSplitter = nullptr;
    QSplitter *m_leftSplitter    = nullptr;
    QSplitter *m_rightSplitter   = nullptr;
    QSplitter *m_bottomBar       = nullptr;

    QLabel       *m_statusLabel    = nullptr;
    QLabel       *m_clockLabel     = nullptr;
    QPushButton  *m_connectBtn     = nullptr;
    QPushButton  *m_cs2Btn         = nullptr;
    QPushButton  *m_exchBtns[3]    = {};   // [0]=BYBIT [1]=BINANCE.US [2]=CS2
    QPushButton  *m_comboArrow     = nullptr;
    QButtonGroup *m_exchangeGroup = nullptr;
    QComboBox    *m_symbolCombo   = nullptr;
    QWidget      *m_comboWrap = nullptr;   // combo + arrow container (for width resize)

    void applyExchangeAccent(int exchIdx);  // recolor combo to match active exchange
    QString       m_currentSymbol;
    QString       m_pendingSymbol;
    QString       m_savedCryptoSymbol;
    QString       m_cs2BaseSkin;
    QString       m_cs2Wear           = "Factory New";
    int           m_cs2ActiveWearIdx  = 0;   // index of currently selected wear
    QString       m_cs2HistoryMkt     = "steam";
    bool          m_wearHistoryRequested[kWearCount] = {};
    MarketMode    m_mode = MarketMode::Crypto;

    QTimer  m_renderTimer;
    QTimer  m_clockTimer;
    int     m_candleIntervalSec = 60;

    void setupUi();
    void connectExchangeSignals(IExchangeClient *ex);
    void switchSymbol(const QString &symbol);
    void switchExchange(IExchangeClient *ex);
    void enterCS2Mode();
    void exitCS2Mode();
    void onAlertTriggered(double price);
    void fetchNextWearHistory();   // background-load next unloaded wear, one at a time

protected:
    void closeEvent(QCloseEvent *e) override;
};

#endif // MAINWINDOW_H
