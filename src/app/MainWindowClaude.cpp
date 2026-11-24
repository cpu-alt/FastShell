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
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QSaveFile>
#include <QStandardPaths>
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

#include "app/CloudTunnel.h"
#include "app/McpServer.h"
#include "app/Style.h"
#include "app/TabPage.h"
#include "app/TerminalView.h"
#include "app/Vault.h"

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

// Secret path for cloud access (/mcp/<secret>): kept across restarts, so only
// the tunnel's address changes.
QByteArray cloudSecret() {
    QSettings settings;
    QByteArray secret = settings.value(QStringLiteral("claude/remoteSecret")).toByteArray();
    if (secret.size() != 64) {
        secret = mcp::newToken();
        settings.setValue(QStringLiteral("claude/remoteSecret"), secret);
    }
    return secret;
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
    m_claudeAction = menu->addAction(tr("Let Claude use the terminals"));
    m_claudeAction->setCheckable(true);
    m_claudeAction->setToolTip(tr("Starts a local server (127.0.0.1) that Claude connects to"));
    menu->addAction(tr("Connect Claude..."), this, &MainWindow::showClaudeSetup);
    m_cloudAction = menu->addAction(tr("Cloud access for claude.ai"));
    m_cloudAction->setCheckable(true);
    m_cloudAction->setToolTip(tr("Opens a secure tunnel (cloudflared) so that Claude on the web can reach FastShell"));
    menu->addSeparator();
    menu->addAction(tr("Claude's notes..."), this, &MainWindow::showClaudeNotes);
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
        QSettings settings;
        settings.remove(QStringLiteral("claude/token"));
        settings.remove(QStringLiteral("claude/remoteSecret"));
        m_mcp->setToken(claudeToken());
        if (m_cloudAction->isChecked()) m_mcp->setRemoteSecret(cloudSecret());
        showClaudeConnection();
    });

    m_statusClaude = new QLabel(this);
    m_statusClaude->setContentsMargins(8, 0, 8, 0);
    statusBar()->addWidget(m_statusClaude);
    m_statusClaude->setCursor(Qt::PointingHandCursor);
    m_statusClaude->installEventFilter(this);  // click: Connect Claude
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateClaudeIndicator);

    m_tunnel = new CloudTunnel(this);
    connect(m_tunnel, &CloudTunnel::urlChanged, this, &MainWindow::updateClaudeIndicator);
    connect(m_tunnel, &CloudTunnel::failed, this, [this](const QString &message) {
        if (!m_cloudAction->isChecked()) return;
        QSignalBlocker block(m_cloudAction);
        m_cloudAction->setChecked(false);
        m_mcp->setRemoteSecret({});
        updateClaudeIndicator();
        QMessageBox::warning(this, tr("Claude on claude.ai"), message);
    });

    connect(m_claudeAction, &QAction::toggled, this, &MainWindow::setClaudeEnabled);
    connect(m_cloudAction, &QAction::toggled, this, &MainWindow::setCloudEnabled);
    const bool on = QSettings().value(QStringLiteral("claude/enabled"), false).toBool();
    m_claudeAction->setChecked(on);  // starts the server through toggled
    if (on && QSettings().value(QStringLiteral("claude/cloud"), false).toBool()) m_cloudAction->setChecked(true);
    updateClaudeIndicator();
}

void MainWindow::setClaudeEnabled(bool on) {
    QSettings().setValue(QStringLiteral("claude/enabled"), on);
    if (!on) {
        if (m_cloudAction && m_cloudAction->isChecked()) {
            m_cloudAction->setChecked(false);  // no server, no tunnel
            QSettings().setValue(QStringLiteral("claude/cloud"), true);  // back with the server
        }
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
        auto *off = new QLabel(tr("The connection is off: turn on Claude > Let Claude use the terminals."), &dialog);
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
    if (m_tunnel && m_tunnel->isRunning()) {
        const bool up = !m_tunnel->url().isEmpty();
        m_statusClaude->setText(m_statusClaude->text() + (up ? tr(" + cloud") : tr(" + cloud (starting)")));
        m_statusClaude->setToolTip(m_statusClaude->toolTip() + QLatin1Char('\n')
                                   + (up ? tr("Claude on claude.ai can connect through %1").arg(m_tunnel->url())
                                         : tr("Opening the tunnel...")));
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
        if (const vault::Entry *e = vaultEntryFor(v, true)) s.vault = e->name;
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
    out.lines = maskVaultPasswords(v, snap.lines);
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

void MainWindow::openSavedMany(const QStringList &names, bool sideBySide,
                               std::function<void(const QList<int> &, const QString &)> done) {
    if (!m_claudeOpenAction || !m_claudeOpenAction->isChecked()) {
        done({}, QStringLiteral("The user does not let Claude open sessions (Claude menu in FastShell)."));
        return;
    }
    QList<SessionInfo> found;
    QList<int> slots;  // index in names of each found session
    QStringList missing;
    for (qsizetype i = 0; i < names.size(); ++i) {
        const QList<SessionInfo> &all = m_store->sessions();
        const auto it = std::find_if(all.cbegin(), all.cend(), [&](const SessionInfo &s) {
            return s.type != SessionInfo::Type::Rdp && s.name.compare(names.at(i), Qt::CaseInsensitive) == 0;
        });
        if (it == all.cend()) {
            missing << names.at(i);
        } else {
            found << *it;
            slots << int(i);
        }
    }
    const QString missingText = missing.isEmpty()
                                    ? QString()
                                    : QStringLiteral("Unknown saved sessions: %1 (call list_saved_sessions).").arg(missing.join(QStringLiteral(", ")));
    if (found.isEmpty()) {
        done(QList<int>(names.size(), 0), missingText);
        return;
    }
    const int count = int(names.size());
    auto open = [this, found, slots, count, sideBySide, missingText, done](bool ok) {
        QList<int> ids(count, 0);
        if (!ok) return done(ids, QStringLiteral("The user refused."));
        if (sideBySide && found.size() > 1) {
            QList<TerminalView *> views;
            for (const SessionInfo &s : found) views << createView(s, QString());
            addPage(new TabPage(views), found.first());
            for (qsizetype i = 0; i < views.size(); ++i) ids[slots.at(i)] = claudeId(views.at(i));
        } else {
            for (qsizetype i = 0; i < found.size(); ++i) {
                openSession(found.at(i));
                if (TerminalView *v = current()) ids[slots.at(i)] = claudeId(v);
            }
        }
        done(ids, missingText);
    };
    const QString level = QSettings().value(QStringLiteral("claude/defaultAccess"), QStringLiteral("ask")).toString();
    if (level == mcp::accessName(mcp::Access::Full)) {
        open(true);
        return;
    }
    QStringList list;
    for (const SessionInfo &s : found) list << s.name;
    auto *box = new QMessageBox(QMessageBox::Question, tr("Claude"),
                                tr("Claude wants to open %n session(s)%1:", nullptr, int(found.size()))
                                    .arg(sideBySide && found.size() > 1 ? tr(" side by side") : QString()),
                                QMessageBox::Yes | QMessageBox::No, this);
    box->setInformativeText(list.join(QLatin1Char('\n')));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setTextFormat(Qt::PlainText);
    connect(box, &QMessageBox::finished, this, [box, open] { open(box->result() == QMessageBox::Yes); });
    box->open();
    box->raise();
    QApplication::alert(this);
}

void MainWindow::approveMany(const QList<int> &ids, const QString &what, std::function<void(bool)> done) {
    if (ids.size() == 1) {
        approve(ids.first(), what, std::move(done));
        return;
    }
    auto *box = new QMessageBox(QMessageBox::Question, tr("Claude"),
                                tr("Claude wants to send to %n panes:", nullptr, int(ids.size())), QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setInformativeText(what);
    box->setTextFormat(Qt::PlainText);
    box->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPushButton *sendButton = box->addButton(tr("Send"), QMessageBox::AcceptRole);
    QPushButton *always = box->addButton(tr("Always for these panes"), QMessageBox::AcceptRole);
    QPushButton *refuse = box->addButton(tr("Refuse"), QMessageBox::RejectRole);
    box->setDefaultButton(sendButton);
    box->setEscapeButton(refuse);
    QList<QPointer<TerminalView>> views;
    for (int id : ids) views << QPointer<TerminalView>(claudeView(id));
    connect(box, &QMessageBox::finished, this, [box, sendButton, always, views, done] {
        QAbstractButton *clicked = box->clickedButton();
        if (clicked == always) {
            for (const QPointer<TerminalView> &v : views) {
                if (v) v->setProperty(AccessProperty, mcp::accessName(mcp::Access::Full));
            }
        }
        done(clicked == sendButton || clicked == always);
    });
    box->open();
    box->raise();
    box->activateWindow();
    QApplication::alert(this);
}

// --- Claude's notes ------------------------------------------------------------------

namespace {
QString notesPath() {
    return SessionStore::dataDirectory() + QStringLiteral("/claude-notes.txt");
}
}  // namespace

QString MainWindow::notes() const {
    QFile file(notesPath());
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool MainWindow::setNotes(const QString &text) {
    QDir().mkpath(QFileInfo(notesPath()).absolutePath());
    QSaveFile file(notesPath());
    return file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) >= 0 && file.commit();
}

void MainWindow::showClaudeNotes() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Claude's notes"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(tr("What Claude remembers about your network between conversations (one fact per "
                                "line). You can edit or delete anything."),
                             &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *edit = new QPlainTextEdit(notes(), &dialog);
    edit->setMinimumSize(560, 300);
    layout->addWidget(edit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() == QDialog::Accepted) setNotes(edit->toPlainText());
}

// --- cloud access (claude.ai) ---------------------------------------------------

void MainWindow::setCloudEnabled(bool on) {
    QSettings().setValue(QStringLiteral("claude/cloud"), on);
    if (!on) {
        m_tunnel->stop();
        m_mcp->setRemoteSecret({});
        updateClaudeIndicator();
        return;
    }
    if (CloudTunnel::findProgram().isEmpty()) {
        {
            QSignalBlocker block(m_cloudAction);
            m_cloudAction->setChecked(false);
        }
        QSettings().setValue(QStringLiteral("claude/cloud"), false);
        showCloudConnection();  // explains how to install cloudflared
        return;
    }
    if (!m_claudeAction->isChecked()) m_claudeAction->setChecked(true);  // the tunnel leads to the local server
    if (!m_mcp->isListening()) {
        QSignalBlocker block(m_cloudAction);
        m_cloudAction->setChecked(false);
        return;
    }
    m_mcp->setRemoteSecret(cloudSecret());
    m_tunnel->start(m_mcp->port());
    // Show the address to paste in claude.ai as soon as the tunnel is up.
    auto *once = new QObject(this);
    connect(m_tunnel, &CloudTunnel::urlChanged, once, [this, once](const QString &url) {
        if (url.isEmpty()) return;
        once->deleteLater();
        showCloudConnection();
    });
    connect(m_tunnel, &CloudTunnel::failed, once, &QObject::deleteLater);
    updateClaudeIndicator();
}

void MainWindow::showCloudConnection() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Connect Claude on claude.ai"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    const QString program = CloudTunnel::findProgram();
    if (program.isEmpty()) {
        const QString command = QStringLiteral("winget install --id Cloudflare.cloudflared");
        auto *text = new QLabel(tr("Cloud access goes through cloudflared, Cloudflare's free tunnel (no account "
                                   "needed). Install it once with this command in a terminal, then turn on "
                                   "Claude > Cloud access for claude.ai."),
                                &dialog);
        text->setWordWrap(true);
        layout->addWidget(text);
        auto *edit = new QLineEdit(command, &dialog);
        edit->setReadOnly(true);
        layout->addWidget(edit);
        QPushButton *copy = buttons->addButton(tr("Copy command"), QDialogButtonBox::ActionRole);
        connect(copy, &QPushButton::clicked, &dialog, [command, copy] {
            QApplication::clipboard()->setText(command);
            copy->setText(QObject::tr("Copied"));
        });
    } else if (!m_tunnel->isRunning() || m_tunnel->url().isEmpty()) {
        auto *text = new QLabel(m_tunnel->isRunning()
                                    ? tr("The tunnel is opening: this window will show the address in a few seconds.")
                                    : tr("Turn on Claude > Cloud access for claude.ai first."),
                                &dialog);
        text->setWordWrap(true);
        layout->addWidget(text);
    } else {
        const QString url = m_tunnel->url() + QStringLiteral("/mcp/") + QString::fromLatin1(cloudSecret());
        auto *text = new QLabel(tr("In claude.ai: Settings > Connectors > Add custom connector. Name it FastShell "
                                   "and paste this address (no OAuth settings). Then turn the connector on in your "
                                   "chats or projects."),
                                &dialog);
        text->setWordWrap(true);
        layout->addWidget(text);
        auto *edit = new QLineEdit(url, &dialog);
        edit->setReadOnly(true);
        edit->setMinimumWidth(640);
        layout->addWidget(edit);
        auto *warning = new QLabel(tr("This address is like a password for your terminals: do not share it. The "
                                      "start of the address changes each time the tunnel opens (update the "
                                      "connector then); the end stays the same until Claude > New connection key."),
                                   &dialog);
        warning->setWordWrap(true);
        warning->setStyleSheet(QStringLiteral("color:#FF9F1C"));
        layout->addWidget(warning);
        QPushButton *copy = buttons->addButton(tr("Copy address"), QDialogButtonBox::ActionRole);
        connect(copy, &QPushButton::clicked, &dialog, [url, copy] {
            QApplication::clipboard()->setText(url);
            copy->setText(QObject::tr("Copied"));
        });
    }
    auto *note = new QLabel(tr("The same rules apply as for Claude Code: only the panes you share, never passwords, "
                               "and \"Ask\" panes wait for your approval here."),
                            &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addWidget(buttons);
    dialog.exec();
}

// --- Claude app on this PC --------------------------------------------------------

void MainWindow::connectClaudeDesktop() {
#ifdef _WIN32
    const QString configDir = qEnvironmentVariable("APPDATA") + QStringLiteral("/Claude");
#else
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/Claude");
#endif
    const QString configPath = configDir + QStringLiteral("/claude_desktop_config.json");
    if (!QFileInfo::exists(configDir)) {
        QMessageBox::information(this, tr("Claude app"),
                                 tr("The Claude app does not seem to be installed for this Windows user (no %1).")
                                     .arg(QDir::toNativeSeparators(configDir)));
        return;
    }

    // FastShell itself is the bridge ("--mcp-stdio"): the key stays in
    // FastShell's settings, nothing secret goes in Claude's file.
    QStringList args{QStringLiteral("--mcp-stdio")};
    const QString dataDir = SessionStore::dataDirectory();
    if (QSettings().format() == QSettings::IniFormat) args << QStringLiteral("--data-dir") << QDir::toNativeSeparators(dataDir);
    QJsonArray jsonArgs;
    for (const QString &a : args) jsonArgs.append(a);
    const QJsonObject server{{QStringLiteral("command"), QDir::toNativeSeparators(QApplication::applicationFilePath())},
                             {QStringLiteral("args"), jsonArgs}};

    QJsonObject root;
    QFile file(configPath);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Claude app"), file.errorString());
            return;
        }
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        file.close();
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, tr("Claude app"),
                                 tr("%1 is not valid JSON: fix it or remove it first.").arg(QDir::toNativeSeparators(configPath)));
            return;
        }
        root = doc.object();
    }
    QJsonObject servers = root.value(QStringLiteral("mcpServers")).toObject();
    const bool already = servers.value(QStringLiteral("fastshell")).toObject() == server;
    if (!already) {
        if (QMessageBox::question(this, tr("Claude app"),
                                  tr("Add FastShell to the Claude app's tools?\n\n%1 will be updated (a copy of the "
                                     "current file is kept as claude_desktop_config.json.bak).")
                                      .arg(QDir::toNativeSeparators(configPath))) != QMessageBox::Yes) {
            return;
        }
        if (file.exists()) {
            QFile::remove(configPath + QStringLiteral(".bak"));
            QFile::copy(configPath, configPath + QStringLiteral(".bak"));
        }
        servers.insert(QStringLiteral("fastshell"), server);
        root.insert(QStringLiteral("mcpServers"), servers);
        QSaveFile out(configPath);
        if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
            || !out.commit()) {
            QMessageBox::warning(this, tr("Claude app"), out.errorString());
            return;
        }
    }
    if (!m_claudeAction->isChecked()) m_claudeAction->setChecked(true);
    QMessageBox::information(this, tr("Claude app"),
                             (already ? tr("FastShell is already in the Claude app's tools.")
                                      : tr("FastShell was added to the Claude app's tools."))
                                 + QLatin1Char(' ')
                                 + tr("Quit the Claude app completely (tray icon > Quit) and start it again. Keep "
                                      "FastShell open while Claude uses it."));
}

// --- one window to connect Claude ----------------------------------------------------

void MainWindow::showClaudeSetup() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Connect Claude"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *state = new QLabel(&dialog);
    state->setWordWrap(true);
    auto *toggle = new QPushButton(&dialog);
    auto refreshState = [this, state, toggle] {
        const bool on = m_claudeAction->isChecked();
        state->setText(on ? tr("<b>Claude can use the terminals you share.</b> Each pane has its own access "
                               "(right click on a tab > Claude access).")
                          : tr("<b>Claude cannot use FastShell yet.</b> Turn it on, then choose how you use Claude."));
        toggle->setText(on ? tr("Turn off") : tr("Turn on"));
    };
    connect(toggle, &QPushButton::clicked, &dialog, [this, refreshState] {
        m_claudeAction->setChecked(!m_claudeAction->isChecked());
        refreshState();
    });
    refreshState();
    auto *top = new QHBoxLayout;
    top->addWidget(state, 1);
    top->addWidget(toggle);
    layout->addLayout(top);

    // One row per way of using Claude: what it is, and one button.
    auto addRow = [&](const QString &title, const QString &text, const QString &button, auto action) {
        auto *frame = new QFrame(&dialog);
        frame->setFrameShape(QFrame::StyledPanel);
        auto *row = new QHBoxLayout(frame);
        auto *label = new QLabel(QStringLiteral("<b>%1</b><br>%2").arg(title.toHtmlEscaped(), text.toHtmlEscaped()), frame);
        label->setWordWrap(true);
        auto *go = new QPushButton(button, frame);
        go->setMinimumWidth(170);
        connect(go, &QPushButton::clicked, &dialog, [this, &dialog, action, refreshState] {
            if (!m_claudeAction->isChecked()) m_claudeAction->setChecked(true);
            refreshState();
            action();
        });
        row->addWidget(label, 1);
        row->addWidget(go);
        layout->addWidget(frame);
    };
    addRow(tr("Claude Code"), tr("Claude in a terminal (claude command). One command to run once."),
           tr("Show the command..."), [this] { showClaudeConnection(); });
    addRow(tr("Claude app on this PC"), tr("The Claude desktop app. FastShell adds itself to its tools in one click."),
           tr("Add to the Claude app"), [this] { connectClaudeDesktop(); });
    addRow(tr("Claude on claude.ai"), tr("Claude on the web, on your phone or in your projects, through a secure "
                                         "tunnel. You paste one address in claude.ai."),
           tr("Set up cloud access..."), [this] {
               if (!m_cloudAction->isChecked() && !CloudTunnel::findProgram().isEmpty()) m_cloudAction->setChecked(true);
               else showCloudConnection();
           });

    auto *note = new QLabel(tr("Claude never sees or types your passwords. Accounts of the Vault that you allow "
                               "(edit an account > Claude may log in) are typed by FastShell itself."),
                            &dialog);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:%1").arg(style::current().muted.name()));
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(620, dialog.sizeHint().height());
    dialog.exec();
}
