#pragma once
#include <QWidget>
#include <QTimer>
#include <QFont>
#include "network/OpenSkinClient.h"

class CS2MarketStatsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CS2MarketStatsWidget(QWidget *parent = nullptr);
    void setSkinName(const QString &name);
    void setStatusMessage(const QString &msg);
    void setOpenSkinPrices(const OpenSkinPrices &prices);
    // Per-wear prices from batch fetch
    void setWearPrices(int wearIdx, const OpenSkinPrices &prices);
    // Switch displayed wear (reads from cached m_wearPrices)
    void setActiveWear(int wearIdx);
    void setHealth(const OpenSkinHealth &health);
    void setLoading(bool loading);
    void setIdle();
    void clear();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void paintIdle(QPainter &p, const QRect &body) const;
    void paintLoading(QPainter &p, const QRect &body) const;
    void paintArbitrage(QPainter &p, const QRect &r) const;
    void paintOrderDepth(QPainter &p, const QRect &r) const;
    void paintVolatility(QPainter &p, const QRect &r) const;
    void paintPriceImpact(QPainter &p, const QRect &r) const;

    enum class State { Idle, Loading, Data, Status };
    State          m_state     = State::Idle;
    QString        m_skinName;
    QString        m_status;
    OpenSkinPrices m_prices;
    OpenSkinPrices m_wearPrices[kWearCount];
    OpenSkinHealth m_health;
    int            m_activeWear  = 0;
    int            m_shimmer     = 0;
    QTimer         m_shimmerTimer;

    // Cached fonts — constructed once, reused every paint call
    QFont m_fTitle   { "Segoe UI",  12, QFont::Bold };
    QFont m_fUpd     { "Consolas",  12 };
    QFont m_fBadge   { "Consolas",  13 };
    QFont m_fHdr     { "Segoe UI",  11, QFont::Bold };
    QFont m_fVal     { "Consolas",  14, QFont::Bold };
    QFont m_fSub     { "Consolas",  11 };
    QFont m_fSmall   { "Consolas",  10 };
    QFont m_fLbl     { "Consolas",  11 };
    QFont m_fValBold { "Consolas",  11, QFont::Bold };
    QFont m_fSubHdr  { "Consolas",   9, QFont::Bold };
    QFont m_fQty     { "Consolas",  13, QFont::Bold };
    QFont m_fPct     { "Consolas",  12, QFont::Bold };
};
