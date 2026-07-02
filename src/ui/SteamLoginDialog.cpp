#include "SteamLoginDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QWindow>
#include <QTimer>
#include <QDebug>
#include <QResizeEvent>
#include <wrl.h>                    // Microsoft::WRL::Callback
using Microsoft::WRL::Callback;

static constexpr wchar_t kSteamLoginUrl[] =
    L"https://steamcommunity.com/login/home/?goto=market%2F";

// ── helpers ───────────────────────────────────────────────────────────────────

// Extract the value of a named cookie from a semicolon-separated cookie string.
static QString extractCookieValue(const std::wstring &cookieString,
                                  const std::wstring &name)
{
    const std::wstring search = name + L"=";
    size_t pos = cookieString.find(search);
    if (pos == std::wstring::npos) return {};
    pos += search.size();
    const size_t end = cookieString.find(L';', pos);
    const std::wstring val = (end == std::wstring::npos)
        ? cookieString.substr(pos)
        : cookieString.substr(pos, end - pos);
    return QString::fromStdWString(val);
}

// ── SteamLoginDialog ──────────────────────────────────────────────────────────

SteamLoginDialog::SteamLoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Login with Steam — Price History");
    setMinimumSize(520, 680);
    resize(540, 720);
    setStyleSheet("QDialog { background:#080c12; }");

    // ── Status bar at the bottom ──────────────────────────────────────────────
    auto *bar = new QWidget();
    bar->setObjectName("statusBar");
    bar->setFixedHeight(40);
    bar->setStyleSheet("#statusBar { background:#080c12; border-top:1px solid #141c28; }");

    m_status = new QLabel("Initializing browser…");
    m_status->setStyleSheet("color:#4a6078; font-size:9pt; padding-left:10px;");

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedSize(72, 26);
    m_cancelBtn->setStyleSheet(
        "QPushButton { background:#0c1420; color:#4a5a70; border:1px solid #1a2a3a;"
        "  border-radius:3px; font-size:10px; }"
        "QPushButton:hover { color:#7a9ab8; border-color:#2a4a6a; }");
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *barRow = new QHBoxLayout(bar);
    barRow->setContentsMargins(0, 0, 8, 0);
    barRow->addWidget(m_status, 1);
    barRow->addWidget(m_cancelBtn);

    // ── Root layout ───────────────────────────────────────────────────────────
    // The browser fills the area above the status bar.
    // We use a placeholder widget; WebView2 is positioned over it via its HWND.
    auto *placeholder = new QWidget();
    placeholder->setObjectName("browserArea");
    placeholder->setStyleSheet("#browserArea { background:#0a0e16; }");
    placeholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(placeholder, 1);
    root->addWidget(bar);

    // ── Cookie poll timer ─────────────────────────────────────────────────────
    // After WebView2 navigates away from the login page we poll every 500 ms
    // by running JS on the page to detect when we've reached the market page,
    // then ask WebView2's cookie manager for steamLoginSecure.
    m_cookieTimer.setInterval(500);
    connect(&m_cookieTimer, &QTimer::timeout, this, &SteamLoginDialog::checkForCookie);
}

SteamLoginDialog::~SteamLoginDialog()
{
    m_cookieTimer.stop();
    if (m_controller) {
        m_controller->Close();
        m_controller->Release();
    }
    if (m_webview) m_webview->Release();
    if (m_browserHwnd) DestroyWindow(m_browserHwnd);
}

void SteamLoginDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    // Defer WebView2 init until the window handle exists
    QTimer::singleShot(0, this, &SteamLoginDialog::initWebView);
}

void SteamLoginDialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    if (m_controller && m_browserHwnd) {
        // Keep the browser filling the area above the status bar (40 px)
        const int statusH = 40;
        RECT rc{ 0, 0, e->size().width(), e->size().height() - statusH };
        m_controller->put_Bounds(rc);
        SetWindowPos(m_browserHwnd, nullptr, 0, 0,
                     e->size().width(), e->size().height() - statusH,
                     SWP_NOZORDER);
    }
}

// ── WebView2 initialisation ───────────────────────────────────────────────────

void SteamLoginDialog::initWebView()
{
    HWND parentHwnd = reinterpret_cast<HWND>(winId());
    const int statusH = 40;
    const int browserH = height() - statusH;

    // Create a child HWND for WebView2 to attach to
    m_browserHwnd = CreateWindowExW(
        0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE,
        0, 0, width(), browserH,
        parentHwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!m_browserHwnd) {
        setStatus("Failed to create browser window.");
        return;
    }

    // WebView2 environment creation is async; the callback fires on the GUI thread
    // because we process the Windows message loop via Qt's event loop.
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment *env) -> HRESULT {
                if (FAILED(result) || !env) {
                    QMetaObject::invokeMethod(this, [this]() {
                        setStatus("WebView2 environment failed — is Edge installed?");
                    }, Qt::QueuedConnection);
                    return result;
                }

                env->CreateCoreWebView2Controller(
                    m_browserHwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT res, ICoreWebView2Controller *ctrl) -> HRESULT {
                            if (FAILED(res) || !ctrl) return res;

                            m_controller = ctrl;
                            m_controller->AddRef();
                            m_controller->get_CoreWebView2(&m_webview);

                            // Fit browser to the window
                            RECT rc;
                            GetClientRect(m_browserHwnd, &rc);
                            m_controller->put_Bounds(rc);
                            m_controller->put_IsVisible(TRUE);

                            // Hide the default context menu and status bar for a cleaner look
                            ICoreWebView2Settings *settings = nullptr;
                            if (SUCCEEDED(m_webview->get_Settings(&settings)) && settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->Release();
                            }

                            // Start polling for the cookie after every navigation completes
                            m_webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *) -> HRESULT {
                                        QMetaObject::invokeMethod(this, [this]() {
                                            m_cookieTimer.start();
                                        }, Qt::QueuedConnection);
                                        return S_OK;
                                    }).Get(), nullptr);

                            m_webview->Navigate(kSteamLoginUrl);

                            QMetaObject::invokeMethod(this, [this]() {
                                m_webviewReady = true;
                                setStatus("Log in to Steam to download CS2 price history.");
                            }, Qt::QueuedConnection);

                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr))
        setStatus("WebView2 init failed — Edge may not be installed.");
}

// ── Cookie extraction ─────────────────────────────────────────────────────────

void SteamLoginDialog::checkForCookie()
{
    if (!m_webview) return;

    ICoreWebView2_2 *wv2 = nullptr;
    if (FAILED(m_webview->QueryInterface(IID_ICoreWebView2_2,
                                          reinterpret_cast<void **>(&wv2))) || !wv2)
        return;

    ICoreWebView2CookieManager *mgr = nullptr;
    if (FAILED(wv2->get_CookieManager(&mgr)) || !mgr) {
        wv2->Release();
        return;
    }
    wv2->Release();

    mgr->GetCookies(
        L"https://steamcommunity.com",
        Callback<ICoreWebView2GetCookiesCompletedHandler>(
            [this](HRESULT, ICoreWebView2CookieList *list) -> HRESULT {
                if (!list) return S_OK;
                UINT count = 0;
                list->get_Count(&count);
                for (UINT i = 0; i < count; ++i) {
                    ICoreWebView2Cookie *cookie = nullptr;
                    if (FAILED(list->GetValueAtIndex(i, &cookie)) || !cookie) continue;

                    LPWSTR name = nullptr;
                    cookie->get_Name(&name);
                    const bool isSteamCookie = (name && wcscmp(name, L"steamLoginSecure") == 0);
                    CoTaskMemFree(name);

                    if (isSteamCookie) {
                        LPWSTR value = nullptr;
                        cookie->get_Value(&value);
                        const QString val = value ? QString::fromWCharArray(value) : QString();
                        CoTaskMemFree(value);
                        cookie->Release();

                        if (!val.isEmpty()) {
                            QMetaObject::invokeMethod(this, [this, val]() {
                                m_cookie = val;
                                m_cookieTimer.stop();
                                setStatus("Login successful — saving and downloading history…", true);
                                QTimer::singleShot(900, this, &QDialog::accept);
                            }, Qt::QueuedConnection);
                        }
                        return S_OK;
                    }
                    cookie->Release();
                }
                return S_OK;
            }).Get());

    mgr->Release();
}

void SteamLoginDialog::setStatus(const QString &text, bool success)
{
    m_status->setText(text);
    m_status->setStyleSheet(
        success ? "color:#3aaa58; font-size:9pt; padding-left:10px;"
                : "color:#4a6078; font-size:9pt; padding-left:10px;");
}
