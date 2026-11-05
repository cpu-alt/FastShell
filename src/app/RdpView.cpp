// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/RdpView.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

#ifdef FASTSHELL_HAVE_AXRDP
#include <QAxObject>
#include <QAxWidget>
#endif

#ifdef FASTSHELL_HAVE_AXRDP
namespace {

QAxWidget *axOf(const QPointer<QWidget> &widget) {
    return static_cast<QAxWidget *>(widget.data());
}

// "DOMAIN\user" or "user@domain" -> {user, domain}. A UPN is passed whole.
std::pair<QString, QString> splitUser(const QString &login) {
    const qsizetype slash = login.indexOf(QLatin1Char('\\'));
    if (slash > 0) return {login.mid(slash + 1), login.left(slash)};
    return {login, QString()};
}

}  // namespace
#endif

RdpView::RdpView(const Options &options, QWidget *parent) : QWidget(parent), m_options(options) {
    setAttribute(Qt::WA_StyledBackground);
    setObjectName(QStringLiteral("rdpView"));
    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);

    // Status page: a centred card with the host and what is going on.
    m_statusPage = new QWidget(this);
    auto *outer = new QVBoxLayout(m_statusPage);
    outer->addStretch(2);
    auto *card = new QWidget(m_statusPage);
    card->setObjectName(QStringLiteral("rdpCard"));
    card->setAttribute(Qt::WA_StyledBackground);
    card->setMaximumWidth(460);
    auto *box = new QVBoxLayout(card);
    box->setContentsMargins(28, 24, 28, 24);
    box->setSpacing(10);
    m_statusTitle = new QLabel(card);
    m_statusTitle->setObjectName(QStringLiteral("rdpTitle"));
    m_statusTitle->setText(options.name.isEmpty() ? options.host : options.name);
    m_statusTitle->setAlignment(Qt::AlignCenter);
    auto *address = new QLabel(QStringLiteral("%1:%2").arg(options.host).arg(options.port), card);
    address->setObjectName(QStringLiteral("rdpAddress"));
    address->setAlignment(Qt::AlignCenter);
    m_statusText = new QLabel(card);
    m_statusText->setObjectName(QStringLiteral("rdpStatus"));
    m_statusText->setAlignment(Qt::AlignCenter);
    m_statusText->setWordWrap(true);
    m_connectButton = new QPushButton(tr("Connect"), card);
    m_connectButton->setObjectName(QStringLiteral("accentButton"));
    m_connectButton->setCursor(Qt::PointingHandCursor);
    m_externalButton = new QPushButton(tr("Open in the system Remote Desktop client"), card);
    m_externalButton->setCursor(Qt::PointingHandCursor);
    box->addWidget(m_statusTitle);
    box->addWidget(address);
    box->addSpacing(6);
    box->addWidget(m_statusText);
    box->addSpacing(6);
    box->addWidget(m_connectButton);
    box->addWidget(m_externalButton);
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(card, 1);
    row->addStretch();
    outer->addLayout(row);
    outer->addStretch(3);
    m_stack->addWidget(m_statusPage);

    connect(m_connectButton, &QPushButton::clicked, this, &RdpView::connectToHost);
    connect(m_externalButton, &QPushButton::clicked, this, &RdpView::openExternally);

    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(400);
    connect(&m_resizeTimer, &QTimer::timeout, this, &RdpView::applyDisplaySize);
    m_stateTimer.setInterval(500);
    connect(&m_stateTimer, &QTimer::timeout, this, &RdpView::pollState);

    if (embeddedAvailable()) {
        showStatus(tr("Ready to connect."), true);
    } else {
        showStatus(tr("This build cannot show remote desktops inside the tab (Qt ActiveQt is missing). "
                      "Open the session in the system client instead."),
                   false);
    }
}

RdpView::~RdpView() {
    m_stateTimer.stop();
#ifdef FASTSHELL_HAVE_AXRDP
    if (m_ax && m_connected) axOf(m_ax)->dynamicCall("Disconnect()");
#endif
}

bool RdpView::embeddedAvailable() {
#ifdef FASTSHELL_HAVE_AXRDP
    return true;
#else
    return false;
#endif
}

void RdpView::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // First time on screen: connect at the real size of the tab.
    if (!m_started && embeddedAvailable()) {
        m_started = true;
        QTimer::singleShot(0, this, &RdpView::connectToHost);
    }
}

void RdpView::showStatus(const QString &text, bool canConnect) {
    m_statusText->setText(text);
    m_connectButton->setVisible(canConnect);
    m_connectButton->setText(m_started ? tr("Reconnect") : tr("Connect"));
    m_stack->setCurrentWidget(m_statusPage);
    Q_EMIT statusChanged(text);
}

bool RdpView::createControl() {
#ifdef FASTSHELL_HAVE_AXRDP
    if (m_ax) return true;
    auto *ax = new QAxWidget(this);
    // Newest control first. The "NotSafeForScripting" classes accept a
    // password from the host application; the generic ProgID is the fallback.
    const QStringList controls{
        QStringLiteral("{A0C63C30-F08D-4AB4-907C-34905D770C7D}"),  // MsRdpClient10NotSafeForScripting
        QStringLiteral("{8B918B82-7985-4C24-89DF-C33AD2BBFBCD}"),  // MsRdpClient9NotSafeForScripting
        QStringLiteral("{A3BC03A0-041D-42E3-AD22-882B7865C9C5}"),  // MsRdpClient8NotSafeForScripting
        QStringLiteral("{54D38BF7-B1EF-4479-9674-1BD6EA465258}"),  // MsRdpClient7NotSafeForScripting
        QStringLiteral("MsTscAx.MsTscAx"),
    };
    bool ok = false;
    for (const QString &control : controls) {
        if (ax->setControl(control)) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        delete ax;
        return false;
    }
    m_ax = ax;
    m_stack->addWidget(ax);
    connect(ax, SIGNAL(OnConnected()), this, SLOT(onConnected()));
    connect(ax, SIGNAL(OnLoginComplete()), this, SLOT(onLoginComplete()));
    connect(ax, SIGNAL(OnDisconnected(int)), this, SLOT(onDisconnected(int)));
    connect(ax, SIGNAL(OnFatalError(int)), this, SLOT(onFatalError(int)));
    return true;
#else
    return false;
#endif
}

void RdpView::connectToHost() {
    m_started = true;
#ifdef FASTSHELL_HAVE_AXRDP
    if (!createControl()) {
        showStatus(tr("The Windows Remote Desktop control (mstscax.dll) could not be loaded."), false);
        return;
    }
    if (m_connected) return;

    // Settings live in several "AdvancedSettingsN" interfaces depending on
    // the Windows version: set each value on the newest one that has it.
    // The interfaces are looked up once (newest first).
    QList<QAxObject *> advanced;
    for (const char *name : {"AdvancedSettings9", "AdvancedSettings8", "AdvancedSettings7", "AdvancedSettings6",
                             "AdvancedSettings5", "AdvancedSettings4", "AdvancedSettings3", "AdvancedSettings2"}) {
        if (QAxObject *settings = axOf(m_ax)->querySubObject(name)) advanced << settings;
    }
    auto setAdvanced = [&advanced](const char *property, const QVariant &value) {
        for (QAxObject *settings : advanced) {
            if (settings->setProperty(property, value)) return true;
        }
        return false;
    };

    const auto [user, domain] = splitUser(m_options.user);
    const qreal ratio = devicePixelRatioF();
    axOf(m_ax)->setProperty("Server", m_options.host);
    if (!user.isEmpty()) axOf(m_ax)->setProperty("UserName", user);
    if (!domain.isEmpty()) axOf(m_ax)->setProperty("Domain", domain);
    axOf(m_ax)->setProperty("DesktopWidth", std::clamp(int(width() * ratio), 640, 8192));
    axOf(m_ax)->setProperty("DesktopHeight", std::clamp(int(height() * ratio), 480, 8192));
    axOf(m_ax)->setProperty("ColorDepth", 32);
    // Shown by the control itself while it connects (its surface is blank).
    axOf(m_ax)->setProperty("ConnectingText", tr("Connecting to %1...").arg(m_options.host));
    axOf(m_ax)->setProperty("DisconnectedText", tr("Disconnected."));
    setAdvanced("RDPPort", m_options.port);
    setAdvanced("EnableCredSspSupport", true);  // NLA, required by most servers
    setAdvanced("AuthenticationLevel", 2);      // warn about unknown certificates
    setAdvanced("SmartSizing", true);           // scale while the tab is resized
    setAdvanced("RedirectClipboard", true);
    setAdvanced("EnableAutoReconnect", true);
    // Speed: compression, persistent bitmap cache (the API spells it
    // "Peristence"), the link quality detected automatically so the server
    // picks the best codec, and Windows key shortcuts sent to the server.
    setAdvanced("Compress", 1);
    setAdvanced("BitmapPeristence", 1);
    setAdvanced("BitmapPersistence", 1);
    setAdvanced("CachePersistenceActive", 1);
    setAdvanced("BandwidthDetection", true);
    setAdvanced("NetworkConnectionType", 7);  // CONNECTION_TYPE_AUTODETECT
    setAdvanced("PerformanceFlags", 0x80 | 0x100);  // font smoothing, desktop composition
    setAdvanced("GrabFocusOnConnect", true);
    setAdvanced("overallConnectionTimeout", 30);  // seconds: an unreachable host fails sooner
    if (!m_options.password.isEmpty()) setAdvanced("ClearTextPassword", m_options.password);
    for (const char *name : {"SecuredSettings3", "SecuredSettings2"}) {
        QAxObject *secured = axOf(m_ax)->querySubObject(name);
        if (secured && secured->setProperty("KeyboardHookMode", 1)) break;  // Alt+Tab, Win... go to the remote desktop
    }

    // Show the control while it connects: its own prompts (user name and
    // password, unknown certificate) are drawn from its window and never
    // appear while it is hidden, which left the tab on "Connecting...".
    Q_EMIT statusChanged(tr("Connecting..."));
    m_stack->setCurrentWidget(m_ax.data());
    m_seenActive = false;
    m_loggedIn = false;
    axOf(m_ax)->dynamicCall("Connect()");
    m_stateTimer.start(500);  // fast while connecting
#else
    openExternally();
#endif
}

void RdpView::disconnectFromHost() {
#ifdef FASTSHELL_HAVE_AXRDP
    if (m_ax && m_connected) {
        m_connected = false;
        axOf(m_ax)->dynamicCall("Disconnect()");
        m_stateTimer.stop();
        showStatus(tr("Disconnected."), true);
    }
#endif
}

// The ActiveX events (OnConnected, OnDisconnected...) never reach Qt with this
// control, so its "Connected" property is read twice a second instead:
// 0 = not connected, 1 = connected, 2 = connecting.
void RdpView::pollState() {
#ifdef FASTSHELL_HAVE_AXRDP
    if (!m_ax) {
        m_stateTimer.stop();
        return;
    }
    const int state = axOf(m_ax)->property("Connected").toInt();
    if (state == 1 || state == 2) m_seenActive = true;
    if (state == 1) {
        if (!m_connected) onConnected();
        if (!m_loggedIn) onLoginComplete();
    } else if (state == 0 && m_seenActive) {
        const bool wasConnected = m_connected;
        m_stateTimer.stop();
        if (wasConnected) {
            onDisconnected(0);
        } else {
            showStatus(tr("Could not connect to %1.").arg(m_options.host), true);
        }
    }
#endif
}

void RdpView::onConnected() {
    m_connected = true;
    if (m_ax) m_stack->setCurrentWidget(m_ax.data());
    Q_EMIT statusChanged(tr("Connected"));
}

void RdpView::onLoginComplete() {
    m_loggedIn = true;
    m_stateTimer.setInterval(2000);  // connected: only a drop is left to notice
    if (m_ax) {
        m_stack->setCurrentWidget(m_ax.data());
        m_ax->setFocus();
    }
    // The tab may have changed size while connecting: send the real size.
    m_resizeTimer.start();
    Q_EMIT statusChanged(tr("Connected"));
}

void RdpView::onDisconnected(int reason) {
    m_connected = false;
    m_stateTimer.stop();
    QString text = tr("Disconnected.");
#ifdef FASTSHELL_HAVE_AXRDP
    if (m_ax) {
        const int extended = axOf(m_ax)->property("ExtendedDisconnectReason").toInt();
        const QString description =
            axOf(m_ax)->dynamicCall("GetErrorDescription(uint,uint)", reason, extended).toString().trimmed();
        // 1, 2, 3: closed locally, by the user or by the server; not an error.
        if (!description.isEmpty() && reason > 3) text = description;
        else if (reason > 3) text = tr("Disconnected (code %1).").arg(reason);
    }
#endif
    showStatus(text, true);
}

void RdpView::onFatalError(int code) {
    m_connected = false;
    showStatus(tr("Remote desktop error (code %1).").arg(code), true);
}

void RdpView::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_connected) m_resizeTimer.start();
}

// Asks the server for a desktop of the new size (Windows 8.1 / 2012 R2 and
// later). Older servers keep their size and SmartSizing scales the picture.
void RdpView::applyDisplaySize() {
#ifdef FASTSHELL_HAVE_AXRDP
    if (!m_ax || !m_connected) return;
    const qreal ratio = devicePixelRatioF();
    const uint w = uint(std::clamp(int(width() * ratio), 640, 8192)) & ~1u;  // even width
    const uint h = uint(std::clamp(int(height() * ratio), 480, 8192));
    axOf(m_ax)->dynamicCall("UpdateSessionDisplaySettings(uint,uint,uint,uint,uint,uint,uint)", w, h, w, h, 0u, 100u, 100u);
#endif
}

void RdpView::openExternally() {
#ifdef Q_OS_WIN
    // A small .rdp file carries host, port and user; mstsc asks for the password.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString file = dir + QStringLiteral("/fastshell-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)
                       + QStringLiteral(".rdp");
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString text = QStringLiteral("full address:s:%1:%2\n").arg(m_options.host).arg(m_options.port);
        if (!m_options.user.isEmpty()) text += QStringLiteral("username:s:%1\n").arg(m_options.user);
        text += QStringLiteral("prompt for credentials:i:1\nscreen mode id:i:1\nsmart sizing:i:1\n");
        f.write(text.toUtf8());
        f.close();
    }
    if (!QProcess::startDetached(QStringLiteral("mstsc.exe"), {QDir::toNativeSeparators(file)})) {
        showStatus(tr("Could not start mstsc.exe."), embeddedAvailable());
    }
    // mstsc reads the file at start: do not leave host and user names behind.
    QTimer::singleShot(30000, qApp, [file] { QFile::remove(file); });
#else
    QStringList args{QStringLiteral("/v:%1:%2").arg(m_options.host).arg(m_options.port), QStringLiteral("/dynamic-resolution")};
    if (!m_options.user.isEmpty()) args << QStringLiteral("/u:%1").arg(m_options.user);
    if (!QProcess::startDetached(QStringLiteral("xfreerdp"), args)) {
        showStatus(tr("Could not start xfreerdp. Install FreeRDP to open remote desktops."), false);
    }
#endif
}
