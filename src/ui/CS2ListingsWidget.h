#pragma once
#include <QWidget>
#include <QVector>
#include <QFont>
#include <QColor>
#include "model/CS2Types.h"

class QPushButton;
class QLabel;

class CS2ListingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CS2ListingsWidget(QWidget *parent = nullptr);

    void setListings(const QVector<CS2Listing> &listings);
    void setHasMore(bool more);       // shows/hides Load More row at bottom
    void setLoadingMore(bool loading);
    void setActiveWear(int idx);      // 0=FN 1=MW 2=FT 3=WW 4=BS  -1=none
    void setEmptyReason(const QString &reason);
    void setLoading(bool loading);    // show skeleton rows while initial fetch is in flight
    void clear();

signals:
    void wearSelected(const QString &wearFull);
    void loadMoreRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private slots:
    void selectWear(int idx);
    void setSortMode(int mode);

private:
    QVector<CS2Listing>  m_listings;
    QVector<CS2Listing>  m_sorted;
    QString              m_emptyReason;
    int                  m_activeWear  = 2;
    int                  m_sortMode    = 0;
    int                  m_scrollOffset = 0;
    int                  m_hoverRow    = -1;
    bool                 m_hasMore     = false;
    bool                 m_loadingMore = false;
    bool                 m_loading     = false;
    bool                 m_hasIds      = false;   // tracks setCursor state
    QString              m_titleStr;              // cached; only rebuilt on wear change

    // Set during paintEvent so mousePressEvent knows if Load More row is visible
    mutable int          m_loadMoreRowY = -1;

    // Pre-formatted display strings for each sorted row, rebuilt in applySort()
    struct RowDisplay { QString priceStr; QString floatStr; QColor wearColor; QColor wearColorDark; };
    QVector<RowDisplay>  m_rowDisplay;

    // Cached fonts — constructed once
    QFont m_fTitle  { "Segoe UI",  9, QFont::Bold };
    QFont m_fBadge  { "Consolas",  7, QFont::Bold };
    QFont m_fColHdr { "Segoe UI",  9 };
    QFont m_fMono   { "Consolas", 11 };
    QFont m_fEmpty  { "Segoe UI", 11 };

    static constexpr int kFilterH = 66;
    static constexpr int kColH    = 22;
    static constexpr int kTitleH  = 18;
    static constexpr int kRowH    = 26;
    int headerBottom() const { return kFilterH + kTitleH + kColH; }

    QWidget     *m_filterBar    = nullptr;
    QPushButton *m_wearBtns[5]  = {};
    QPushButton *m_sortPriceBtn = nullptr;
    QPushButton *m_sortFloatBtn = nullptr;
    QLabel      *m_countLabel   = nullptr;

    void buildFilterBar();
    void applySort();
    int  maxScroll() const;
    int  rowAtY(int y) const;
    void openListingInBrowser(int row) const;
    void updateFilterBarGeometry();
    void updateSortButtons();
    void updateWearButtons();
};
