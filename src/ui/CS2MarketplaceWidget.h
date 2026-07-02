#pragma once
#include <QWidget>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include "network/OpenSkinClient.h"

class CS2MarketplaceWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CS2MarketplaceWidget(QWidget *parent = nullptr);

    // Per-wear prices from batch fetch; also updates button enabled states
    void setWearPrices(int wearIdx, const OpenSkinPrices &prices);
    // Legacy single-wear update (sets active wear's data)
    void setOpenSkinPrices(const OpenSkinPrices &prices);
    void setSkinName(const QString &name);
    void setActiveWear(int idx);        // 0=FN 1=MW 2=FT 3=WW 4=BS
    void setLoading(bool loading);
    void setIdle();
    void setStatusMessage(const QString &msg);
    void clear();

signals:
    void wearSelected(const QString &wearFull);   // legacy
    void activeWearChanged(int wearIdx);           // fired when wear button clicked
    void refreshRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void layoutWearButtons();

    // kWearCount is the global constant from OpenSkinClient.h (5)
    static constexpr int kMktCount  = 5;
    static constexpr int kHeaderH   = 58;
    static constexpr int kRowH      = 90;

    enum class State { Idle, Loading, Data, Status };
    State           m_state      = State::Idle;
    QString         m_skinName;
    QString         m_status;
    int             m_activeWear = 0;
    int             m_hoveredRow = -1;
    int             m_shimmer    = 0;   // 0–100, drives loading pulse
    QTimer          m_shimmerTimer;

    OpenSkinPrices  m_prices;
    OpenSkinPrices  m_wearPrices[kWearCount];   // cached per-wear prices

    QPushButton    *m_wearBtns[kWearCount] = {};
    QPushButton    *m_refreshBtn           = nullptr;
};
