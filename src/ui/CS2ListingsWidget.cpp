#include "CS2ListingsWidget.h"

#include <QPainter>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWheelEvent>
#include <QDesktopServices>
#include <QUrl>
#include <algorithm>

static const char *kWears[]     = {"FN",           "MW",          "FT",           "WW",        "BS"};
static const char *kWearFull[]  = {"Factory New",  "Minimal Wear","Field-Tested", "Well-Worn", "Battle-Scarred"};
static const char *kWearColors[]= {"#60a8ff",      "#50d870",     "#e0b830",      "#e07020",   "#c04545"};

// Static wear-color table — avoids string comparisons per row in paint
static const QColor kWearColorTable[5] = {
    QColor(0x60, 0xa8, 0xff),   // FN
    QColor(0x50, 0xd8, 0x70),   // MW
    QColor(0xe0, 0xb8, 0x30),   // FT
    QColor(0xe0, 0x70, 0x20),   // WW
    QColor(0xc0, 0x45, 0x45),   // BS
};
// Pre-computed darker variants
static const QColor kWearColorDarkTable[5] = {
    QColor(0x4a, 0x84, 0xcb),
    QColor(0x3e, 0xa8, 0x58),
    QColor(0xaf, 0x8f, 0x25),
    QColor(0xaf, 0x57, 0x18),
    QColor(0x96, 0x35, 0x35),
};

CS2ListingsWidget::CS2ListingsWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(220);
    setMinimumHeight(200);
    setStyleSheet("background: #080c12;");
    setMouseTracking(true);

    // Apply letter-spacing to cached fonts that need it
    m_fTitle.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    m_fBadge.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    m_fColHdr.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);

    buildFilterBar();
    updateWearButtons();
}

void CS2ListingsWidget::buildFilterBar()
{
    m_filterBar = new QWidget(this);
    m_filterBar->setFixedHeight(kFilterH);
    m_filterBar->setStyleSheet("background: #050810; border-bottom: 1px solid #141c28;");

    auto *vlay = new QVBoxLayout(m_filterBar);
    vlay->setContentsMargins(8, 5, 8, 5);
    vlay->setSpacing(4);

    // ── Row 1: wear selector ──────────────────────────────────────────────────
    auto *row1  = new QWidget(m_filterBar);
    auto *r1lay = new QHBoxLayout(row1);
    r1lay->setContentsMargins(0, 0, 0, 0);
    r1lay->setSpacing(5);

    for (int i = 0; i < 5; ++i) {
        m_wearBtns[i] = new QPushButton(kWears[i], m_filterBar);
        m_wearBtns[i]->setCheckable(true);
        m_wearBtns[i]->setToolTip(QString("%1 — click to fetch these listings from CSFloat")
                                  .arg(kWearFull[i]));
        m_wearBtns[i]->setMinimumWidth(44);
        m_wearBtns[i]->setFixedHeight(24);
        static const char *kWearBdr[] = {"#1a3860","#1a4020","#504010","#502810","#481010"};
        static const char *kWearBg[]  = {"#081428","#081408","#1a1000","#180800","#140606"};
        static const char *kWearSel[] = {"#90c8ff","#80e890","#f0c840","#f08030","#e06060"};
        m_wearBtns[i]->setStyleSheet(QString(
            "QPushButton {"
            "  background:%2; color:%1; border:1px solid %3;"
            "  border-radius:3px; font-size:11px; font-weight:700; font-family:Consolas; }"
            "QPushButton:checked {"
            "  background:%2; color:%4; border:2px solid %1; }"
            "QPushButton:hover { background:#141e2c; color:%4; }")
            .arg(kWearColors[i]).arg(kWearBg[i]).arg(kWearBdr[i]).arg(kWearSel[i]));
        const int idx = i;
        connect(m_wearBtns[i], &QPushButton::clicked, this, [this, idx]() {
            selectWear(idx);
        });
        r1lay->addWidget(m_wearBtns[i]);
    }
    r1lay->addStretch();
    vlay->addWidget(row1);

    // ── Row 2: sort buttons + count ───────────────────────────────────────────
    auto *row2  = new QWidget(m_filterBar);
    auto *r2lay = new QHBoxLayout(row2);
    r2lay->setContentsMargins(0, 0, 0, 0);
    r2lay->setSpacing(5);

    auto makeSortBtn = [&](const QString &label) {
        auto *btn = new QPushButton(label, m_filterBar);
        btn->setCheckable(true);
        btn->setMinimumWidth(82);
        btn->setFixedHeight(22);
        btn->setStyleSheet(
            "QPushButton {"
            "  background:#0c1220; color:#3a5070; border:1px solid #1a2030;"
            "  border-radius:3px; font-size:10px; font-family:Consolas; padding:0 6px; }"
            "QPushButton:checked { background:#0e1a2a; color:#78a0c8; border-color:#2a4060; }"
            "QPushButton:hover { background:#101822; }");
        return btn;
    };

    m_sortPriceBtn = makeSortBtn("PRICE \xe2\x86\x91");
    m_sortFloatBtn = makeSortBtn("FLOAT \xe2\x86\x91");
    m_sortPriceBtn->setChecked(true);

    connect(m_sortPriceBtn, &QPushButton::clicked, this, [this]() {
        setSortMode(m_sortMode == 0 ? 2 : 0);
    });
    connect(m_sortFloatBtn, &QPushButton::clicked, this, [this]() {
        setSortMode(m_sortMode == 1 ? 3 : 1);
    });
    r2lay->addWidget(m_sortPriceBtn);
    r2lay->addWidget(m_sortFloatBtn);
    r2lay->addStretch();

    m_countLabel = new QLabel("0 listings", m_filterBar);
    m_countLabel->setStyleSheet("color:#3a5470; font-size:10px; font-family:Consolas;");
    r2lay->addWidget(m_countLabel);

    vlay->addWidget(row2);
    updateFilterBarGeometry();
}

void CS2ListingsWidget::updateFilterBarGeometry()
{
    if (m_filterBar)
        m_filterBar->setGeometry(0, 0, width(), kFilterH);
}

// ── Public API ────────────────────────────────────────────────────────────────

void CS2ListingsWidget::setListings(const QVector<CS2Listing> &listings)
{
    m_loading = false;
    m_listings = listings;
    if (!listings.isEmpty())
        m_emptyReason.clear();
    m_scrollOffset = 0;
    applySort();
}

void CS2ListingsWidget::setLoading(bool loading)
{
    if (m_loading == loading) return;
    m_loading = loading;
    if (loading) {
        m_listings.clear();
        m_sorted.clear();
        m_emptyReason.clear();
        m_scrollOffset = 0;
    }
    update();
}

void CS2ListingsWidget::setHasMore(bool more)
{
    if (m_hasMore == more) return;
    m_hasMore = more;
    update();
}

void CS2ListingsWidget::setLoadingMore(bool loading)
{
    if (m_loadingMore == loading) return;
    m_loadingMore = loading;
    update();
}

void CS2ListingsWidget::setEmptyReason(const QString &reason)
{
    m_loading = false;
    m_emptyReason = reason;
    m_listings.clear();
    m_sorted.clear();
    m_scrollOffset = 0;
    m_hoverRow = -1;
    if (m_countLabel) m_countLabel->setText("0 listings");
    update();
}

void CS2ListingsWidget::setActiveWear(int idx)
{
    m_activeWear = idx;
    // Rebuild cached title string — only changes on wear selection
    if (m_activeWear >= 0 && m_activeWear < 5)
        m_titleStr = QString("LISTINGS  \xe2\x80\x94  %1").arg(kWears[m_activeWear]);
    else
        m_titleStr = "LISTINGS";
    updateWearButtons();
}

void CS2ListingsWidget::clear()
{
    m_loading = false;
    m_listings.clear();
    m_sorted.clear();
    m_emptyReason.clear();
    m_scrollOffset = 0;
    m_hoverRow = -1;
    m_hasMore = false;
    m_loadingMore = false;
    m_loadMoreRowY = -1;
    if (m_countLabel) m_countLabel->setText("0 listings");
    update();
}

// ── Wear / sort ───────────────────────────────────────────────────────────────

void CS2ListingsWidget::selectWear(int idx)
{
    m_activeWear = idx;
    updateWearButtons();
    emit wearSelected(kWearFull[idx]);
}

void CS2ListingsWidget::setSortMode(int mode)
{
    m_sortMode = mode;
    updateSortButtons();
    applySort();
}

void CS2ListingsWidget::updateWearButtons()
{
    for (int i = 0; i < 5; ++i) {
        if (m_wearBtns[i]) {
            m_wearBtns[i]->blockSignals(true);
            m_wearBtns[i]->setChecked(i == m_activeWear);
            m_wearBtns[i]->blockSignals(false);
        }
    }
}

void CS2ListingsWidget::updateSortButtons()
{
    m_sortPriceBtn->blockSignals(true);
    m_sortFloatBtn->blockSignals(true);

    m_sortPriceBtn->setChecked(m_sortMode == 0 || m_sortMode == 2);
    m_sortFloatBtn->setChecked(m_sortMode == 1 || m_sortMode == 3);

    const QString up   = " \xe2\x86\x91";
    const QString down = " \xe2\x86\x93";
    m_sortPriceBtn->setText(QString("PRICE") + (m_sortMode == 2 ? down : up));
    m_sortFloatBtn->setText(QString("FLOAT") + (m_sortMode == 3 ? down : up));

    m_sortPriceBtn->blockSignals(false);
    m_sortFloatBtn->blockSignals(false);
}

void CS2ListingsWidget::applySort()
{
    m_sorted = m_listings;

    std::stable_sort(m_sorted.begin(), m_sorted.end(),
        [this](const CS2Listing &a, const CS2Listing &b) {
            switch (m_sortMode) {
                case 0: return a.price < b.price;
                case 1: return a.floatValue < b.floatValue;
                case 2: return a.price > b.price;
                case 3: return a.floatValue > b.floatValue;
            }
            return a.price < b.price;
        });

    // Pre-format strings and pre-compute colors so paintEvent does zero allocation per row
    m_rowDisplay.resize(m_sorted.size());
    for (int i = 0; i < m_sorted.size(); ++i) {
        const CS2Listing &l = m_sorted[i];
        m_rowDisplay[i].priceStr = QString("$%1").arg(l.price, 0, 'f', 2);
        m_rowDisplay[i].floatStr = QString::number(l.floatValue, 'f', 4);
        // Map wear short-string to color table index
        int wi = 0;
        if      (l.wear == "FN") wi = 0;
        else if (l.wear == "MW") wi = 1;
        else if (l.wear == "FT") wi = 2;
        else if (l.wear == "WW") wi = 3;
        else if (l.wear == "BS") wi = 4;
        m_rowDisplay[i].wearColor     = kWearColorTable[wi];
        m_rowDisplay[i].wearColorDark = kWearColorDarkTable[wi];
    }

    // Update cursor state once — not per paint
    const bool hasIds = !m_sorted.isEmpty() && !m_sorted[0].listingId.isEmpty();
    if (hasIds != m_hasIds) {
        m_hasIds = hasIds;
        setCursor(hasIds ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    m_scrollOffset = std::min(m_scrollOffset, maxScroll());
    if (m_countLabel) {
        const int n = m_sorted.size();
        QString txt = QString("%1 listing%2").arg(n).arg(n == 1 ? "" : "s");
        if (m_hasMore) txt += "+";
        m_countLabel->setText(txt);
    }
    update();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

int CS2ListingsWidget::maxScroll() const
{
    const int visible = (height() - headerBottom()) / kRowH;
    return std::max(0, static_cast<int>(m_sorted.size()) - visible);
}

int CS2ListingsWidget::rowAtY(int y) const
{
    if (y < headerBottom()) return -1;
    const int idx = (y - headerBottom()) / kRowH + m_scrollOffset;
    return (idx >= 0 && idx < m_sorted.size()) ? idx : -1;
}

void CS2ListingsWidget::openListingInBrowser(int row) const
{
    if (row < 0 || row >= m_sorted.size()) return;
    const CS2Listing &l = m_sorted[row];
    if (!l.listingId.isEmpty())
        QDesktopServices::openUrl(QUrl(QString("https://csfloat.com/item/%1").arg(l.listingId)));
}

// ── Events ────────────────────────────────────────────────────────────────────

void CS2ListingsWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    updateFilterBarGeometry();
    m_scrollOffset = std::min(m_scrollOffset, maxScroll());
}

void CS2ListingsWidget::wheelEvent(QWheelEvent *e)
{
    const int delta = e->angleDelta().y() > 0 ? -1 : 1;
    m_scrollOffset = std::clamp(m_scrollOffset + delta, 0, maxScroll());
    update();
    e->accept();
}

void CS2ListingsWidget::mouseMoveEvent(QMouseEvent *e)
{
    const int row = rowAtY(e->pos().y());
    if (row != m_hoverRow) {
        m_hoverRow = row;
        update();
    }
}

void CS2ListingsWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    const int y = e->pos().y();
    const int row = rowAtY(y);
    if (row >= 0) {
        openListingInBrowser(row);
        return;
    }
    // Check Load More row
    if (m_loadMoreRowY >= 0 && y >= m_loadMoreRowY && y < m_loadMoreRowY + kRowH) {
        if (!m_loadingMore)
            emit loadMoreRequested();
    }
}

// ── Paint ─────────────────────────────────────────────────────────────────────

static QColor wearColor(const QString &wear)
{
    if (wear == "FN") return QColor(0x60, 0xa8, 0xff);
    if (wear == "MW") return QColor(0x50, 0xd8, 0x70);
    if (wear == "FT") return QColor(0xe0, 0xb8, 0x30);
    if (wear == "WW") return QColor(0xe0, 0x70, 0x20);
    return QColor(0xc0, 0x45, 0x45);
}

void CS2ListingsWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(0x08, 0x0c, 0x12));

    const int w = width();
    const int h = height();

    // ── Title + CSFloat branding ──────────────────────────────────────────────
    const int titleY = kFilterH;
    p.fillRect(0, titleY, w, kTitleH + kColH, QColor(0x05, 0x08, 0x10));

    p.setFont(m_fTitle);
    p.setPen(QColor(0x38, 0x50, 0x68));
    p.drawText(QRect(0, titleY, w - 68, kTitleH), Qt::AlignCenter,
               m_titleStr.isEmpty() ? "LISTINGS" : m_titleStr);

    p.setFont(m_fBadge);
    p.setPen(QColor(0x2a, 0x5a, 0x9a));
    p.drawText(QRect(w - 66, titleY, 62, kTitleH),
               Qt::AlignRight | Qt::AlignVCenter, "via CSFloat");

    // ── Column headers ────────────────────────────────────────────────────────
    const int priceW = w * 38 / 100;
    const int floatW = w * 42 / 100;
    const int wearW  = w - priceW - floatW;
    const int colY   = titleY + kTitleH;

    p.setFont(m_fColHdr);
    p.setPen(QColor(0x30, 0x42, 0x58));
    p.drawText(QRect(6,           colY, priceW-6,  kColH), Qt::AlignLeft|Qt::AlignVCenter,   "PRICE");
    p.drawText(QRect(priceW+2,    colY, floatW-4,  kColH), Qt::AlignCenter|Qt::AlignVCenter, "FLOAT");
    p.drawText(QRect(priceW+floatW, colY, wearW-2, kColH), Qt::AlignCenter|Qt::AlignVCenter, "WEAR");
    p.setPen(QColor(0x0e, 0x16, 0x22));
    p.drawLine(0, colY + kColH - 1, w, colY + kColH - 1);

    // ── Loading skeleton (static — no flashing) ───────────────────────────────
    if (m_loading) {
        // Alternating row background tones, subtle enough not to distract
        static const QColor kSkelEven(0x0d, 0x14, 0x20);
        static const QColor kSkelOdd (0x0f, 0x17, 0x24);
        // Placeholder bar: slightly lighter than the row background
        static const QColor kSkelBar (0x17, 0x22, 0x34);
        // Vary widths so rows look organic, not mechanical
        static const int kPriceWs[] = {46, 58, 52, 62, 44, 55, 60, 48, 54, 50, 56, 42};
        static const int kFloatWs[] = {52, 44, 60, 38, 56, 48, 42, 58, 46, 54, 40, 50};
        const int barTop = 8, barH = kRowH - 16;
        const int visRows = std::min(12, (h - headerBottom()) / kRowH);
        int y = headerBottom();
        for (int i = 0; i < visRows && y + kRowH <= h; ++i) {
            p.fillRect(0, y, w, kRowH, i % 2 == 0 ? kSkelEven : kSkelOdd);
            p.fillRect(6,             y + barTop, kPriceWs[i % 12], barH, kSkelBar);
            p.fillRect(priceW + 4,    y + barTop, kFloatWs[i % 12], barH, kSkelBar);
            p.fillRect(priceW + floatW + 5, y + barTop, 22, barH, kSkelBar);
            p.setPen(QColor(0x0b, 0x11, 0x1b));
            p.drawLine(0, y + kRowH - 1, w, y + kRowH - 1);
            y += kRowH;
        }
        m_loadMoreRowY = -1;
        return;
    }

    // ── Empty state ───────────────────────────────────────────────────────────
    if (m_sorted.isEmpty()) {
        const QRect emptyRect(8, headerBottom(), w - 16, h - headerBottom());
        p.setFont(m_fEmpty);
        if (!m_emptyReason.isEmpty()) {
            p.setPen(QColor(0xc0, 0x70, 0x30));
            p.drawText(emptyRect, Qt::AlignCenter | Qt::TextWordWrap, m_emptyReason);
        } else {
            p.setPen(QColor(40, 52, 68));
            p.drawText(emptyRect, Qt::AlignCenter, "Waiting for data\xe2\x80\xa6");
        }
        m_loadMoreRowY = -1;
        return;
    }

    // ── Rows ──────────────────────────────────────────────────────────────────
    static const QColor kRowEven(0x08, 0x0c, 0x12);
    static const QColor kRowOdd (0x0a, 0x0e, 0x16);
    static const QColor kRowHov (0x14, 0x1e, 0x2e);
    static const QColor kRowTop (0x1a, 0x07, 0x07);
    static const QColor kDivider(0x0e, 0x14, 0x1e);
    static const QColor kPriceTop(0xff, 0x72, 0x72);
    static const QColor kPriceNorm(0xcc, 0x60, 0x60);

    p.setFont(m_fMono);
    int y = headerBottom();
    for (int i = m_scrollOffset; i < m_sorted.size() && y + kRowH <= h; ++i) {
        const CS2Listing  &l   = m_sorted[i];
        const RowDisplay  &rd  = m_rowDisplay[i];
        const bool         top = (i == m_scrollOffset);
        const bool         hov = (i == m_hoverRow);

        p.fillRect(0, y, w, kRowH, hov ? kRowHov : top ? kRowTop
                                       : (i % 2 == 0 ? kRowEven : kRowOdd));

        p.setPen(top ? kPriceTop : kPriceNorm);
        p.drawText(QRect(6, y, priceW-6, kRowH), Qt::AlignLeft|Qt::AlignVCenter, rd.priceStr);

        p.setPen(rd.wearColor);
        p.drawText(QRect(priceW+2, y, floatW-6, kRowH), Qt::AlignRight|Qt::AlignVCenter, rd.floatStr);

        p.setPen(rd.wearColorDark);
        p.drawText(QRect(priceW+floatW+1, y, wearW-2, kRowH), Qt::AlignCenter|Qt::AlignVCenter, l.wear);

        p.setPen(kDivider);
        p.drawLine(0, y + kRowH - 1, w, y + kRowH - 1);
        y += kRowH;
    }

    // ── Load More / Loading row ───────────────────────────────────────────────
    m_loadMoreRowY = -1;

    if (y + kRowH <= h) {
        if (m_loadingMore) {
            p.fillRect(0, y, w, kRowH, QColor(0x08, 0x0e, 0x18));
            p.setFont(m_fColHdr);
            p.setPen(QColor(0x30, 0x50, 0x78));
            p.drawText(QRect(0, y, w, kRowH), Qt::AlignCenter, "Loading more listings\xe2\x80\xa6");
        } else if (m_hasMore) {
            m_loadMoreRowY = y;
            p.fillRect(0, y, w, kRowH, QColor(0x08, 0x10, 0x20));
            p.setFont(m_fTitle);
            p.setPen(QColor(0x2a, 0x60, 0xaa));
            p.drawText(QRect(0, y, w, kRowH), Qt::AlignCenter,
                       "\xe2\x86\x93  LOAD MORE LISTINGS  \xe2\x86\x93");
            p.setPen(QColor(0x14, 0x24, 0x3c));
            p.drawLine(0, y + kRowH - 1, w, y + kRowH - 1);
        }
    }

    // ── Scroll indicator ──────────────────────────────────────────────────────
    if (maxScroll() > 0) {
        const int listH = h - headerBottom();
        const int barH  = std::max(24, listH * (listH / kRowH) / static_cast<int>(m_sorted.size()));
        const int barY  = headerBottom() + (listH - barH) * m_scrollOffset / maxScroll();
        p.fillRect(w - 4, barY, 3, barH, QColor(0x22, 0x38, 0x55));
    }
}
