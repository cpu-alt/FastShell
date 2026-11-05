// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <algorithm>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QDateTime>
#include <QFileInfo>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QFontDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include "app/FilerPanel.h"
#include "app/Icons.h"
#include "app/Logo.h"
#include "app/ManageDialogs.h"
#include "app/Panels.h"
#include "app/RdpView.h"
#include "app/SessionPanel.h"
#include "app/Style.h"
#include "app/TabPage.h"
#include "app/TerminalView.h"
#include "app/Updater.h"
#include "net/SftpClient.h"
#include "net/SshBackend.h"
#include "net/SerialBackend.h"
#include "net/TelnetBackend.h"
#include "pty/Pty.h"

namespace {

SerialOptions serialOptionsFor(const SessionInfo &s) {
    SerialOptions o;
    o.port = s.serialPort;
    o.baudRate = s.baudRate;
    o.dataBits = QSerialPort::DataBits(std::clamp(s.dataBits, 5, 8));
    switch (s.parity.toLatin1()) {
    case 'E': o.parity = QSerialPort::EvenParity; break;
    case 'O': o.parity = QSerialPort::OddParity; break;
    case 'M': o.parity = QSerialPort::MarkParity; break;
    case 'S': o.parity = QSerialPort::SpaceParity; break;
    default: o.parity = QSerialPort::NoParity; break;
    }
    o.stopBits = s.stopBits == QLatin1String("2") ? QSerialPort::TwoStop
               : s.stopBits == QLatin1String("1.5") ? QSerialPort::OneAndHalfStop
                                                    : QSerialPort::OneStop;
    o.flowControl = s.flowControl == QLatin1String("rtscts") ? QSerialPort::HardwareControl
                  : s.flowControl == QLatin1String("xonxoff") ? QSerialPort::SoftwareControl
                                                              : QSerialPort::NoFlowControl;
    o.sendCrLf = s.sendCrLf;
    return o;
}

std::function<bool(const QString &)> confirmIn(QPointer<QWidget> parent) {
    return [parent](const QString &question) {
        return QMessageBox::question(parent, QObject::tr("Host key"), question) == QMessageBox::Yes;
    };
}

std::function<std::optional<QString>(const QString &, bool)> askIn(QPointer<QWidget> parent) {
    return [parent](const QString &prompt, bool secret) -> std::optional<QString> {
        bool ok = false;
        const QString answer = QInputDialog::getText(parent, QObject::tr("Authentication"), prompt,
                                                     secret ? QLineEdit::Password : QLineEdit::Normal, QString(), &ok);
        return ok ? std::optional<QString>(answer) : std::nullopt;
    };
}

// A menu of sessions whose entries can also be dragged out of it: click opens
// the session in a new tab, dragging it onto a terminal opens it right beside
// (same drop zones as a session dragged from the session tree).
class SessionDragMenu : public QMenu {
public:
    using QMenu::QMenu;

protected:
    void mousePressEvent(QMouseEvent *event) override {
        m_pressPos = event->button() == Qt::LeftButton ? event->position().toPoint() : QPoint(-1, -1);
        m_pressed = actionAt(m_pressPos);
        QMenu::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (m_pressed && (event->buttons() & Qt::LeftButton) && m_pressPos.x() >= 0
            && (event->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            const QString id = m_pressed->data().toString();
            const QIcon icon = m_pressed->icon();
            m_pressed = nullptr;
            if (!id.isEmpty()) {
                // Close the menu (and its parents) first, then drag.
                for (QWidget *w = this; auto *menu = qobject_cast<QMenu *>(w); w = menu->parentWidget()) menu->hide();
                auto *drag = new QDrag(parentWidget() ? parentWidget() : this);
                auto *mime = new QMimeData;
                mime->setData(QLatin1String(SessionMimeType), id.toUtf8());
                drag->setMimeData(mime);
                drag->setPixmap(icon.pixmap(24, 24));
                drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
                return;
            }
        }
        QMenu::mouseMoveEvent(event);
    }

private:
    QPoint m_pressPos{-1, -1};
    QAction *m_pressed = nullptr;
};

}  // namespace

// The detected shells (PowerShell, cmd, WSL...), clickable and draggable.
QMenu *MainWindow::shellMenu(QWidget *parent) {
    auto *menu = new SessionDragMenu(parent);
    menu->setToolTipsVisible(true);
    for (const SessionInfo &s : m_store->detectedShells()) {
        QAction *a = menu->addAction(icons::session(s), s.name, this, [this, s] { openSession(s); });
        a->setData(s.id);
        a->setToolTip(tr("Click: new tab. Drag onto a terminal: open it beside."));
    }
    return menu;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_store(new SessionStore(this)), m_tabs(new QTabWidget(this)) {
    setWindowTitle(QStringLiteral("FastShell"));
    setWindowIcon(logo::icon());
    setDockNestingEnabled(true);

    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tabs->tabBar()->setDrawBase(false);
    m_tabs->tabBar()->setAcceptDrops(true);  // hover a tab while dragging a pane to switch to it
    m_tabs->tabBar()->installEventFilter(this);
    m_tabs->tabBar()->setToolTip(tr("Drag a tab onto a terminal to show both side by side"));
    m_tabHoverTimer = new QTimer(this);
    m_tabHoverTimer->setSingleShot(true);
    m_tabHoverTimer->setInterval(450);
    connect(m_tabHoverTimer, &QTimer::timeout, this, [this] {
        if (m_tabHoverIndex >= 0 && m_tabHoverIndex < m_tabs->count()) m_tabs->setCurrentIndex(m_tabHoverIndex);
    });
    setCentralWidget(m_tabs);

    auto *plus = new QToolButton(this);
    plus->setIcon(style::icon(style::Glyph::Terminal));
    plus->setProperty("fs_glyph", int(style::Glyph::Terminal));
    plus->setIconSize(QSize(18, 18));
    plus->setAutoRaise(true);
    plus->setToolTip(tr("New local tab (Ctrl+Shift+T)"));
    plus->setPopupMode(QToolButton::MenuButtonPopup);
    plus->setMenu(shellMenu(plus));
    m_tabs->setCornerWidget(plus, Qt::TopRightCorner);
    connect(plus, &QToolButton::clicked, this, &MainWindow::newLocalTab);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        if (TerminalView *view = current()) {
            view->setFocus();
        }
        if (m_tabs->count() > 0) {
            setWindowTitle(m_tabs->tabText(m_tabs->currentIndex()) + QStringLiteral(" - FastShell"));
        } else {
            setWindowTitle(QStringLiteral("FastShell"));
        }
        updatePanels();
        updateStatus();
    });
    connect(m_tabs->tabBar(), &QTabBar::customContextMenuRequested, this, &MainWindow::showTabMenu);

    QSettings settings;
    m_showTimestamps = settings.value(QStringLiteral("view/timestamps"), true).toBool();
    m_showLineNumbers = settings.value(QStringLiteral("view/lineNumbers"), true).toBool();

    m_updater = new Updater(this);
    QTimer::singleShot(5000, m_updater, &Updater::checkAutomatically);  // once the window is up

    createDocks();
    createMenus();
    createToolBar();
    createStatusBar();

    // A window saved before the panels existed would be too small for them.
    const bool hasLayout = settings.contains(QStringLiteral("window/state"));
    if (!hasLayout || !restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray())) {
        resize(1440, 880);
    }
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
}

// --- layout -------------------------------------------------------------------------

QDockWidget *MainWindow::addPanel(const QString &title, const QString &name, QWidget *widget, Qt::DockWidgetArea area) {
    auto *dock = new QDockWidget(title, this);
    dock->setObjectName(name);
    dock->setWidget(widget);
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(area, dock);
    return dock;
}

void MainWindow::createDocks() {
    m_sessionPanel = new SessionPanel(m_store, this);
    connect(m_sessionPanel, &SessionPanel::openRequested, this,
            [this](const SessionInfo &s, const QString &password) { openSession(s, password); });
    connect(m_sessionPanel, &SessionPanel::openSideBySideRequested, this, &MainWindow::openSideBySide);
    connect(m_sessionPanel, &SessionPanel::openInSplitRequested, this,
            [this](const SessionInfo &s, Qt::Orientation orientation) { splitCurrent(orientation, &s); });
    QDockWidget *sessionDock = addPanel(tr("Session"), QStringLiteral("sessionDock"), m_sessionPanel, Qt::RightDockWidgetArea);

    m_outline = new OutlinePanel(this);
    QDockWidget *outlineDock = addPanel(tr("Outline"), QStringLiteral("outlineDock"), m_outline, Qt::RightDockWidgetArea);
    splitDockWidget(sessionDock, outlineDock, Qt::Vertical);

    m_filer = new FilerPanel(this);
    m_filerDock = addPanel(tr("Filer"), QStringLiteral("filerDock"), m_filer, Qt::LeftDockWidgetArea);
    connect(m_filerDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) updatePanels();
    });

    m_transfers = new TransferPanel(this);
    m_transferDock = addPanel(tr("Transfer"), QStringLiteral("transferDock"), m_transfers, Qt::BottomDockWidgetArea);
    m_transferDock->hide();
    connect(m_transfers, &TransferPanel::activity, m_transferDock, &QDockWidget::show);

    resizeDocks({sessionDock, m_filerDock}, {270, 330}, Qt::Horizontal);

    m_quickCommands = new QuickCommandBar(this);
    addToolBar(Qt::BottomToolBarArea, m_quickCommands);
    connect(m_quickCommands, &QuickCommandBar::commandTriggered, this, [this](const QString &command) {
        if (TerminalView *view = current()) {
            view->sendText(command + QLatin1Char('\r'));
            view->setFocus();
        }
    });
}

void MainWindow::createMenus() {
    auto add = [this](QMenu *menu, const QString &text, const QKeySequence &key, auto slot) {
        QAction *action = menu->addAction(text);
        action->setShortcut(key);
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    QMenu *session = menuBar()->addMenu(tr("&Session"));
    add(session, tr("New session..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this] { m_sessionPanel->newSession(); });
    add(session, tr("New local tab"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), [this] { newLocalTab(); });
    add(session, tr("New remote desktop..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
        [this] { m_sessionPanel->newSession(QString(), SessionInfo::Type::Rdp); });
    QMenu *shells = shellMenu(session);
    shells->setTitle(tr("New shell"));
    session->addMenu(shells);
    add(session, tr("Manage users..."), QKeySequence(), [this] { UsersDialog(m_store, this).exec(); });
    add(session, tr("Manage device profiles..."), QKeySequence(), [this] { ProfilesDialog(m_store, this).exec(); });
    session->addSeparator();
    add(session, tr("Duplicate tab"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), [this] {
        if (TerminalView *v = current()) openSession(m_sessions.value(v));
    });
    session->addSeparator();
    add(session, tr("Close pane"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W), [this] { closePane(); });
    add(session, tr("Close tab"), QKeySequence(), [this] { closeTab(m_tabs->currentIndex()); });
    add(session, tr("Next tab"), QKeySequence(Qt::CTRL | Qt::Key_Tab), [this] {
        if (m_tabs->count()) m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    add(session, tr("Previous tab"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), [this] {
        if (m_tabs->count()) m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
    });
    session->addSeparator();
    add(session, tr("Quit"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q), [this] { close(); });

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    add(edit, tr("Copy"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), [this] {
        if (TerminalView *v = current()) v->copySelection();
    });
    add(edit, tr("Paste"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), [this] {
        if (TerminalView *v = current()) v->paste();
    });
    edit->addSeparator();
    add(edit, tr("Copy last command output"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), [this] {
        if (TerminalView *v = current()) copyCommandOutput(v, -1);
    });
    add(edit, tr("Save last command output..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), [this] {
        if (TerminalView *v = current()) saveCommandOutput(v, -1);
    });
    edit->addSeparator();
    QAction *confirmPaste = edit->addAction(tr("Confirm before pasting several lines"));
    confirmPaste->setCheckable(true);
    connect(edit, &QMenu::aboutToShow, confirmPaste, [confirmPaste] {
        confirmPaste->setChecked(QSettings().value(QStringLiteral("terminal/confirmMultiLinePaste"), true).toBool());
    });
    connect(confirmPaste, &QAction::triggered, this, [](bool on) {
        QSettings().setValue(QStringLiteral("terminal/confirmMultiLinePaste"), on);
    });
    QAction *autoMore = edit->addAction(tr("Skip --More-- automatically"));
    autoMore->setCheckable(true);
    autoMore->setToolTip(tr("Long outputs (show running-config...) scroll by whole, without pressing Space"));
    connect(edit, &QMenu::aboutToShow, autoMore, [autoMore] {
        autoMore->setChecked(QSettings().value(QStringLiteral("terminal/autoMore"), true).toBool());
    });
    connect(autoMore, &QAction::triggered, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("terminal/autoMore"), on);
        for (TerminalView *v : allViews()) v->setAutoMore(on);
    });

    QMenu *window = menuBar()->addMenu(tr("&Window"));
    add(window, tr("Split right"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), [this] { splitCurrent(Qt::Horizontal); });
    add(window, tr("Split down"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), [this] { splitCurrent(Qt::Vertical); });
    add(window, tr("Close pane"), QKeySequence(), [this] { closePane(); });
    m_broadcastAction = add(window, tr("Type in all panes of this tab"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I),
                            [this] { setBroadcastCurrent(m_broadcastAction->isChecked()); });
    m_broadcastAction->setCheckable(true);
    m_broadcastAction->setToolTip(tr("Send what you type to every session of the tab at once"));
    window->addSeparator();
    const std::pair<const char *, Qt::Key> moves[] = {
        {"Focus left pane", Qt::Key_Left}, {"Focus right pane", Qt::Key_Right},
        {"Focus pane above", Qt::Key_Up}, {"Focus pane below", Qt::Key_Down}};
    for (const auto &[label, key] : moves) {
        add(window, tr(label), QKeySequence(Qt::CTRL | Qt::ALT | key), [this, key] {
            if (TerminalView *v = current()) {
                if (TabPage *page = pageOf(v)) page->focusNeighbor(key);
            }
        });
    }
    window->addSeparator();
    // Same arrows with Shift: the pane itself moves (swaps with its neighbor).
    const std::pair<const char *, Qt::Key> swaps[] = {
        {"Move pane left", Qt::Key_Left}, {"Move pane right", Qt::Key_Right},
        {"Move pane up", Qt::Key_Up}, {"Move pane down", Qt::Key_Down}};
    for (const auto &[label, key] : swaps) {
        add(window, tr(label), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | key), [this, key] {
            if (TerminalView *v = current()) {
                if (TabPage *page = pageOf(v)) page->moveActive(key);
            }
        });
    }
    add(window, tr("Move pane to a new tab"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N), [this] {
        if (TerminalView *v = current()) detachToNewTab(v);
    });

    QMenu *view = menuBar()->addMenu(tr("&View"));
    auto zoom = [this](int delta) {
        if (TerminalView *v = current()) {
            v->zoom(delta);
            QSettings().setValue(QStringLiteral("view/font"), v->terminalFont().toString());
        }
    };
    m_zoomInAction = add(view, tr("Zoom in"), QKeySequence(Qt::CTRL | Qt::Key_Equal), [zoom] { zoom(1); });
    m_zoomOutAction = add(view, tr("Zoom out"), QKeySequence(Qt::CTRL | Qt::Key_Minus), [zoom] { zoom(-1); });
    m_fontAction = add(view, tr("Font..."), QKeySequence(), [this] {
        TerminalView *v = current();
        bool ok = false;
        const QFont font = QFontDialog::getFont(&ok, v ? v->terminalFont() : QFont(), this, tr("Terminal font"),
                                                QFontDialog::MonospacedFonts);
        if (ok) {
            for (TerminalView *v : allViews()) v->setTerminalFont(font);
            QSettings().setValue(QStringLiteral("view/font"), font.toString());
        }
    });
    view->addSeparator();
    QAction *timestamps = m_timestampsAction = view->addAction(tr("Timestamps"));
    timestamps->setIconText(tr("Time"));
    timestamps->setToolTip(tr("Show or hide the time of each line"));
    timestamps->setCheckable(true);
    timestamps->setChecked(m_showTimestamps);
    connect(timestamps, &QAction::toggled, this, [this](bool on) {
        m_showTimestamps = on;
        QSettings().setValue(QStringLiteral("view/timestamps"), on);
        for (TerminalView *v : allViews()) v->setShowTimestamps(on);
    });
    QAction *lineNumbers = m_lineNumbersAction = view->addAction(tr("Line numbers"));
    lineNumbers->setIconText(tr("Ln#"));
    lineNumbers->setToolTip(tr("Show or hide line numbers"));
    lineNumbers->setCheckable(true);
    lineNumbers->setChecked(m_showLineNumbers);
    connect(lineNumbers, &QAction::toggled, this, [this](bool on) {
        m_showLineNumbers = on;
        QSettings().setValue(QStringLiteral("view/lineNumbers"), on);
        for (TerminalView *v : allViews()) v->setShowLineNumbers(on);
    });
    view->addSeparator();
    for (QDockWidget *dock : findChildren<QDockWidget *>()) {
        view->addAction(dock->toggleViewAction());
    }
    view->addAction(m_quickCommands->toggleViewAction());
    view->addSeparator();
    createThemeMenu(view);
    view->addSeparator();
    m_fullScreenAction = add(view, tr("Full screen"), QKeySequence(Qt::ALT | Qt::Key_Return), [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Check for updates..."), this, [this] { m_updater->checkNow(); });
    QAction *autoUpdate = help->addAction(tr("Check for updates automatically"));
    autoUpdate->setCheckable(true);
    autoUpdate->setChecked(Updater::automaticChecks());
    connect(autoUpdate, &QAction::toggled, this, [](bool on) { Updater::setAutomaticChecks(on); });
    help->addSeparator();
    help->addAction(tr("Sessions file location"), this, [this] {
        QMessageBox::information(this, tr("Sessions"), m_store->filePath());
    });
    help->addAction(tr("About FastShell"), this, [this] {
        QMessageBox box(this);
        box.setWindowTitle(tr("About FastShell"));
        box.setIconPixmap(logo::pixmap(96));
        box.setText(tr("<h3>FastShell %1</h3><p>SSH, Telnet, serial, remote desktop and local terminal.</p>"
                       "<p><b>Credits:</b> Nathan Witz &amp; Patrizio Ventura</p>"
                       "<p>Apache-2.0. Qt %2, libssh, OpenSSL. Third-party notices: NOTICE file.</p>")
                        .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
        box.exec();
    });
}

void MainWindow::createToolBar() {
    auto *bar = new QToolBar(tr("Toolbar"), this);
    bar->setObjectName(QStringLiteral("mainToolBar"));
    bar->setMovable(false);
    bar->setIconSize(QSize(20, 20));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::TopToolBarArea, bar);

    auto action = [this, bar](style::Glyph glyph, const QString &tip, auto slot) {
        QAction *a = bar->addAction(style::icon(glyph), tip);
        a->setProperty("fs_glyph", int(glyph));
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    action(style::Glyph::NewSession, tr("New session (Ctrl+Shift+N)"), [this] { m_sessionPanel->newSession(); });
    QAction *local = action(style::Glyph::Terminal, tr("New local tab (Ctrl+Shift+T)"), [this] { newLocalTab(); });
    local->setMenu(shellMenu(this));
    if (auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(local))) {
        button->setPopupMode(QToolButton::MenuButtonPopup);
    }
    action(style::Glyph::RemoteDesktop, tr("New remote desktop (Ctrl+Shift+R)"),
           [this] { m_sessionPanel->newSession(QString(), SessionInfo::Type::Rdp); });
    bar->addSeparator();
    action(style::Glyph::SplitRight, tr("Split right (Ctrl+Shift+E)"), [this] { splitCurrent(Qt::Horizontal); });
    action(style::Glyph::SplitDown, tr("Split down (Ctrl+Shift+O)"), [this] { splitCurrent(Qt::Vertical); });
    m_broadcastAction->setIcon(style::icon(style::Glyph::Broadcast));
    m_broadcastAction->setProperty("fs_glyph", int(style::Glyph::Broadcast));
    bar->addAction(m_broadcastAction);

    // View: the same actions as the View menu, one click away.
    bar->addSeparator();
    auto viewAction = [bar](QAction *a, style::Glyph glyph, const QString &tip) {
        a->setIcon(style::icon(glyph));
        a->setProperty("fs_glyph", int(glyph));
        a->setToolTip(tip);
        bar->addAction(a);
    };
    viewAction(m_zoomOutAction, style::Glyph::ZoomOut, tr("Zoom out (Ctrl+-)"));
    viewAction(m_zoomInAction, style::Glyph::ZoomIn, tr("Zoom in (Ctrl+=)"));
    viewAction(m_fontAction, style::Glyph::Font, tr("Terminal font"));
    viewAction(m_timestampsAction, style::Glyph::Clock, tr("Show or hide the time of each line"));
    viewAction(m_lineNumbersAction, style::Glyph::LineNumbers, tr("Show or hide line numbers"));
    bar->addSeparator();
    const std::pair<const char *, style::Glyph> panels[] = {{"sessionDock", style::Glyph::SessionList},
                                                            {"filerDock", style::Glyph::Filer},
                                                            {"outlineDock", style::Glyph::Outline}};
    for (const auto &[name, glyph] : panels) {
        if (auto *dock = findChild<QDockWidget *>(QLatin1String(name))) {
            viewAction(dock->toggleViewAction(), glyph, tr("Show or hide the %1 panel").arg(dock->windowTitle()));
        }
    }
    viewAction(m_fullScreenAction, style::Glyph::FullScreen, tr("Full screen (Alt+Enter)"));

    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    auto *hint = new QLabel(tr("Tip: drag a tab or a pane title onto a terminal to split"), bar);
    hint->setStyleSheet(QStringLiteral("color:#6C7380; padding-right: 8px"));
    bar->addWidget(hint);
}

void MainWindow::createStatusBar() {
    m_statusMessage = new QLabel(tr("Ready"), this);
    m_statusCursor = new QLabel(this);
    m_statusSize = new QLabel(this);
    m_statusType = new QLabel(this);
    m_statusClock = new QLabel(this);
    statusBar()->addWidget(m_statusMessage, 1);
    m_statusBroadcast = new QLabel(tr("  TYPING IN ALL PANES  "), this);
    m_statusBroadcast->setStyleSheet(QStringLiteral("background:#FF9F1C; color:#10141A; font-weight:bold; border-radius:3px"));
    m_statusBroadcast->hide();
    statusBar()->addWidget(m_statusBroadcast);
    // One-click toggles for the margin (same actions as the View menu).
    for (QAction *action : {m_timestampsAction, m_lineNumbersAction}) {
        auto *button = new QToolButton(this);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        statusBar()->addPermanentWidget(button);
    }
    for (QLabel *label : {m_statusType, m_statusCursor, m_statusSize, m_statusClock}) {
        label->setContentsMargins(10, 0, 10, 0);
        statusBar()->addPermanentWidget(label);
    }
    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statusTimer->start();
    updateStatus();
}

// Minimized for hours: nothing on screen to refresh, so the status bar stops.
// Back on screen (or reactivated): everything is refreshed at once.
void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (!m_statusTimer) return;
    if (event->type() == QEvent::WindowStateChange) {
        if (isMinimized()) {
            m_statusTimer->stop();
        } else if (!m_statusTimer->isActive()) {
            m_statusTimer->start();
            refreshAfterIdle();
        }
    } else if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
        refreshAfterIdle();
    }
}

void MainWindow::refreshAfterIdle() {
    updateStatus();
    for (TerminalView *view : allViews()) {
        if (view->isVisible()) view->update();
    }
}

void MainWindow::updateStatus() {
    {
        TerminalView *v = current();
        TabPage *page = v ? pageOf(v) : nullptr;
        const bool on = page && page->broadcast();
        if (m_broadcastAction) m_broadcastAction->setChecked(on);
        if (m_statusBroadcast) m_statusBroadcast->setVisible(on);
    }
    m_statusClock->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    TerminalView *view = current();
    if (!view) {
        m_statusCursor->clear();
        m_statusSize->clear();
        m_statusType->clear();
        m_statusMessage->setText(tr("Ready"));
        if (RdpView *rdp = rdpAt(m_tabs->currentIndex())) {
            m_statusType->setText(QStringLiteral("RDP %1").arg(rdp->options().host));
            m_statusMessage->setText(rdp->isConnected() ? tr("Connected") : tr("Not connected"));
        }
        return;
    }
    const QPoint cursor = view->cursorPosition();
    const QSize size = view->terminalSize();
    const SessionInfo s = m_sessions.value(view);
    m_statusCursor->setText(tr("Ln %1  Ch %2").arg(cursor.y()).arg(cursor.x()));
    m_statusSize->setText(QStringLiteral("%1×%2").arg(size.width()).arg(size.height()));
    if (s.type == SessionInfo::Type::Local) {
        m_statusType->setText(tr("Local"));
    } else if (s.type == SessionInfo::Type::Serial) {
        m_statusType->setText(QStringLiteral("Serial %1 %2").arg(s.serialPort, serialOptionsFor(s).summary()));
    } else {
        QString type = s.typeName() + QLatin1Char(' ') + s.host;
        if (auto *ssh = qobject_cast<SshBackend *>(view->backend()); ssh && ssh->usedLegacyAlgorithms()) {
            type += tr(" (old algorithms)");
        }
        m_statusType->setText(type);
    }
    m_statusMessage->setText(view->isRunning() ? tr("Ready") : tr("Disconnected"));
}

// --- tabs and panes -----------------------------------------------------------------

TabPage *MainWindow::pageAt(int index) const {
    return qobject_cast<TabPage *>(m_tabs->widget(index));
}

TabPage *MainWindow::pageOf(TerminalView *view) const {
    for (QWidget *w = view; w; w = w->parentWidget()) {
        if (auto *page = qobject_cast<TabPage *>(w)) return page;
    }
    return nullptr;
}

TerminalView *MainWindow::current() const {
    TabPage *page = pageAt(m_tabs->currentIndex());
    return page ? page->activeView() : nullptr;
}

QList<TerminalView *> MainWindow::allViews() const {
    QList<TerminalView *> views;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (TabPage *page = pageAt(i)) views += page->views();
    }
    return views;
}

void MainWindow::applyViewOptions(TerminalView *view) {
    view->setAutoMore(QSettings().value(QStringLiteral("terminal/autoMore"), true).toBool());
    const QString font = QSettings().value(QStringLiteral("view/font")).toString();
    QFont f;
    if (!font.isEmpty() && f.fromString(font)) {
        view->setTerminalFont(f);
    }
    view->setShowTimestamps(m_showTimestamps);
    view->setShowLineNumbers(m_showLineNumbers);
}

void MainWindow::newLocalTab() {
    SessionInfo s;
    s.type = SessionInfo::Type::Local;
    s.name = QFileInfo(Pty::defaultShell()).completeBaseName();
    openSession(s);
}

TerminalView *MainWindow::createView(const SessionInfo &session, const QString &password) {
    TerminalView::BackendFactory factory;
    QPointer<MainWindow> self(this);

    switch (session.type) {
    case SessionInfo::Type::Local:
        factory = [session](QObject *parent) -> Backend * {
            Pty::Options options;
            options.program = session.program;
            options.arguments = session.arguments;
            options.workingDirectory = session.workingDirectory;
            return Pty::create(options, parent).release();
        };
        break;
    case SessionInfo::Type::Telnet:
        factory = [session](QObject *parent) -> Backend * {
            return new TelnetBackend(session.host, session.port, parent);
        };
        break;
    case SessionInfo::Type::Serial:
        factory = [session](QObject *parent) -> Backend * {
            return new SerialBackend(serialOptionsFor(session), parent);
        };
        break;
    case SessionInfo::Type::Ssh: {
        // A shared user replaces the session's own login fields.
        SshOptions options;
        options.host = session.host;
        options.port = session.port;
        options.user = session.user;
        options.password = password.isEmpty() ? session.password() : password;
        options.identityFile = session.identityFile;
        options.legacyAlgorithms = session.legacyAlgorithms;
        if (const Credential *c = m_store->findCredential(session.credentialId)) {
            options.user = c->user;
            options.password = password.isEmpty() ? c->password() : password;
            options.identityFile = c->identityFile;
        }
        factory = [options, self](QObject *parent) -> Backend * {
            auto *ssh = new SshBackend(options, parent);
            ssh->confirm = confirmIn(self);
            ssh->ask = askIn(self);
            // The Filer (SFTP) opens only once logged in, with the same login.
            QObject::connect(ssh, &SshBackend::connected, self.data(), [self] {
                if (!self) return;
                self->updatePanels();
                for (int i = 0; i < self->m_tabs->count(); ++i) {  // tooltip: old algorithms
                    if (TabPage *page = self->pageAt(i)) self->refreshTabTitle(page);
                }
            });
            return ssh;
        };
        break;
    }
    case SessionInfo::Type::Rdp:
        Q_ASSERT_X(false, "createView", "remote desktops are not terminals: use openRemoteDesktop()");
        return nullptr;
    }

    SessionInfo shown = session;
    if (!shown.color.isValid()) {
        if (const DeviceProfile *p = m_store->findProfile(session.profileId)) shown.color = p->color;
    }
    auto *view = new TerminalView(factory);
    applyViewOptions(view);
    view->setBackspaceSendsCtrlH(session.backspaceCtrlH);
    view->setPaneInfo(shown.name, icons::sessionColor(shown));
    m_sessions.insert(view, shown);
    connect(view, &QObject::destroyed, this, [this, view] {
        m_sessions.remove(view);
        m_sftp.remove(view);
    });
    connect(view, &TerminalView::titleChanged, this, [this, view] {
        if (TabPage *page = pageOf(view); page && page->activeView() == view) refreshTabTitle(page);
    });
    connect(view, &TerminalView::connected, this, [this, view] {
        if (view == current()) updatePanels();
    });
    return view;
}

void MainWindow::refreshTabTitle(TabPage *page) {
    const int index = m_tabs->indexOf(page);
    TerminalView *view = page->activeView();
    if (index < 0 || !view) return;
    const SessionInfo s = m_sessions.value(view);
    const QString title = view->title();
    // Saved sessions keep their name; local shells show their live title,
    // unless it is just the path of their own .exe.
    const bool useTitle = s.type == SessionInfo::Type::Local && !title.isEmpty()
                       && !title.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive);
    QString text = (useTitle ? title : s.name).left(32);
    if (page->count() > 1) {
        // Several panes: list their sessions, "sw-core · sw-edge (+2)".
        QStringList names;
        for (TerminalView *v : page->views()) names << v->paneTitle();
        text = names.mid(0, 2).join(QStringLiteral(" · "));
        if (names.size() > 2) text += QStringLiteral(" (+%1)").arg(names.size() - 2);
    }
    if (!page->property("fs_customName").toString().isEmpty()) {
        text = page->property("fs_customName").toString();
    }
    m_tabs->setTabText(index, text);
    QString tip = title;
    if (auto *ssh = qobject_cast<SshBackend *>(view->backend()); ssh && ssh->usedLegacyAlgorithms()) {
        tip += (tip.isEmpty() ? QString() : QStringLiteral("\n"))
             + tr("Old device: connected with legacy SHA-1/CBC algorithms, as PuTTY does.");
    }
    m_tabs->setTabToolTip(index, tip);
    m_tabs->setTabIcon(index, icons::session(s));
    m_tabs->tabBar()->setTabTextColor(index, s.color.isValid() ? s.color : QColor());
    if (index == m_tabs->currentIndex()) {
        setWindowTitle(text + QStringLiteral(" - FastShell"));
    }
}

void MainWindow::addPage(TabPage *page, const SessionInfo &first, bool startViews) {
    connect(page, &TabPage::activeViewChanged, this, [this, page] {
        refreshTabTitle(page);
        if (pageAt(m_tabs->currentIndex()) == page) {
            updatePanels();
            updateStatus();
        }
    });
    connect(page, &TabPage::panesChanged, this, [this, page] { refreshTabTitle(page); });
    connect(page, &TabPage::empty, this, [this, page] { closeTab(m_tabs->indexOf(page)); });
    connect(page, &TabPage::sessionsDropped, this,
            [this, page](TerminalView *target, const QStringList &ids, Qt::Edge edge) {
                dropSessions(page, target, ids, edge);
            });
    connect(page, &TabPage::paneMenuRequested, this, &MainWindow::showPaneMenu);
    const int index = m_tabs->addTab(page, icons::session(first), first.name);
    addCloseButton(page);
    m_tabs->setCurrentIndex(index);
    refreshTabTitle(page);
    if (startViews) {
        for (TerminalView *view : page->views()) {
            QTimer::singleShot(0, view, &TerminalView::start);  // once laid out, at its real size
        }
    }
    if (TerminalView *active = page->activeView()) active->setFocus();
}

void MainWindow::openSession(const SessionInfo &session, const QString &password) {
    if (session.type == SessionInfo::Type::Rdp) {
        openRemoteDesktop(session, password);
        return;
    }
    addPage(new TabPage(createView(session, password)), session);
}

void MainWindow::addCloseButton(QWidget *page) {
    auto *button = new QToolButton(m_tabs->tabBar());
    button->setObjectName(QStringLiteral("tabClose"));
    button->setIcon(style::icon(style::Glyph::Close));
    button->setProperty("fs_glyph", int(style::Glyph::Close));
    button->setIconSize(QSize(12, 12));
    button->setFixedSize(18, 18);
    button->setToolTip(tr("Close tab"));
    button->setCursor(Qt::ArrowCursor);
    connect(button, &QToolButton::clicked, this, [this, page] { closeTab(m_tabs->indexOf(page)); });
    m_tabs->tabBar()->setTabButton(m_tabs->indexOf(page), QTabBar::RightSide, button);
}

RdpView *MainWindow::rdpAt(int index) const {
    return qobject_cast<RdpView *>(m_tabs->widget(index));
}

// A remote desktop takes a whole tab: it cannot be split like terminals.
void MainWindow::openRemoteDesktop(const SessionInfo &session, const QString &password) {
    RdpView::Options options;
    options.host = session.host;
    options.port = session.port > 0 ? session.port : SessionInfo::defaultPort(SessionInfo::Type::Rdp);
    options.user = session.user;
    options.password = password.isEmpty() ? session.password() : password;
    options.name = session.name;
    if (const Credential *c = m_store->findCredential(session.credentialId)) {
        options.user = c->user;
        options.password = password.isEmpty() ? c->password() : password;
    }
    auto *view = new RdpView(options);
    const int index = m_tabs->addTab(view, icons::session(session), session.name);
    addCloseButton(view);
    m_tabs->setTabToolTip(index, QStringLiteral("RDP %1:%2").arg(options.host).arg(options.port));
    if (session.color.isValid()) m_tabs->tabBar()->setTabTextColor(index, session.color);
    connect(view, &RdpView::statusChanged, this, [this] { updateStatus(); });
    m_tabs->setCurrentIndex(index);
}

void MainWindow::openSideBySide(const QList<SessionInfo> &all) {
    // Remote desktops get their own tabs; the terminals share one.
    QList<SessionInfo> sessions;
    for (const SessionInfo &s : all) {
        if (s.type == SessionInfo::Type::Rdp) openRemoteDesktop(s, QString());
        else sessions << s;
    }
    if (sessions.isEmpty()) return;
    if (sessions.size() > 16 && QMessageBox::question(this, tr("Open side by side"),
                                                       tr("Open %1 sessions in one tab?").arg(sessions.size()))
                                    != QMessageBox::Yes) {
        return;
    }
    QList<TerminalView *> views;
    for (const SessionInfo &s : sessions) views << createView(s, QString());
    auto *page = new TabPage(views);
    addPage(page, sessions.first());
}

void MainWindow::setBroadcastCurrent(bool on) {
    TerminalView *view = current();
    if (TabPage *page = view ? pageOf(view) : nullptr) {
        page->setBroadcast(on);
    }
    updateStatus();
}

void MainWindow::splitCurrent(Qt::Orientation orientation, const SessionInfo *session, const QString &password) {
    TerminalView *active = current();
    TabPage *page = active ? pageOf(active) : nullptr;
    if (!page || (session && session->type == SessionInfo::Type::Rdp)) {
        if (session) openSession(*session, password);
        else newLocalTab();
        return;
    }
    // Without an explicit session, the new pane repeats the active one.
    const SessionInfo info = session ? *session : m_sessions.value(active);
    TerminalView *view = createView(info, password);
    page->split(active, view, orientation);
    refreshTabTitle(page);
    QTimer::singleShot(0, view, &TerminalView::start);  // once laid out, at its real size
}

void MainWindow::closePane() {
    TerminalView *view = current();
    TabPage *page = view ? pageOf(view) : nullptr;
    if (!page) return;
    if (page->count() > 1) {
        page->closeView(view);
        refreshTabTitle(page);
    } else {
        closeTab(m_tabs->indexOf(page));
    }
}

void MainWindow::closeTab(int index) {
    QWidget *page = m_tabs->widget(index);  // a TabPage, or an RdpView
    if (!page) return;
    m_tabs->removeTab(index);
    page->deleteLater();  // its views (and their SFTP clients) go with it
    if (m_tabs->count() == 0) {
        updatePanels();
    }
}

void MainWindow::showTabMenu(const QPoint &pos) {
    const int index = m_tabs->tabBar()->tabAt(pos);
    if (RdpView *rdp = rdpAt(index)) {
        m_tabs->setCurrentIndex(index);
        QMenu menu(this);
        if (rdp->isConnected()) {
            menu.addAction(tr("Disconnect"), rdp, &RdpView::disconnectFromHost);
        } else if (RdpView::embeddedAvailable()) {
            menu.addAction(tr("Connect"), rdp, &RdpView::connectToHost);
        }
        menu.addAction(tr("Open in the system Remote Desktop client"), rdp, &RdpView::openExternally);
        menu.addSeparator();
        menu.addAction(tr("Move to a panel"), this, [this, rdp] { detachRdp(rdp); });
        menu.addAction(tr("Close tab"), this, [this, rdp] { closeTab(m_tabs->indexOf(rdp)); });
        menu.exec(m_tabs->tabBar()->mapToGlobal(pos));
        return;
    }
    TabPage *page = pageAt(index);
    if (!page) return;
    m_tabs->setCurrentIndex(index);
    TerminalView *view = page->activeView();
    const SessionInfo s = m_sessions.value(view);
    QMenu menu(this);
    if (auto *serial = qobject_cast<SerialBackend *>(view->backend())) {
        menu.addAction(tr("Send BREAK"), serial, &SerialBackend::sendBreak);
        QAction *backspace = menu.addAction(tr("Backspace sends ^H"));
        backspace->setCheckable(true);
        backspace->setChecked(view->backspaceSendsCtrlH());
        backspace->setToolTip(tr("Switch it if Backspace does nothing on this device (this tab only)"));
        connect(backspace, &QAction::toggled, view, &TerminalView::setBackspaceSendsCtrlH);
        menu.addSeparator();
    }
    menu.addAction(tr("Split right"), this, [this] { splitCurrent(Qt::Horizontal); });
    menu.addAction(tr("Split down"), this, [this] { splitCurrent(Qt::Vertical); });
    menu.addSeparator();
    menu.addAction(tr("Duplicate tab"), this, [this, s] { openSession(s); });
    menu.addAction(tr("Rename..."), this, [this, page, index] {
        const QString name = QInputDialog::getText(this, tr("Rename tab"), tr("Name (empty = automatic):"),
                                                   QLineEdit::Normal, m_tabs->tabText(index));
        page->setProperty("fs_customName", name.trimmed());
        refreshTabTitle(page);
    });
    if (s.id.isEmpty() && s.type != SessionInfo::Type::Local) {
        menu.addAction(tr("Save in sessions"), this, [this, s] { m_store->upsert(s); });
    }
    menu.addSeparator();
    if (page->count() > 1) {
        menu.addAction(tr("Close pane"), this, &MainWindow::closePane);
    }
    menu.addAction(tr("Close tab"), this, [this, page] { closeTab(m_tabs->indexOf(page)); });
    menu.addAction(tr("Close other tabs"), this, [this, page] {
        for (int i = m_tabs->count() - 1; i >= 0; --i) {
            if (pageAt(i) != page) closeTab(i);
        }
    });
    menu.exec(m_tabs->tabBar()->mapToGlobal(pos));
}

// --- panels -------------------------------------------------------------------------

SftpClient *MainWindow::sftpFor(TerminalView *view) {
    if (QPointer<SftpClient> existing = m_sftp.value(view)) {
        return existing;
    }
    auto *ssh = qobject_cast<SshBackend *>(view->backend());
    // Not before the login typed in the terminal: SFTP reuses it.
    if (!ssh || !view->isRunning() || !ssh->isLoggedIn()) return nullptr;

    SshPrompter prompter;
    prompter.guiContext = this;
    prompter.confirm = confirmIn(this);
    prompter.ask = askIn(this);
    auto *client = new SftpClient(ssh->connectedOptions(), prompter, view);  // dies with the tab
    m_transfers->attach(client);
    m_sftp.insert(view, client);
    return client;
}

void MainWindow::updatePanels() {
    TerminalView *view = current();
    m_outline->setView(view);
    if (!m_filerDock->isVisible()) return;  // no SFTP connection unless the Filer is shown

    const SessionInfo s = view ? m_sessions.value(view) : SessionInfo{};
    if (view && s.type == SessionInfo::Type::Ssh) {
        if (SftpClient *client = sftpFor(view)) {
            m_filer->showRemote(view, client);
            return;
        }
    }
    m_filer->showLocal(view);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    QMainWindow::closeEvent(event);
}

// Sessions dragged from the tree and dropped on one side of a pane (or of
// the whole tab, target = null) open right there, in the same tab.
void MainWindow::dropSessions(TabPage *page, TerminalView *target, const QStringList &ids, Qt::Edge edge) {
    TerminalView *anchor = target;
    for (const QString &id : ids) {
        const SessionInfo *s = m_store->find(id);
        if (!s) continue;
        if (s->type == SessionInfo::Type::Rdp) {  // a desktop takes its own tab
            openRemoteDesktop(*s, QString());
            continue;
        }
        TerminalView *view = createView(*s, QString());
        page->place(view, anchor, edge);
        QTimer::singleShot(0, view, &TerminalView::start);
        anchor = view;  // several sessions line up in the same direction
    }
    refreshTabTitle(page);
}

// A pane dropped on the tab bar leaves its tab for a new one of its own.
void MainWindow::detachToNewTab(TerminalView *view) {
    TabPage *from = pageOf(view);
    if (!from || from->count() < 2) return;  // already alone in its tab
    from->release(view);
    refreshTabTitle(from);
    addPage(new TabPage(view), m_sessions.value(view), false);  // keeps running
}

// A pane joins another tab, on its right side. Its old tab closes if empty.
void MainWindow::moveToTab(TerminalView *view, TabPage *target) {
    TabPage *from = pageOf(view);
    if (!from || !target || from == target) return;
    from->release(view);
    if (m_tabs->indexOf(from) >= 0) refreshTabTitle(from);
    target->place(view, nullptr, Qt::RightEdge);
    m_tabs->setCurrentWidget(target);
    refreshTabTitle(target);
}

void MainWindow::copyCommandOutput(TerminalView *view, int index) {
    const QString text = view->commandOutput(index);
    if (text.isEmpty()) {
        statusBar()->showMessage(tr("No command output to copy"), 3000);
        return;
    }
    QGuiApplication::clipboard()->setText(text);
    statusBar()->showMessage(tr("Output copied: %n line(s)", nullptr, int(text.count(QLatin1Char('\n')) + 1)), 4000);
}

void MainWindow::saveCommandOutput(TerminalView *view, int index) {
    const QString text = view->commandOutput(index);
    if (text.isEmpty()) {
        statusBar()->showMessage(tr("No command output to save"), 3000);
        return;
    }
    // Suggested name: session and command, e.g. "A41-SW-01 show running-config.txt".
    const QList<TerminalView::Command> &commands = view->commands();
    const QString command = commands.value(index < 0 ? commands.size() - 1 : index).text;
    QString name = view->paneTitle().isEmpty() ? view->title() : view->paneTitle();
    name = (name + QLatin1Char(' ') + command).simplified();
    name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    const QString dir = QSettings().value(QStringLiteral("output/lastDir"), QDir::homePath()).toString();
    const QString file = QFileDialog::getSaveFileName(this, tr("Save command output"), QDir(dir).filePath(name.left(120) + QStringLiteral(".txt")),
                                                      tr("Text files (*.txt);;All files (*)"));
    if (file.isEmpty()) return;
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text) || out.write((text + QLatin1Char('\n')).toUtf8()) < 0 || !out.commit()) {
        QMessageBox::warning(this, tr("Save command output"), tr("Cannot write %1.").arg(QDir::toNativeSeparators(file)));
        return;
    }
    QSettings().setValue(QStringLiteral("output/lastDir"), QFileInfo(file).absolutePath());
    statusBar()->showMessage(tr("Output saved to %1").arg(QDir::toNativeSeparators(file)), 5000);
}

// Right click on a pane's title bar, or Shift+right click in a terminal.
void MainWindow::showPaneMenu(TerminalView *view, const QPoint &globalPos, int commandIndex) {
    TabPage *page = pageOf(view);
    if (!page) return;
    QMenu menu(this);

    // Command output: the one under the mouse, the last one, or any recent one.
    const QList<TerminalView::Command> &commands = view->commands();
    auto shortText = [](const QString &command) {
        return command.size() > 40 ? command.left(39) + QStringLiteral("…") : command;
    };
    if (commandIndex >= 0) {
        const QString c = shortText(commands.at(commandIndex).text);
        menu.addAction(tr("Copy output of \"%1\"").arg(c), this, [this, view, commandIndex] { copyCommandOutput(view, commandIndex); });
        menu.addAction(tr("Save output of \"%1\"...").arg(c), this, [this, view, commandIndex] { saveCommandOutput(view, commandIndex); });
    }
    QAction *last = menu.addAction(tr("Copy last command output"), this, [this, view] { copyCommandOutput(view, -1); });
    last->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    last->setEnabled(!commands.isEmpty());
    if (commands.size() > 1) {
        QMenu *recent = menu.addMenu(tr("Copy output of"));
        for (int i = int(commands.size()) - 1; i >= std::max(0, int(commands.size()) - 15); --i) {
            recent->addAction(shortText(commands.at(i).text), this, [this, view, i] { copyCommandOutput(view, i); });
        }
    }
    QAction *more = menu.addAction(tr("Skip --More-- automatically (this pane)"));
    more->setCheckable(true);
    more->setChecked(view->autoMore());
    connect(more, &QAction::toggled, view, &TerminalView::setAutoMore);
    menu.addSeparator();
    QAction *copy = menu.addAction(tr("Copy selection"), view, &TerminalView::copySelection);
    copy->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    menu.addAction(tr("Paste"), view, &TerminalView::paste)->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    menu.addSeparator();

    // Placement.
    menu.addAction(tr("Split right"), this, [this, view] { view->setFocus(); splitCurrent(Qt::Horizontal); });
    menu.addAction(tr("Split down"), this, [this, view] { view->setFocus(); splitCurrent(Qt::Vertical); });
    if (page->count() > 1) {
        QMenu *move = menu.addMenu(tr("Move pane"));
        const std::pair<QString, Qt::Key> directions[] = {
            {tr("Left"), Qt::Key_Left}, {tr("Right"), Qt::Key_Right}, {tr("Up"), Qt::Key_Up}, {tr("Down"), Qt::Key_Down}};
        for (const auto &[label, key] : directions) {
            QAction *a = move->addAction(label, this, [page, view, key] { view->setFocus(); page->moveActive(key); });
            a->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | key));
            a->setEnabled(page->neighbor(view, key) != nullptr);
        }
        QMenu *swapWith = menu.addMenu(tr("Swap with"));
        for (TerminalView *other : page->views()) {
            if (other == view) continue;
            const QString name = other->paneTitle().isEmpty() ? other->title() : other->paneTitle();
            swapWith->addAction(name, this, [page, view, other] { page->swap(view, other); });
        }
        menu.addAction(tr("Move to a new tab"), this, [this, view] { detachToNewTab(view); })
            ->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N));
    }
    if (m_tabs->count() > 1) {
        QMenu *toTab = menu.addMenu(tr("Move to tab"));
        for (int i = 0; i < m_tabs->count(); ++i) {
            TabPage *target = pageAt(i);
            if (!target || target == page) continue;
            toTab->addAction(m_tabs->tabText(i), this, [this, view, target] { moveToTab(view, target); });
        }
        if (toTab->isEmpty()) toTab->menuAction()->setVisible(false);
    }
    if (page->count() > 1) {
        menu.addAction(tr("Zoom pane"), this, [page, view] { page->toggleZoom(view); });
        menu.addSeparator();
        menu.addAction(tr("Close pane"), this, [page, view] { page->closeView(view); });
    }
    menu.exec(globalPos);
}

// The tab bar: a tab dragged down out of the bar becomes a pane drag (drop it
// on a terminal to show both side by side), and while a pane or a session is
// dragged, resting on a tab switches to it.
bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_tabs->tabBar() && tabBarDragEvent(event)) return true;
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::tabBarDragEvent(QEvent *event) {
    QTabBar *bar = m_tabs->tabBar();
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *e = static_cast<QMouseEvent *>(event);
        if (e->button() == Qt::LeftButton) {
            m_tabPressPos = e->position().toPoint();
            m_tabPressIndex = bar->tabAt(m_tabPressPos);
        }
        return false;
    }
    case QEvent::MouseButtonRelease:
        m_tabPressIndex = -1;
        return false;
    case QEvent::MouseMove: {
        auto *e = static_cast<QMouseEvent *>(event);
        if (m_tabPressIndex < 0 || !(e->buttons() & Qt::LeftButton)) return false;
        // Sideways moves reorder the tabs (QTabBar); only pulling the tab
        // down out of the bar starts a drag.
        const QPoint pos = e->position().toPoint();
        if (pos.y() < bar->height() + 12) return false;
        TabPage *page = pageAt(m_tabPressIndex);
        m_tabPressIndex = -1;
        TerminalView *view = page ? page->activeView() : nullptr;
        if (!view) return false;
        // End the tab bar's own move first, as if the mouse were released.
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(m_tabPressPos), e->globalPosition(), Qt::LeftButton,
                            Qt::NoButton, e->modifiers());
        QCoreApplication::sendEvent(bar, &release);
        view->startPaneDrag();
        return true;
    }
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto *e = static_cast<QDragMoveEvent *>(event);
        const QMimeData *mime = e->mimeData();
        const bool pane = mime->hasFormat(QLatin1String(PaneMimeType)) && TerminalView::dragSource();
        if (!pane && !mime->hasFormat(QLatin1String(SessionMimeType))) return false;
        const int index = bar->tabAt(e->position().toPoint());
        if (index != m_tabHoverIndex) {
            m_tabHoverIndex = index;
            if (index >= 0 && index != m_tabs->currentIndex()) m_tabHoverTimer->start();
            else m_tabHoverTimer->stop();
        }
        // Dropped on the bar itself: a pane gets its own tab, sessions open in new tabs.
        e->setDropAction(pane ? Qt::MoveAction : Qt::CopyAction);
        e->accept();
        return true;
    }
    case QEvent::DragLeave:
        m_tabHoverTimer->stop();
        m_tabHoverIndex = -1;
        return true;
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
        m_tabHoverTimer->stop();
        m_tabHoverIndex = -1;
        const QMimeData *mime = e->mimeData();
        if (mime->hasFormat(QLatin1String(PaneMimeType)) && TerminalView::dragSource()) {
            e->setDropAction(Qt::MoveAction);
            e->accept();
            detachToNewTab(TerminalView::dragSource());
            return true;
        }
        if (mime->hasFormat(QLatin1String(SessionMimeType))) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            const QString data = QString::fromUtf8(mime->data(QLatin1String(SessionMimeType)));
            for (const QString &id : data.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                if (const SessionInfo *s = m_store->find(id)) openSession(*s);
            }
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

// View > Theme: the colour themes, switched live, and the contrasted edges.
void MainWindow::createThemeMenu(QMenu *view) {
    QMenu *menu = view->addMenu(tr("Theme"));
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    const QList<style::ColorTheme> &list = style::themes();
    for (int i = 0; i < list.size(); ++i) {
        if (i == 3) menu->addSeparator();  // FastShell's own themes, then the VS Code classics
        const style::ColorTheme &theme = list.at(i);
        QAction *a = menu->addAction(theme.name);
        a->setCheckable(true);
        a->setChecked(theme.id == style::current().id);
        group->addAction(a);
        const QString id = theme.id;
        connect(a, &QAction::triggered, this, [id] { style::setTheme(id); });
    }
    menu->addSeparator();
    QAction *glow = menu->addAction(tr("Contrasted edges"));
    glow->setToolTip(tr("Crisp outlines between panels, panes and controls"));
    glow->setCheckable(true);
    glow->setChecked(style::glow());
    connect(glow, &QAction::toggled, this, [](bool on) { style::setGlow(on); });
    connect(style::notifier(), &style::Notifier::changed, this, &MainWindow::refreshThemeIcons);
}

// The toolbar icons are drawn in the theme colours: draw them again.
void MainWindow::refreshThemeIcons() {
    for (QAction *a : findChildren<QAction *>()) {
        const QVariant glyph = a->property("fs_glyph");
        if (glyph.isValid()) a->setIcon(style::icon(style::Glyph(glyph.toInt())));
    }
    for (QToolButton *b : findChildren<QToolButton *>()) {
        const QVariant glyph = b->property("fs_glyph");
        if (glyph.isValid()) b->setIcon(style::icon(style::Glyph(glyph.toInt())));
    }
    for (QWidget *w : findChildren<QWidget *>()) w->update();
}

// The remote desktop leaves its tab for a panel the user can dock on any side
// or float, like the Filer. Closing the panel ends the session.
void MainWindow::detachRdp(RdpView *rdp) {
    const int index = m_tabs->indexOf(rdp);
    if (index < 0) return;
    const QString title = m_tabs->tabText(index);
    const QIcon icon = m_tabs->tabIcon(index);
    m_tabs->removeTab(index);  // keeps the view (and its connection) alive

    auto *dock = new QDockWidget(title, this);
    static int count = 0;
    dock->setObjectName(QStringLiteral("rdpDock%1").arg(++count));
    dock->setWindowIcon(icon);
    dock->setAttribute(Qt::WA_DeleteOnClose);
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    dock->setWidget(rdp);
    dock->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dock, &QWidget::customContextMenuRequested, this, [this, dock, rdp](const QPoint &pos) {
        QMenu menu(this);
        menu.addAction(tr("Move back to a tab"), this, [this, dock] { attachRdp(dock); });
        if (rdp->isConnected()) {
            menu.addAction(tr("Disconnect"), rdp, &RdpView::disconnectFromHost);
        } else if (RdpView::embeddedAvailable()) {
            menu.addAction(tr("Connect"), rdp, &RdpView::connectToHost);
        }
        menu.addAction(tr("Open in the system Remote Desktop client"), rdp, &RdpView::openExternally);
        menu.addSeparator();
        menu.addAction(tr("Close"), dock, &QWidget::close);
        menu.exec(dock->mapToGlobal(pos));
    });
    // Its own column next to the Filer, half of the window wide.
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    if (m_filerDock && !m_filerDock->isFloating() && dockWidgetArea(m_filerDock) == Qt::LeftDockWidgetArea) {
        splitDockWidget(m_filerDock, dock, Qt::Horizontal);
    }
    dock->show();
    QTimer::singleShot(0, this, [this, dock] { resizeDocks({dock}, {std::max(480, width() / 2)}, Qt::Horizontal); });
    rdp->setFocus();
    if (m_tabs->count() == 0) updatePanels();
    updateStatus();
}

void MainWindow::attachRdp(QDockWidget *dock) {
    auto *rdp = qobject_cast<RdpView *>(dock->widget());
    if (!rdp) return;
    const QString title = dock->windowTitle();
    const QIcon icon = dock->windowIcon();
    const int index = m_tabs->addTab(rdp, icon, title);  // reparents the view out of the panel
    addCloseButton(rdp);
    m_tabs->setTabToolTip(index, QStringLiteral("RDP %1:%2").arg(rdp->options().host).arg(rdp->options().port));
    m_tabs->setCurrentIndex(index);
    removeDockWidget(dock);
    dock->deleteLater();
    rdp->setFocus();
}
