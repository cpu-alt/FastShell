// Copyright 2026 FastShell contributors. Apache-2.0.
//
// A remote desktop (RDP) session shown in a tab.
//
// On Windows, with Qt ActiveQt available (FASTSHELL_HAVE_AXRDP), the Microsoft
// Remote Desktop ActiveX control (mstscax.dll, the engine of mstsc.exe) is
// embedded right in the tab. Without it, the tab offers to open the session in
// the system client instead (mstsc on Windows, xfreerdp elsewhere).
#pragma once

#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedLayout;

class RdpView : public QWidget {
    Q_OBJECT

public:
    struct Options {
        QString host;
        int port = 3389;
        QString user;      // "user", "DOMAIN\user" or "user@domain"
        QString password;  // empty: Windows asks for it
        QString name;      // shown on the status page
    };

    explicit RdpView(const Options &options, QWidget *parent = nullptr);
    ~RdpView() override;

    void connectToHost();
    void disconnectFromHost();
    bool isConnected() const { return m_connected; }
    const Options &options() const { return m_options; }
    // Opens the same session in the system client (mstsc / xfreerdp).
    void openExternally();
    // True when this build can show the session inside the tab.
    static bool embeddedAvailable();

Q_SIGNALS:
    void statusChanged(const QString &text);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private Q_SLOTS:
    // Events of the ActiveX control (connected by name, see RdpView.cpp).
    void onConnected();
    void onLoginComplete();
    void onDisconnected(int reason);
    void onFatalError(int code);

private:
    void showStatus(const QString &text, bool canConnect);
    bool createControl();
    void applyDisplaySize();

    Options m_options;
    QStackedLayout *m_stack = nullptr;
    QWidget *m_statusPage = nullptr;
    QLabel *m_statusTitle = nullptr;
    QLabel *m_statusText = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_externalButton = nullptr;
    QPointer<QWidget> m_ax;  // the QAxWidget, when embedded
    QTimer m_resizeTimer;  // the remote desktop follows the tab size, debounced
    bool m_connected = false;
    bool m_started = false;
};
