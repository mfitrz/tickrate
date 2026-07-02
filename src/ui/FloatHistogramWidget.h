#pragma once
#include <QWidget>
#include <QVector>
#include "model/CS2Types.h"

class FloatHistogramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FloatHistogramWidget(QWidget *parent = nullptr);

    void setListings(const QVector<CS2Listing> &listings);
    void clear();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<CS2Listing> m_listings;
};
