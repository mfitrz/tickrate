#include "CS2MarketplaceWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QFont>
#include <algorithm>

// ── Marketplace metadata ──────────────────────────────────────────────────────

struct MktInfo {
    const char *label;
    const char *key;       // OpenSkin market key
    QColor      accent;
    const char *urlPattern;  // %1 = URL-encoded item name
};

static const MktInfo kMarkets[5] = {
    {"STEAM",    "steam",    QColor(0x4a, 0x8c, 0xd8),
     "https://steamcommunity.com/market/listings/730/%1"},
    {"BUFF163",  "buff",     QColor(0xf0, 0xa0, 0x20),
     "https://buff.163.com/market/goods?game=csgo#game=csgo&page_num=1&search=%1"},
    {"SKINPORT", "skinport", QColor(0xa0, 0x60, 0xd0),
     "https://skinport.com/market?search=%1"},
    {"YOUPIN",   "youpin",   QColor(0xe0, 0x50, 0x50),
     "https://youpin898.com/market?keywords=%1"},
    {"CSFLOAT",  "csfloat",  QColor(0x20, 0xc8, 0xa0),
     "https://csfloat.com/db?name=%1"},
};

static const char *kWearFullNames[5] = {
    "Factory New", "Minimal Wear", "Field-Tested", "Well-Worn", "Battle-Scarred"
};
static const char *kWearAbbrevs[5] = { "FN", "MW", "FT", "WW", "BS" };

static QString fmtPrice(double p)
{
    if (p <= 0) return "--";
    return QString("$%1").arg(p, 0, 'f', p >= 1000 ? 0 : 2);
}

// ── Construction ──────────────────────────────────────────────────────────────

CS2MarketplaceWidget::CS2MarketplaceWidget(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet("background:#06090f;");
    setMinimumWidth(200);
    setMouseTracking(true);

    m_shimmerTimer.setInterval(40);
    connect(&m_shimmerTimer, &QTimer::timeout, this, [this]() {
        m_shimmer = (m_shimmer + 4) % 200;
        update();
    });

    // Per-wear colors matching the chart line colours
    static const char *kWearClr[kWearCount]    = { "#40ffa0", "#60c0ff", "#ffd040", "#ff8040", "#ff5050" };
    static const char *kWearDimBg[kWearCount]  = { "#061810", "#061420", "#181200", "#140a00", "#120606" };
    static const char *kWearBrightBg[kWearCount] = { "#0a2818", "#0a1c30", "#221a00", "#1e0e00", "#1e0808" };

    for (int i = 0; i < kWearCount; ++i) {
        const QString style = QString(
            "QPushButton {"
            "  background:#0a1020; color:#304860; border:2px solid #141e2e;"
            "  border-radius:4px; font-size:15px; font-family:Consolas; font-weight:700; }"
            "QPushButton:checked {"
            "  background:%1; color:%2; border-color:%2; border-width:2px; }"
            "QPushButton:hover   { background:%3; color:%2; }"
            "QPushButton:disabled { background:#07090e; color:#1a2230; border-color:#090d16; }")
            .arg(kWearBrightBg[i], kWearClr[i], kWearDimBg[i]);

        m_wearBtns[i] = new QPushButton(kWearAbbrevs[i], this);
        m_wearBtns[i]->setCheckable(true);
        m_wearBtns[i]->setFixedHeight(38);
        m_wearBtns[i]->setStyleSheet(style);
        m_wearBtns[i]->setCursor(Qt::PointingHandCursor);
        const int idx = i;
        connect(m_wearBtns[i], &QPushButton::clicked, this, [this, idx]() {
            m_activeWear = idx;
            for (int j = 0; j < kWearCount; ++j)
                m_wearBtns[j]->setChecked(j == idx);
            // Use cached prices — no API refetch
            const auto &wp = m_wearPrices[idx];
            const bool hasData = wp.steam.valid || wp.skinport.valid
                              || wp.buff.valid   || wp.csfloat.valid
                              || wp.youpin.valid;
            if (hasData) {
                m_prices = wp;
                m_state  = State::Data;
            }
            emit wearSelected(kWearFullNames[idx]);
            emit activeWearChanged(idx);
            update();
        });
    }
    m_wearBtns[m_activeWear]->setChecked(true);

    m_refreshBtn = new QPushButton("REFRESH", this);
    m_refreshBtn->setFixedHeight(38);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setToolTip("Refresh all wear prices from API");
    m_refreshBtn->setStyleSheet(
        "QPushButton { background:#0a1a10; color:#207840; border:2px solid #0e2818;"
        "  border-radius:4px; font-size:10px; font-family:Consolas; font-weight:700; }"
        "QPushButton:hover { background:#0d2818; color:#30cc60; border-color:#186030; }");
    connect(m_refreshBtn, &QPushButton::clicked, this, &CS2MarketplaceWidget::refreshRequested);
}

// ── Public API ────────────────────────────────────────────────────────────────

void CS2MarketplaceWidget::setWearPrices(int wearIdx, const OpenSkinPrices &prices)
{
    if (wearIdx < 0 || wearIdx >= kWearCount) return;
    m_wearPrices[wearIdx] = prices;

    const bool hasData = prices.steam.valid || prices.skinport.valid
                      || prices.buff.valid   || prices.csfloat.valid
                      || prices.youpin.valid;
    m_wearBtns[wearIdx]->setEnabled(hasData);

    if (wearIdx == m_activeWear) {
        if (hasData) {
            m_prices = prices;
            m_state  = State::Data;
            m_shimmerTimer.stop();
        }
        update();
    }
}

void CS2MarketplaceWidget::setOpenSkinPrices(const OpenSkinPrices &prices)
{
    m_wearPrices[m_activeWear] = prices;
    m_prices = prices;
    m_state  = State::Data;
    m_status.clear();
    m_shimmerTimer.stop();
    update();
}

void CS2MarketplaceWidget::setSkinName(const QString &name)
{
    m_skinName = name;
    update();
}

void CS2MarketplaceWidget::setActiveWear(int idx)
{
    if (idx < 0 || idx >= kWearCount) return;
    m_activeWear = idx;
    for (int i = 0; i < kWearCount; ++i)
        m_wearBtns[i]->setChecked(i == idx);
    // Refresh display from cache if available
    const auto &wp = m_wearPrices[idx];
    const bool hasData = wp.steam.valid || wp.skinport.valid
                      || wp.buff.valid   || wp.csfloat.valid
                      || wp.youpin.valid;
    if (hasData) {
        m_prices = wp;
        m_state  = State::Data;
        update();
    }
}

void CS2MarketplaceWidget::setLoading(bool loading)
{
    if (loading) {
        m_state   = State::Loading;
        m_shimmer = 0;
        m_shimmerTimer.start();
    } else if (m_state == State::Loading) {
        m_state = State::Idle;
        m_shimmerTimer.stop();
    }
    update();
}

void CS2MarketplaceWidget::setIdle()
{
    m_state = State::Idle;
    m_shimmerTimer.stop();
    update();
}

void CS2MarketplaceWidget::setStatusMessage(const QString &msg)
{
    m_state  = State::Status;
    m_status = msg;
    m_shimmerTimer.stop();
    update();
}

void CS2MarketplaceWidget::clear()
{
    m_skinName.clear();
    m_status.clear();
    m_prices     = {};
    for (int i = 0; i < kWearCount; ++i) {
        m_wearPrices[i] = {};
        m_wearBtns[i]->setEnabled(true);  // re-enable all wear buttons on clear
    }
    m_state      = State::Idle;
    m_hoveredRow = -1;
    m_shimmerTimer.stop();
    update();
}

// ── Layout ────────────────────────────────────────────────────────────────────

void CS2MarketplaceWidget::layoutWearButtons()
{
    const int w = width();
    const int y = (kHeaderH - 38) / 2;
    const int refreshW = 58;
    const int gap = 6;
    const int availW = w - 16 - refreshW - gap;
    const int btnW = (availW - 4 * gap) / kWearCount;

    int x = 8;
    for (int i = 0; i < kWearCount; ++i) {
        m_wearBtns[i]->setGeometry(x, y, btnW, 38);
        x += btnW + gap;
    }
    m_refreshBtn->setGeometry(w - 8 - refreshW, y, refreshW, 38);
}

void CS2MarketplaceWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    layoutWearButtons();
}

// ── Mouse hover for row highlighting ─────────────────────────────────────────

void CS2MarketplaceWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_state != State::Data) {
        m_hoveredRow = -1;
        setCursor(Qt::ArrowCursor);
        return;
    }
    const int y = e->pos().y() - kHeaderH;
    const int row = (y >= 0) ? y / kRowH : -1;
    const int newHover = (row >= 0 && row < kMktCount) ? row : -1;
    setCursor(newHover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (newHover != m_hoveredRow) { m_hoveredRow = newHover; update(); }
}

void CS2MarketplaceWidget::leaveEvent(QEvent *e)
{
    QWidget::leaveEvent(e);
    setCursor(Qt::ArrowCursor);
    if (m_hoveredRow != -1) { m_hoveredRow = -1; update(); }
}

void CS2MarketplaceWidget::mousePressEvent(QMouseEvent *e)
{
    if (m_state != State::Data || m_skinName.isEmpty()) return;
    const int y = e->pos().y() - kHeaderH;
    const int row = (y >= 0) ? y / kRowH : -1;
    if (row < 0 || row >= kMktCount) return;

    const QString encoded = QUrl::toPercentEncoding(m_skinName);
    const QString url = QString(kMarkets[row].urlPattern).arg(encoded);
    QDesktopServices::openUrl(QUrl(url));
}

// ── paintEvent ────────────────────────────────────────────────────────────────

void CS2MarketplaceWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = rect();
    p.fillRect(r, QColor(0x06, 0x09, 0x0f));

    // ── Header bar ────────────────────────────────────────────────────────────
    p.fillRect(0, 0, r.width(), kHeaderH, QColor(0x07, 0x0b, 0x14));
    p.setPen(QColor(0x12, 0x1c, 0x2c));
    p.drawLine(0, kHeaderH, r.width(), kHeaderH);

    // Wear buttons fill the header row — skin name shown in window title / stats widget

    const int bodyTop = kHeaderH;
    const int bodyH   = r.height() - bodyTop;

    // ── Idle: ghost rows + connect prompt ─────────────────────────────────────
    if (m_state == State::Idle) {
        for (int i = 0; i < kMktCount; ++i) {
            const int ry = bodyTop + i * kRowH;
            if (i % 2 == 0) p.fillRect(0, ry, r.width(), kRowH, QColor(0x07, 0x0c, 0x15));
            // accent stub
            QColor acc = kMarkets[i].accent; acc.setAlpha(25);
            p.fillRect(0, ry+4, 3, kRowH-8, acc);
            // label ghost
            p.setPen(QColor(0x10, 0x18, 0x26));
            QFont mf("Segoe UI", 8, QFont::Bold); p.setFont(mf);
            p.drawText(QRect(12, ry+8, 90, 16), Qt::AlignLeft|Qt::AlignVCenter, kMarkets[i].label);
            // price placeholder dashes
            QFont df("Consolas", 13, QFont::Bold); p.setFont(df);
            p.setPen(QColor(0x0e, 0x18, 0x26));
            p.drawText(QRect(r.width()/2-80, ry+8, 160, 24), Qt::AlignCenter, "--");
            // divider
            p.setPen(QColor(0x09, 0x10, 0x1a));
            p.drawLine(0, ry+kRowH-1, r.width(), ry+kRowH-1);
        }
        // overlay prompt
        QFont pf("Segoe UI", 10, QFont::Bold); p.setFont(pf);
        p.setPen(QColor(0x1e, 0x36, 0x56));
        p.drawText(QRect(0, bodyTop, r.width(), bodyH), Qt::AlignCenter,
                   "Connect to view\nmarketplace prices");
        return;
    }

    // ── Status ────────────────────────────────────────────────────────────────
    if (m_state == State::Status) {
        QFont sf("Segoe UI", 9);
        p.setFont(sf);
        p.setPen(QColor(0x38, 0x58, 0x78));
        p.drawText(QRect(0, bodyTop, r.width(), bodyH), Qt::AlignCenter, m_status);
        return;
    }

    // ── Loading shimmer ───────────────────────────────────────────────────────
    if (m_state == State::Loading) {
        const double phase = m_shimmer / 100.0;
        for (int i = 0; i < kMktCount; ++i) {
            const int ry = bodyTop + i * kRowH;
            if (i % 2 == 0) p.fillRect(0, ry, r.width(), kRowH, QColor(0x07, 0x0c, 0x15));
            QColor acc = kMarkets[i].accent; acc.setAlpha(35);
            p.fillRect(0, ry+4, 3, kRowH-8, acc);

            auto shimBar = [&](int x, int y2, int w, int h) {
                const double norm = static_cast<double>(x) / r.width();
                const double d    = std::abs(norm - (phase > 1.0 ? phase-1.0 : phase));
                const int    sa   = static_cast<int>(std::max(0.0, 1.0-d*7.0)*55);
                p.fillRect(x, y2, w, h, QColor(0x0d+sa/4, 0x16+sa/4, 0x24+sa/4));
            };
            shimBar(12,               ry+8,  60+(i%3)*8,  9);   // market label
            shimBar(12,               ry+26, 44+(i%2)*12, 8);   // tag / sub-info
            shimBar(r.width()/2-30,   ry+32, 60,          16);  // ASK price (centred)
            shimBar(r.width()-90,     ry+8,  68,          8);   // pct comparison
            shimBar(r.width()-90,     ry+58, 72,          10);  // BID price
        }
        return;
    }

    // ── Data rows ─────────────────────────────────────────────────────────────
    // Layout zones (all rows share these fixed boundaries):
    //   accent bar : 0–3px
    //   LEFT zone  : 12 – leftEnd       (mkt label, BEST DEAL, listed count)
    //   CENTER zone: leftEnd – rightStart (ASK price, centered)
    //   RIGHT zone : rightStart – (r.width()-4)  (pct vs steam, BID price)
    const int leftEnd    = 86;
    const int rightStart = r.width() - 72;
    const int rightW     = 68;   // always r.width()-rightStart-4

    const OpenSkinPrices::Market *markets[5] = {
        &m_prices.steam, &m_prices.buff, &m_prices.skinport,
        &m_prices.youpin, &m_prices.csfloat,
    };
    const double steamAsk = m_prices.steam.valid ? m_prices.steam.ask : 0;

    QFont mktFont ("Segoe UI",  11, QFont::Bold);
    QFont askFont ("Consolas",  17, QFont::Bold);
    QFont bidFont ("Consolas",  12);
    QFont medFont ("Consolas",  10);
    QFont lblFont ("Consolas",   9);
    QFont volFont ("Consolas",   9);
    QFont tagFont ("Consolas",   9, QFont::Bold);
    QFont pctFont ("Consolas",  11, QFont::Bold);
    QFont suggFont("Consolas",   9);
    QFont hintFont("Segoe UI",   8);

    for (int i = 0; i < kMktCount; ++i) {
        const MktInfo &mi    = kMarkets[i];
        const OpenSkinPrices::Market &mkt = *markets[i];
        const int ry = bodyTop + i * kRowH;
        const bool isSteam    = (i == 0);
        const bool isSkinport = (i == 2);

        const bool isLowest  = (m_prices.lowestAskMarket == mi.key);
        const bool isHovered = (m_hoveredRow == i);

        // Row background + hover highlight
        QColor bg = (i % 2 == 0) ? QColor(0x07, 0x0c, 0x15) : QColor(0x06, 0x09, 0x0f);
        if (isHovered) bg = bg.lighter(145);
        p.fillRect(0, ry, r.width(), kRowH, bg);
        if (isHovered) {
            // right-edge accent glow signals clickability
            QColor glow = mi.accent; glow.setAlpha(90);
            p.fillRect(r.width() - 3, ry, 3, kRowH, glow);
        }

        // Left accent bar
        QColor accent = mi.accent;
        if (!mkt.valid) accent.setAlpha(60);
        p.fillRect(0, ry + 4, 3, kRowH - 8, accent);

        // ── ROW 1 (ry+4 to ry+20): Marketplace label + pct comparison ─────────
        p.setFont(mktFont);
        p.setPen(mkt.valid ? accent.lighter(130) : QColor(0x48, 0x60, 0x7e));
        p.drawText(QRect(12, ry + 4, leftEnd - 14, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, mi.label);

        if (!isSteam && steamAsk > 0 && mkt.ask > 0) {
            const double pct   = (mkt.ask - steamAsk) / steamAsk * 100.0;
            const bool cheaper = pct < 0;
            p.setFont(pctFont);
            p.setPen(cheaper ? QColor(0x30, 0xdd, 0x70) : QColor(0xdd, 0x60, 0x30));
            p.drawText(QRect(rightStart, ry + 4, rightW, 16),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString("%1%2%").arg(cheaper ? "" : "+").arg(pct, 0, 'f', 1));
            p.setFont(volFont); p.setPen(QColor(0x38, 0x54, 0x72));
            p.drawText(QRect(rightStart, ry + 20, rightW, 12),
                       Qt::AlignRight | Qt::AlignVCenter, "vs STEAM");
        }

        // ── ROW 2 (ry+20 to ry+34): BEST DEAL tag ────────────────────────────
        if (isLowest && mkt.valid) {
            p.setFont(tagFont);
            const QRect tagR(12, ry + 22, 66, 14);
            QColor tagBg = accent; tagBg.setAlpha(45);
            p.fillRect(tagR, tagBg);
            p.setPen(accent.lighter(160));
            p.drawRect(tagR.adjusted(0, 0, -1, -1));   // 1px border
            p.drawText(tagR, Qt::AlignCenter, "BEST DEAL");
        }

        if (!mkt.valid) {
            p.setFont(bidFont); p.setPen(QColor(0x24, 0x34, 0x4c));
            p.drawText(QRect(leftEnd, ry + 34, rightStart - leftEnd, 22),
                       Qt::AlignCenter, "No data");
            p.setPen(QColor(0x10, 0x18, 0x28));
            p.drawLine(0, ry + kRowH - 1, r.width(), ry + kRowH - 1);
            continue;
        }

        // ── STEAM special layout (BID|ASK side by side below the label) ───────
        if (isSteam) {
            const int half = r.width() / 2;

            // BID (left half, starts at ry+22 so it's below "STEAM" label)
            if (mkt.bid > 0) {
                p.setFont(askFont); p.setPen(QColor(0x30, 0x90, 0xd8));
                p.drawText(QRect(12, ry + 22, half - 16, 26),
                           Qt::AlignLeft | Qt::AlignVCenter, fmtPrice(mkt.bid));
                p.setFont(lblFont); p.setPen(QColor(0x2c, 0x5c, 0x88));
                p.drawText(QRect(12, ry + 48, half - 16, 14),
                           Qt::AlignLeft | Qt::AlignVCenter, "BID");
            }

            // Center divider
            if (mkt.bid > 0 || mkt.ask > 0) {
                p.setPen(QColor(0x18, 0x28, 0x40));
                p.drawLine(half, ry + 26, half, ry + 66);
            }

            // ASK (right half)
            if (mkt.ask > 0) {
                p.setFont(askFont);
                p.setPen(isLowest ? QColor(0x20, 0xff, 0x90) : QColor(0xd0, 0x60, 0x38));
                p.drawText(QRect(half + 8, ry + 22, half - 20, 26),
                           Qt::AlignRight | Qt::AlignVCenter, fmtPrice(mkt.ask));
                p.setFont(lblFont); p.setPen(QColor(0x88, 0x38, 0x24));
                p.drawText(QRect(half + 8, ry + 48, half - 20, 14),
                           Qt::AlignRight | Qt::AlignVCenter, "ASK");
            } else {
                p.setFont(lblFont); p.setPen(QColor(0x4a, 0x64, 0x80));
                p.drawText(QRect(half + 8, ry + 30, half - 20, 24),
                           Qt::AlignRight | Qt::AlignVCenter, "NO LISTINGS");
            }

            // Median (center row)
            if (mkt.median > 0) {
                p.setFont(medFont); p.setPen(QColor(0x44, 0x68, 0x90));
                p.drawText(QRect(half - 56, ry + 50, 112, 12),
                           Qt::AlignHCenter | Qt::AlignVCenter,
                           QString("MED %1").arg(fmtPrice(mkt.median)));
            }

            // Bottom: order counts + 24h volume
            const int buyOrd = mkt.bidVol, sellOrd = mkt.askVol;
            const int vol24h = m_prices.steamVolume24h;
            QStringList parts;
            if (buyOrd > 0)  parts << QString("%1%2 buy").arg(QChar(0x2191)).arg(buyOrd);
            if (sellOrd > 0) parts << QString("%1%2 sell").arg(QChar(0x2193)).arg(sellOrd);
            if (vol24h > 0)  parts << QString("%1 today").arg(vol24h);
            if (!parts.isEmpty()) {
                p.setFont(volFont); p.setPen(QColor(0x40, 0x58, 0x74));
                p.drawText(QRect(12, ry + kRowH - 16, r.width() - 24, 13),
                           Qt::AlignHCenter | Qt::AlignVCenter, parts.join("  "));
            }

            p.setPen(QColor(0x0c, 0x14, 0x20));
            p.drawLine(0, ry + kRowH - 1, r.width(), ry + kRowH - 1);
            continue;
        }

        // ── Non-steam: no listings fallback ───────────────────────────────────
        if (mkt.ask <= 0) {
            p.setFont(lblFont); p.setPen(QColor(0x4a, 0x64, 0x80));
            p.drawText(QRect(leftEnd, ry + 34, rightStart - leftEnd, 22),
                       Qt::AlignCenter, "NO LISTINGS");
            if (mkt.bid > 0) {
                p.setFont(bidFont); p.setPen(QColor(0x50, 0x82, 0xa8));
                p.drawText(QRect(rightStart, ry + 34, rightW, 18),
                           Qt::AlignRight | Qt::AlignVCenter, fmtPrice(mkt.bid));
                p.setFont(volFont); p.setPen(QColor(0x30, 0x48, 0x64));
                p.drawText(QRect(rightStart, ry + 52, rightW, 12),
                           Qt::AlignRight | Qt::AlignVCenter, "BEST BID");
            }
            p.setPen(QColor(0x0c, 0x14, 0x20));
            p.drawLine(0, ry + kRowH - 1, r.width(), ry + kRowH - 1);
            continue;
        }

        // ── ROW 3 (ry+34 to ry+60): ASK price (center) + BID price (right) ───
        p.setFont(askFont);
        p.setPen(isLowest ? QColor(0x20, 0xff, 0x90) : QColor(0x50, 0xa0, 0xd0));
        p.drawText(QRect(leftEnd, ry + 30, rightStart - leftEnd, 26),
                   Qt::AlignCenter, fmtPrice(mkt.ask));

        if (mkt.bid > 0) {
            p.setFont(bidFont); p.setPen(QColor(0x50, 0x82, 0xa8));
            p.drawText(QRect(rightStart, ry + 34, rightW, 18),
                       Qt::AlignRight | Qt::AlignVCenter, fmtPrice(mkt.bid));
        }

        // ── ROW 4 (ry+58 to ry+70): sub-labels ───────────────────────────────
        p.setFont(lblFont); p.setPen(QColor(0x3c, 0x58, 0x78));
        p.drawText(QRect(leftEnd, ry + 56, rightStart - leftEnd, 13),
                   Qt::AlignCenter, "ASK");

        if (mkt.bid > 0) {
            p.setFont(volFont); p.setPen(QColor(0x30, 0x48, 0x64));
            p.drawText(QRect(rightStart, ry + 53, rightW, 12),
                       Qt::AlignRight | Qt::AlignVCenter, "BID");
        }

        // ── ROW 5 (ry+70 to ry+82): listed count + SUGG + hover hint ─────────
        if (mkt.askVol > 0) {
            p.setFont(volFont); p.setPen(QColor(0x34, 0x50, 0x70));
            p.drawText(QRect(12, ry + 70, leftEnd - 4, 13),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString("%1 listed").arg(mkt.askVol));
        }

        if (isSkinport && m_prices.skinportSuggested > 0) {
            p.setFont(suggFont); p.setPen(QColor(0x70, 0x45, 0x98));
            p.drawText(QRect(leftEnd, ry + 70, rightStart - leftEnd, 13),
                       Qt::AlignCenter,
                       QString("SUGG %1").arg(fmtPrice(m_prices.skinportSuggested)));
        }

        if (isHovered) {
            p.setFont(hintFont); p.setPen(QColor(0x36, 0x52, 0x72));
            p.drawText(QRect(rightStart, ry + 70, rightW, 13),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString(QChar(0x2197)) + " open");
        }

        // Row divider
        p.setPen(QColor(0x0c, 0x14, 0x20));
        p.drawLine(0, ry + kRowH - 1, r.width(), ry + kRowH - 1);
    }

    // Footer hint — fill remaining space, wrap if widget is narrow
    if (m_state == State::Data) {
        const int footY = bodyTop + kMktCount * kRowH;
        const int footH = r.height() - footY;
        if (footH >= 24) {
            p.setFont(QFont("Segoe UI", 11));
            p.setPen(QColor(0x48, 0x64, 0x82));
            p.drawText(QRect(12, footY, r.width() - 24, footH),
                       Qt::AlignCenter | Qt::TextWordWrap,
                       "Click any row to open marketplace in browser");
        }
    }
}
