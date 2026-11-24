// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Password vault: logs in the current pane with a saved account, when the
// user clicks it or, for the accounts the user allows, when Claude asks.
#include "app/MainWindow.h"

#include <QDockWidget>
#include <QLabel>
#include <QPointer>
#include <QStatusBar>
#include <QTimer>

#include "app/Fun.h"
#include "app/TerminalView.h"
#include "app/Vault.h"
#include "app/VaultPanel.h"

namespace {

constexpr int PasswordWaitMs = 15000;  // after the user name, for the password prompt
constexpr int PollMs = 100;
constexpr int MessageMs = 5000;
constexpr int MinMaskedLength = 4;  // shorter passwords would mask ordinary text

QString lastLine(const TerminalView *view) {
    const TerminalView::TextSnapshot snap = view->textSnapshot(-1, 1);
    return snap.lines.isEmpty() ? QString() : snap.lines.last();
}

void wipe(QByteArray &bytes) {
    bytes.fill('\0');
    bytes.clear();
}

}  // namespace

void MainWindow::createVault() {
    m_vault = new vault::Store(this);
    m_vault->load();
    m_vaultPanel = new VaultPanel(m_vault, m_store, this);
    connect(m_vaultPanel, &VaultPanel::loginRequested, this, qOverload<const QString &>(&MainWindow::vaultLogin));
    connect(m_store, &SessionStore::folderRenamed, m_vault, &vault::Store::renameFolder);
}

const vault::Entry *MainWindow::vaultEntryFor(TerminalView *view, bool claudeOnly) const {
    if (!view || !m_vault || !m_sessions.contains(view)) return nullptr;
    const SessionInfo &s = m_sessions[view];
    return m_vault->forSession(s.id, s.folder, claudeOnly);
}

void MainWindow::vaultLogin(const QString &id) {
    TerminalView *view = current();
    const vault::Entry *entry = m_vault->find(id);
    if (!entry) return;
    const QString error = typeVaultLogin(view, id);
    statusBar()->showMessage(error.isEmpty() ? tr("Typed the vault account \"%1\"").arg(entry->name) : error, MessageMs);
    if (view) view->setFocus();
}

void MainWindow::vaultLoginCurrent() {
    TerminalView *view = current();
    if (const vault::Entry *entry = vaultEntryFor(view, false)) {
        const QString error = typeVaultLogin(view, entry->id);
        statusBar()->showMessage(error.isEmpty() ? tr("Typed the vault account \"%1\"").arg(entry->name) : error, MessageMs);
        return;
    }
    if (auto *dock = findChild<QDockWidget *>(QStringLiteral("vaultDock"))) {
        dock->show();
        dock->raise();
    }
    statusBar()->showMessage(tr("No vault account is linked to this session: click one in the Vault panel "
                                "(right-click > Edit to link it)"),
                             MessageMs);
}

QString MainWindow::typeVaultLogin(TerminalView *view, const QString &entryId) {
    const vault::Entry *entry = m_vault->find(entryId);
    if (!entry) return tr("This vault account no longer exists.");
    if (!view || !view->isRunning()) return tr("Open a terminal first");
    const vault::Prompt prompt = vault::promptKind(lastLine(view));
    if (prompt == vault::Prompt::None) return tr("No login or password prompt in this terminal");
    QByteArray input = vault::answer(prompt, entry->user, entry->password());
    if (input.isEmpty()) return tr("\"%1\" has no user name").arg(entry->name);
    view->writeInput(input);
    wipe(input);
    if (prompt == vault::Prompt::Password) {
        m_vaultTyped[view].insert(entryId);
        fun::unlock(view, entry->name);
    }
    if (prompt != vault::Prompt::User) return {};

    // Then answer the password prompt when it shows up.
    auto *timer = new QTimer(view);
    timer->setInterval(PollMs);
    auto waited = std::make_shared<int>(0);
    QPointer<vault::Store> store(m_vault);
    connect(timer, &QTimer::timeout, view, [this, view, timer, waited, store, entryId] {
        *waited += PollMs;
        const vault::Entry *e = store ? store->find(entryId) : nullptr;
        const vault::Prompt now = vault::promptKind(lastLine(view));
        if (e && now == vault::Prompt::Password) {
            QByteArray password = vault::answer(now, e->user, e->password());
            view->writeInput(password);
            wipe(password);
            m_vaultTyped[view].insert(entryId);
            fun::unlock(view, e->name);
        }
        if (!e || now == vault::Prompt::Password || *waited >= PasswordWaitMs || !view->isRunning()) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
    return {};
}

QStringList MainWindow::maskVaultPasswords(const TerminalView *view, QStringList lines) const {
    const auto typed = m_vaultTyped.constFind(view);
    if (typed == m_vaultTyped.cend() || !m_vault) return lines;
    for (const QString &entryId : *typed) {
        const vault::Entry *entry = m_vault->find(entryId);
        if (!entry) continue;
        QString password = entry->password();
        if (password.size() >= MinMaskedLength) {
            for (QString &line : lines) line.replace(password, QStringLiteral("********"));
        }
        password.fill(QChar(0));
    }
    return lines;
}

void MainWindow::vaultLogin(int id, std::function<void(const QString &, bool)> done) {
    TerminalView *view = claudeView(id);
    const vault::Entry *entry = vaultEntryFor(view, true);
    if (!entry) {
        done(QStringLiteral("No vault account Claude may use is linked to this session."), true);
        return;
    }
    const QString name = entry->name;
    const QString error = typeVaultLogin(view, entry->id);
    if (!error.isEmpty()) {
        done(error, true);
        return;
    }
    m_statusMessage->setText(tr("Claude logged in \"%1\" with the vault account \"%2\"")
                                 .arg(m_sessions.value(view).name, name));
    done(QStringLiteral("FastShell typed the vault account \"%1\". Call read_output new_only=true to see the "
                        "result (the password follows when the device asks for it).").arg(name),
         false);
}
