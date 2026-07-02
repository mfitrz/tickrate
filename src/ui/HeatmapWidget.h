#pragma once

#include <QWidget>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <deque>
#include <vector>
#include "core/Types.h"

class HeatmapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HeatmapWidget(QWidget *parent = nullptr);
    void pushSnapshot(const BookSnapshot &snap, qint64 timestampMs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    static constexpr int kMaxHistory = 200;
    static constexpr int kDepth      = 50;

    std::deque<BookSnapshot> m_history;
    std::deque<qint64>       m_timestamps;

    double m_maxQty   = 1.0;
    double m_tickSize = 1.0;
    float  m_zoom     = 1.0f;

    QPoint m_mousePos;
    bool   m_mouseIn = false;

    double detectTickSize(const BookSnapshot &snap) const;
    QColor levelColor(double qty, bool isBid) const;

    bool m_skelActive = true;
    void paintSkeleton(QPainter &p);
};
