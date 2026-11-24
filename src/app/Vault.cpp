// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/Vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

#include "app/McpProtocol.h"
#include "app/SessionStore.h"

namespace vault {

QString Entry::password() const {
    return unprotectSecret(protectedPassword);
}

void Entry::setPassword(const QString &password) {
    protectedPassword = protectSecret(password);
}

int Entry::linkStrength(const QString &sessionId, const QString &folder) const {
    if (!sessionId.isEmpty() && sessions.contains(sessionId)) return 2;
    for (const QString &f : folders) {
        if (!f.isEmpty() && (folder == f || folder.startsWith(f + QLatin1Char('/')))) return 1;
    }
    return 0;
}

Prompt promptKind(const QString &line) {
    const QString l = line.trimmed();
    if (l.isEmpty() || l.size() > 80) return Prompt::None;
    if (mcp::isPasswordPrompt(l)) return Prompt::Password;
    // "login as:", "Username:", "User Name:", "login:", "User:", "Nom d'utilisateur :"...
    // Anchored at the end, so "Last login: Tue Nov 24..." does not match.
    static const QRegularExpression user(
        QStringLiteral(R"((login as|user ?name|login|user|utilisateur|identifiant)\s*:$)"),
        QRegularExpression::CaseInsensitiveOption);
    return user.match(l).hasMatch() ? Prompt::User : Prompt::None;
}

QByteArray answer(Prompt prompt, const QString &user, const QString &password) {
    switch (prompt) {
    case Prompt::User: return user.isEmpty() ? QByteArray() : user.toUtf8() + '\r';
    case Prompt::Password: return password.toUtf8() + '\r';
    case Prompt::None: break;
    }
    return {};
}

// --- store -----------------------------------------------------------------

Store::Store(QObject *parent) : QObject(parent) {}

const Entry *Store::find(const QString &id) const {
    for (const Entry &e : m_entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

void Store::upsert(Entry entry) {
    if (entry.id.isEmpty()) entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (Entry &e : m_entries) {
        if (e.id == entry.id) {
            e = std::move(entry);
            save();
            Q_EMIT changed();
            return;
        }
    }
    m_entries.append(std::move(entry));
    save();
    Q_EMIT changed();
}

void Store::remove(const QString &id) {
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id) {
            m_entries.removeAt(i);
            save();
            Q_EMIT changed();
            return;
        }
    }
}

void Store::move(int from, int to) {
    if (from < 0 || to < 0 || from >= m_entries.size() || to >= m_entries.size() || from == to) return;
    m_entries.move(from, to);
    save();
    Q_EMIT changed();
}

const Entry *Store::forSession(const QString &sessionId, const QString &folder, bool claudeOnly) const {
    const Entry *best = nullptr;
    int bestStrength = 0;
    qsizetype bestDepth = -1;
    for (const Entry &e : m_entries) {
        if (claudeOnly && !e.claude) continue;
        const int strength = e.linkStrength(sessionId, folder);
        if (strength == 0) continue;
        qsizetype depth = 0;  // deepest matching folder wins
        if (strength == 1) {
            for (const QString &f : e.folders) {
                if (folder == f || folder.startsWith(f + QLatin1Char('/'))) depth = std::max(depth, f.size());
            }
        }
        if (strength > bestStrength || (strength == bestStrength && depth > bestDepth)) {
            best = &e;
            bestStrength = strength;
            bestDepth = depth;
        }
    }
    return best;
}

void Store::renameFolder(const QString &from, const QString &to) {
    bool changed = false;
    for (Entry &e : m_entries) {
        for (QString &f : e.folders) {
            if (f == from || f.startsWith(from + QLatin1Char('/'))) {
                f = to + f.mid(from.size());
                changed = true;
            }
        }
    }
    if (changed) {
        save();
        Q_EMIT this->changed();
    }
}

QString Store::filePath() const {
    return SessionStore::dataDirectory() + QStringLiteral("/vault.json");
}

void Store::load() {
    m_entries.clear();
    QFile file(filePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonArray list = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("entries")).toArray();
        for (const QJsonValue &v : list) {
            const QJsonObject o = v.toObject();
            Entry e;
            e.id = o.value(QStringLiteral("id")).toString();
            e.name = o.value(QStringLiteral("name")).toString();
            e.user = o.value(QStringLiteral("user")).toString();
            e.protectedPassword = o.value(QStringLiteral("password")).toString().toLatin1();
            e.note = o.value(QStringLiteral("note")).toString();
            for (const QJsonValue &f : o.value(QStringLiteral("folders")).toArray()) e.folders << f.toString();
            for (const QJsonValue &id : o.value(QStringLiteral("sessions")).toArray()) e.sessions << id.toString();
            e.claude = o.value(QStringLiteral("claude")).toBool();
            if (e.id.isEmpty()) e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_entries.append(std::move(e));
        }
    }
    Q_EMIT changed();
}

bool Store::save() const {
    QJsonArray list;
    for (const Entry &e : m_entries) {
        QJsonObject o{{QStringLiteral("id"), e.id}, {QStringLiteral("name"), e.name}, {QStringLiteral("user"), e.user}};
        if (!e.protectedPassword.isEmpty()) o.insert(QStringLiteral("password"), QString::fromLatin1(e.protectedPassword));
        if (!e.note.isEmpty()) o.insert(QStringLiteral("note"), e.note);
        if (!e.folders.isEmpty()) o.insert(QStringLiteral("folders"), QJsonArray::fromStringList(e.folders));
        if (!e.sessions.isEmpty()) o.insert(QStringLiteral("sessions"), QJsonArray::fromStringList(e.sessions));
        if (e.claude) o.insert(QStringLiteral("claude"), true);
        list.append(o);
    }
    const QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("entries"), list}};
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile file(filePath());  // atomic
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

}  // namespace vault
