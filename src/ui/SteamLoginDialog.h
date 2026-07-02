#pragma once
#include <QDialog>
#include <QString>
#include <QTimer>
#include <windows.h>
#include "../../third_party/webview2/include/WebView2.h"

class QLabel;
class QPushButton;
class QResizeEvent;

// Embeds a Microsoft Edge (WebView2) browser in a QDialog.
// Loads the Steam login page and automatically extracts the steamLoginSecure
// cookie once the user authenticates. WebView2 runtime is built into Windows 10/11.
class SteamLoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SteamLoginDialog(QWidget *parent = nullptr);
    ~SteamLoginDialog() override;

    QString cookie() const { return m_cookie; }

protected:
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    void initWebView();
    void checkForCookie();
    void setStatus(const QString &text, bool success = false);

    HWND   m_browserHwnd = nullptr;   // Win32 child window hosting WebView2
    ICoreWebView2Controller *m_controller = nullptr;
    ICoreWebView2           *m_webview    = nullptr;

    QLabel       *m_status    = nullptr;
    QPushButton  *m_cancelBtn = nullptr;
    QTimer        m_cookieTimer;       // polls for the cookie after navigation
    QString       m_cookie;
    bool          m_webviewReady = false;
};
