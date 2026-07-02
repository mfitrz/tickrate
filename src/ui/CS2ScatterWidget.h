#pragma once
#include <QWidget>
#include <QVector>
#include <QPoint>
#include "model/CS2Types.h"

// Float vs Price scatter plot for CS2 listings.
// Each dot represents one listing. X = float value, Y = price.
// Dots are color-coded by wear tier. Hover shows details; click opens CSFloat.
class CS2ScatterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CS2ScatterWidget(QWidget *parent = nullptr);
    void setListings(const QVector<CS2Listing> &listings);
    void clear();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QVector<CS2Listing> m_listings;
    int  m_hoverIdx = -1;

    static constexpr int   kDotR    = 5;
    static constexpr int   kPadL    = 58;
    static constexpr int   kPadR    = 12;
    static constexpr int   kPadT    = 28;
    static constexpr int   kPadB    = 32;

    // Map float/price to pixel coords given current rect
    QPointF toPixel(double floatVal, double price,
                    double minF, double maxF,
                    double minP, double maxP) const;
    int hitTest(const QPointF &pos,
                double minF, double maxF,
                double minP, double maxP) const;
    static QColor wearColor(const QString &wear);
};
