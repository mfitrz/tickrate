#pragma once

#include <QWidget>
#include <QMouseEvent>
#include "core/Types.h"

class DepthChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DepthChartWidget(QWidget *parent = nullptr);
    void setSnapshot(const BookSnapshot &snap);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    static constexpr int kDisplayLevels = 20;

    BookSnapshot m_snap;
    QPoint       m_mousePos;
    bool         m_mouseIn = false;

    bool m_skelActive = true;
    void paintSkeleton(QPainter &p);
};
