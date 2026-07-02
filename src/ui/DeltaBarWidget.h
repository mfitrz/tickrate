#pragma once

#include <QWidget>

// Displays a smoothed bid/ask order-flow imbalance bar in the toolbar.
// setValue() accepts OFI in [-1, 1]: positive = bid-heavy, negative = ask-heavy.
class DeltaBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DeltaBarWidget(QWidget *parent = nullptr);
    void setValue(double ofi);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double m_smoothed = 0.0;
    bool   m_hasData  = false;
};
