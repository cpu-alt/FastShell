// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Password vault: a short list of accounts (name, user, password) kept apart
// from the sessions in vault.json, passwords encrypted with DPAPI like the
// sessions. Clicking an entry logs in the current terminal: it answers a
// "login as:" / "Username:" prompt with the user, then the password prompt
// with the password. Nothing here is ever shown to the Claude bridge.
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

namespace vault {

struct Entry {
    QString id;
    QString name;  // "Switches core", "Firewall"...
    QString user;
    QByteArray protectedPassword;  // DPAPI blob, base64
    QString note;
    // Where this account is used: saved session folders ("Network/Core", with
    // their sub-folders) and saved sessions (ids).
    QStringList folders;
    QStringList sessions;
    // The user lets Claude log in with this account (FastShell types it,
    // Claude never sees the password).
    bool claude = false;

    // 2 = linked to this session, 1 = to its folder (or a parent), 0 = not.
    int linkStrength(const QString &sessionId, const QString &folder) const;

    QString password() const;
    void setPassword(const QString &password);
};

// What the last terminal line asks for.
enum class Prompt { None, User, Password };
Prompt promptKind(const QString &line);

// What to type for a prompt. Empty when the entry cannot answer it (no user
// for a user prompt, nothing asked).
QByteArray answer(Prompt prompt, const QString &user, const QString &password);

class Store : public QObject {
    Q_OBJECT

public:
    explicit Store(QObject *parent = nullptr);

    const QList<Entry> &entries() const { return m_entries; }
    const Entry *find(const QString &id) const;
    void upsert(Entry entry);  // new id when empty
    void remove(const QString &id);
    void move(int from, int to);
    // The account linked to a saved session: a direct link first, then the
    // deepest folder. claudeOnly: only accounts Claude may use. Null if none.
    const Entry *forSession(const QString &sessionId, const QString &folder, bool claudeOnly = false) const;
    // Keeps the links valid after a folder rename / session removal.
    void renameFolder(const QString &from, const QString &to);

    QString filePath() const;
    void load();
    bool save() const;

Q_SIGNALS:
    void changed();

private:
    QList<Entry> m_entries;
};

}  // namespace vault
