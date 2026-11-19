// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Claude Code bridge, GUI side: the Claude menu, the access level of each
// pane, the confirmation box and the status bar indicator. The protocol and
// the password rules are in McpProtocol (tested).
#include "app/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "app/McpServer.h"
#include "app/TabPage.h"
#include "app/TerminalView.h"

namespace {
constexpr quint16 DefaultPort = 47821;
constexpr char IdProperty[] = "fs_claudeId";
constexpr char AccessProperty[] = "fs_claudeAccess";
constexpr qint64 ConnectedForMs = 60 * 1000;  // "connected" while Claude called within a minute

mcp::Access accessFromName(const QString &name) {
    for (mcp::Access a : {mcp::Access::Hidden, mcp::Access::ReadOnly, mcp::Access::Ask, mcp::Access::Full}) {
        if (mcp::accessName(a) == name) return a;
    }
    return mcp::Access::Ask;
}

QByteArray claudeToken() {
    QSettings settings;
    QByteArray token = settings.value(QStringLiteral("claude/token")).toByteArray();
    if (token.size() != 64) {
        token = mcp::newToken();
        settings.setValue(QStringLiteral("claude/token"), token);
    }
    return token;
}

quint16 claudePort() {
    const int port = QSettings().value(QStringLiteral("claude/port"), DefaultPort).toInt();
    return port > 1024 && port < 65536 ? quint16(port) : DefaultPort;
}
}  // namespace

void MainWindow::createClaude() {
    m_mcp = new McpServer(this, this);

    QMenu *menu = new QMenu(tr("&Claude"), this);
    const QList<QAction *> menus = menuBar()->actions();
    menuBar()->insertMenu(menus.isEmpty() ? nullptr : menus.last(), menu);  // before Help
    m_claudeAction = menu->addAction(tr("Let Claude Code use the terminals"));
    m_claudeAction->setCheckable(true);
    m_claudeAction->setToolTip(tr("Starts a local server (127.0.0.1) that Claude Code connects to"));
    menu->addAction(tr("Connect Claude Code..."), this, &MainWindow::showClaudeConnection);
    menu->addSeparator();
    m_claudeOpenAction = menu->addAction(tr("Let Claude open saved sessions"));
    m_claudeOpenAction->setCheckable(true);
    m_claudeOpenAction->setToolTip(tr("Claude sees the names of your saved sessions and can open them (FastShell "
                                      "logs in; Claude never sees the passwords)"));
    m_claudeOpenAction->setChecked(QSettings().value(QStringLiteral("claude/allowOpen"), true).toBool());
    connect(m_claudeOpenAction, &QAction::toggled, this,
            [](bool on) { QSettings().setValue(QStringLiteral("claude/allowOpen"), on); });
    QMenu *defaults = menu->addMenu(tr("New panes"));
    auto *group = new QActionGroup(defaults);
    const QString current = QSettings().value(QStringLiteral("claude/defaultAccess"), QStringLiteral("ask")).toString();
    const std::pair<mcp::Access, QString> levels[] = {{mcp::Access::Hidden, tr("Hidden from Claude")},
                                                      {mcp::Access::ReadOnly, tr("Read only")},
                                                      {mcp::Access::Ask, tr("Ask before each command")},
                                                      {mcp::Access::Full, tr("Allow commands")}};
    for (const auto &[level, label] : levels) {
        QAction *a = defaults->addAction(label);
        a->setCheckable(true);
        a->setChecked(mcp::accessName(level) == current);
        group->addAction(a);
        connect(a, &QAction::triggered, this,
                [level] { QSettings().setValue(QStringLiteral("claude/defaultAccess"), mcp::accessName(level)); });
    }
    menu->addAction(tr("New connection key"), this, [this] {
        if (QMessageBox::question(this, tr("New connection key"),
                                  tr("Claude Code will need the new key: run the connection command again.")) !=
            QMessageBox::Yes) {
            return;
        }
        QSettings().remove(QStringLiteral("claude/token"));
        m_mcp->setToken(claudeToken());
        showClaudeConnection();
    });

    m_statusClaude = new QLabel(this);
    m_statusClaude->setContentsMargins(8, 0, 8, 0);
    statusBar()->addWidget(m_statusClaude);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateClaudeIndicator);

    connect(m_claudeAction, &QAction::toggled, this, &MainWindow::setClaudeEnabled);
    const bool on = QSettings().value(QStringLiteral("claude/enabled"), false).toBool();
    m_claudeAction->setChecked(on);  // starts the server through toggled
    updateClaudeIndicator();
}

void MainWindow::setClaudeEnabled(bool on) {
    QSettings().setValue(QStringLiteral("claude/enabled"), on);
    if (!on) {
        m_mcp->stop();
    } else if (!m_mcp->isListening() && !m_mcp->start(claudePort(), claudeToken())) {
        QMessageBox::warning(this, tr("Claude Code"),
                             tr("Port %1 is already in use on 127.0.0.1 (another FastShell?).").arg(claudePort()));
        QSignalBlocker block(m_claudeAction);
        m_claudeAction->setChecked(false);
        QSettings().setValue(QStringLiteral("claude/enabled"), false);
    }
    m_claudeLastActivity = 0;
    updateClaudeIndicator();
}

void MainWindow::showClaudeConnection() {
    const QString url = QStringLiteral("http://127.0.0.1:%1/mcp").arg(claudePort());
    const QString token = QString::fromLatin1(claudeToken());
    const QString command = QStringLiteral("claude mcp add --transport http --scope user fastshell %1 --header \"Authorization: Bearer %2\"")
                                .arg(url, token);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Connect Claude Code"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(tr("Run this command once in a terminal where Claude Code is installed, then restart "
                                "Claude Code. The key is like a password for your terminals: do not share it."),
                             &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *edit = new QPlainTextEdit(command, &dialog);
    edit->setReadOnly(true);
    edit->setMinimumSize(640, 90);
    layout->addWidget(edit);
    auto *note = new QLabel(tr("Claude only sees the panes you share (right click on a tab or a pane title > Claude "
                               "access). It never sees or types passwords: when a device asks for one, type it yourself."),
                            &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton *copy = buttons->addButton(tr("Copy command"), QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, &dialog, [command, copy] {
        QApplication::clipboard()->setText(command);
        copy->setText(QObject::tr("Copied"));
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (!m_claudeAction->isChecked()) {
        auto *off = new QLabel(tr("The connection is off: turn on Claude > Let Claude Code use the terminals."), &dialog);
        off->setStyleSheet(QStringLiteral("color:#FF9F1C"));
        layout->insertWidget(0, off);
    }
    dialog.exec();
}

void MainWindow::addClaudeAccessMenu(QMenu *menu, TerminalView *view) {
    if (!m_claudeAction || !m_claudeAction->isChecked()) return;
    QMenu *sub = menu->addMenu(tr("Claude access"));
    auto *group = new QActionGroup(sub);
    const mcp::Access current = claudeAccess(view);
    const std::pair<mcp::Access, QString> levels[] = {{mcp::Access::Hidden, tr("Hidden from Claude")},
                                                      {mcp::Access::ReadOnly, tr("Read only")},
                                                      {mcp::Access::Ask, tr("Ask before each command")},
                                                      {mcp::Access::Full, tr("Allow commands")}};
    QPointer<TerminalView> guard(view);
    for (const auto &[level, label] : levels) {
        QAction *a = sub->addAction(label);
        a->setCheckable(true);
        a->setChecked(level == current);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [guard, level] {
            if (guard) guard->setProperty(AccessProperty, mcp::accessName(level));
        });
    }
}

void MainWindow::updateClaudeIndicator() {
    if (!m_statusClaude) return;
    if (!m_mcp->isListening()) {
        m_statusClaude->hide();
        return;
    }
    const bool connected = m_claudeLastActivity && QDateTime::currentMSecsSinceEpoch() - m_claudeLastActivity < ConnectedForMs;
    if (connected) {
        m_statusClaude->setText(tr("  CLAUDE CONNECTED  "));
        m_statusClaude->setStyleSheet(QStringLiteral("background:#D97757; color:#10141A; font-weight:bold; border-radius:3px"));
        m_statusClaude->setToolTip(tr("Claude Code is using the shared terminals"));
    } else {
        m_statusClaude->setText(tr("Claude: on"));
        m_statusClaude->setStyleSheet(QStringLiteral("color:#D97757"));
        m_statusClaude->setToolTip(tr("Claude Code can connect (127.0.0.1:%1)").arg(m_mcp->port()));
    }
    m_statusClaude->show();
}

mcp::Access MainWindow::claudeAccess(TerminalView *view) const {
    QVariant v = view->property(AccessProperty);
    if (!v.isValid()) {
        v = QSettings().value(QStringLiteral("claude/defaultAccess"), QStringLiteral("ask"));
        view->setProperty(AccessProperty, v);  // later changes of the default do not touch open panes
    }
    return accessFromName(v.toString());
}

int MainWindow::claudeId(TerminalView *view) const {
    QVariant v = view->property(IdProperty);
    if (!v.isValid()) {
        v = ++m_claudeNextId;
        view->setProperty(IdProperty, v);
    }
    return v.toInt();
}

TerminalView *MainWindow::claudeView(int id) const {
    for (TerminalView *v : allViews()) {
        if (claudeId(v) == id) return v;
    }
    return nullptr;
}

QList<mcp::SessionInfo> MainWindow::sessions() const {
    QList<mcp::SessionInfo> list;
    for (TerminalView *v : allViews()) {
        mcp::SessionInfo s;
        s.id = claudeId(v);
        TabPage *page = pageOf(v);
        s.title = page ? m_tabs->tabText(m_tabs->indexOf(page)) : v->title();
        if (page && page->count() > 1 && !v->paneTitle().isEmpty()) s.title += QStringLiteral(" / ") + v->paneTitle();
        switch (m_sessions.value(v).type) {
        case SessionInfo::Type::Local: s.kind = QStringLiteral("local"); break;
        case SessionInfo::Type::Ssh: s.kind = QStringLiteral("ssh"); break;
        case SessionInfo::Type::Telnet: s.kind = QStringLiteral("telnet"); break;
        case SessionInfo::Type::Serial: s.kind = QStringLiteral("serial"); break;
        case SessionInfo::Type::Rdp: s.kind = QStringLiteral("rdp"); break;
        }
        s.running = v->isRunning();
        s.access = claudeAccess(v);
        list << s;
    }
    return list;
}

bool MainWindow::text(int id, std::int64_t from, int maxLines, mcp::Text &out) const {
    TerminalView *v = claudeView(id);
    if (!v || claudeAccess(v) == mcp::Access::Hidden) return false;
    const TerminalView::TextSnapshot snap = v->textSnapshot(from, maxLines);
    out.first = snap.first;
    out.cursorLine = snap.cursorLine;
    out.revision = snap.revision;
    out.lines = snap.lines;
    return true;
}

bool MainWindow::send(int id, const QByteArray &data) {
    TerminalView *v = claudeView(id);
    const mcp::Access access = v ? claudeAccess(v) : mcp::Access::Hidden;
    if (!v || !v->isRunning() || (access != mcp::Access::Ask && access != mcp::Access::Full)) return false;
    v->writeInput(data);
    return true;
}

void MainWindow::approve(int id, const QString &what, std::function<void(bool)> done) {
    TerminalView *v = claudeView(id);
    if (!v) {
        done(false);
        return;
    }
    const QString title = [&] {
        for (const mcp::SessionInfo &s : sessions()) {
            if (s.id == id) return s.title;
        }
        return QString();
    }();
    auto *box = new QMessageBox(QMessageBox::Question, tr("Claude Code"),
                                tr("Claude wants to send to \"%1\":").arg(title), QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setInformativeText(what);
    box->setTextFormat(Qt::PlainText);
    box->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPushButton *sendButton = box->addButton(tr("Send"), QMessageBox::AcceptRole);
    QPushButton *always = box->addButton(tr("Always for this pane"), QMessageBox::AcceptRole);
    QPushButton *refuse = box->addButton(tr("Refuse"), QMessageBox::RejectRole);
    box->setDefaultButton(sendButton);
    box->setEscapeButton(refuse);
    QPointer<TerminalView> guard(v);
    connect(box, &QMessageBox::finished, this, [box, sendButton, always, guard, done] {
        QAbstractButton *clicked = box->clickedButton();
        if (clicked == always && guard) guard->setProperty(AccessProperty, mcp::accessName(mcp::Access::Full));
        done(guard && (clicked == sendButton || clicked == always));
    });
    box->open();
    box->raise();
    box->activateWindow();
}

void MainWindow::activity() {
    m_claudeLastActivity = QDateTime::currentMSecsSinceEpoch();
    updateClaudeIndicator();
}

// The device asks for a password: show the pane and tell the user.
void MainWindow::attention(int id, const QString &message) {
    TerminalView *v = claudeView(id);
    if (!v) return;
    if (TabPage *page = pageOf(v)) m_tabs->setCurrentWidget(page);
    v->setFocus();
    m_statusMessage->setText(tr("Claude: %1").arg(message));
    QApplication::alert(this);  // flashes the taskbar button
    raise();
    activateWindow();
}

QList<mcp::SavedSession> MainWindow::savedSessions() const {
    QList<mcp::SavedSession> list;
    if (!m_claudeOpenAction || !m_claudeOpenAction->isChecked()) return list;
    for (const SessionInfo &s : m_store->sessions()) {
        if (s.type == SessionInfo::Type::Rdp) continue;
        mcp::SavedSession saved;
        saved.name = s.name;
        saved.folder = s.folder;
        switch (s.type) {
        case SessionInfo::Type::Serial:
            saved.kind = QStringLiteral("serial");
            saved.target = s.serialPort;
            break;
        case SessionInfo::Type::Telnet: saved.kind = QStringLiteral("telnet"); saved.target = s.host; break;
        case SessionInfo::Type::Local: saved.kind = QStringLiteral("local"); break;
        default: saved.kind = QStringLiteral("ssh"); saved.target = s.host; break;
        }
        list << saved;
    }
    return list;
}

void MainWindow::openSaved(const QString &name, std::function<void(int, const QString &)> done) {
    if (!m_claudeOpenAction || !m_claudeOpenAction->isChecked()) {
        done(-1, QStringLiteral("The user does not let Claude open sessions (Claude menu in FastShell)."));
        return;
    }
    const QList<SessionInfo> &all = m_store->sessions();
    const auto it = std::find_if(all.cbegin(), all.cend(), [&](const SessionInfo &s) {
        return s.type != SessionInfo::Type::Rdp && s.name.compare(name, Qt::CaseInsensitive) == 0;
    });
    if (it == all.cend()) {
        done(-1, QStringLiteral("No saved session named \"%1\": call list_saved_sessions.").arg(name));
        return;
    }
    const SessionInfo session = *it;
    auto open = [this, session, done](bool ok) {
        if (!ok) {
            done(-1, QStringLiteral("The user refused."));
            return;
        }
        openSession(session);
        TerminalView *v = current();
        if (!v) {
            done(-1, QStringLiteral("The session could not be opened."));
            return;
        }
        done(claudeId(v), QString());
    };
    // Opening a connection is asked unless new panes are fully allowed.
    const QString level = QSettings().value(QStringLiteral("claude/defaultAccess"), QStringLiteral("ask")).toString();
    if (level == mcp::accessName(mcp::Access::Full)) {
        open(true);
        return;
    }
    auto *box = new QMessageBox(QMessageBox::Question, tr("Claude Code"),
                                tr("Claude wants to open the session \"%1\".").arg(session.name),
                                QMessageBox::Yes | QMessageBox::No, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setTextFormat(Qt::PlainText);
    connect(box, &QMessageBox::finished, this, [box, open] { open(box->result() == QMessageBox::Yes); });
    box->open();
    box->raise();
    QApplication::alert(this);
}
