// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/SessionStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <QUuid>

#include "core/Cryptographic.h"

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

namespace {

#ifdef _WIN32
QByteArray protect(const QByteArray &plain) {
    DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"FastShell", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    QByteArray result(reinterpret_cast<const char *>(out.pbData), qsizetype(out.cbData));
    LocalFree(out.pbData);
    return result;
}

QByteArray unprotect(const QByteArray &blob) {
    DATA_BLOB in{DWORD(blob.size()), reinterpret_cast<BYTE *>(const_cast<char *>(blob.constData()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    QByteArray result(reinterpret_cast<const char *>(out.pbData), qsizetype(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}
#else
// No OS keystore wired up yet on this platform: passwords are not saved.
QByteArray protect(const QByteArray &) { return {}; }
QByteArray unprotect(const QByteArray &) { return {}; }
#endif

QString typeKey(SessionInfo::Type t) {
    switch (t) {
    case SessionInfo::Type::Local: return QStringLiteral("local");
    case SessionInfo::Type::Telnet: return QStringLiteral("telnet");
    case SessionInfo::Type::Serial: return QStringLiteral("serial");
    case SessionInfo::Type::Rdp: return QStringLiteral("rdp");
    default: return QStringLiteral("ssh");
    }
}

SessionInfo::Type typeFromKey(const QString &key) {
    if (key == QLatin1String("local")) return SessionInfo::Type::Local;
    if (key == QLatin1String("telnet")) return SessionInfo::Type::Telnet;
    if (key == QLatin1String("serial")) return SessionInfo::Type::Serial;
    if (key == QLatin1String("rdp")) return SessionInfo::Type::Rdp;
    return SessionInfo::Type::Ssh;
}

QString cleanFolder(QString folder) {
    folder.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QStringList parts = folder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (QString &p : parts) p = p.trimmed();
    parts.removeAll(QString());
    return parts.join(QLatin1Char('/'));
}

bool inFolder(const QString &path, const QString &folder) {
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}


// --- JSON conversion (shared by the session file and import/export) ---------

QJsonObject toJson(const SessionInfo &s, bool withLogins) {
    QJsonObject o{
        {QStringLiteral("id"), s.id},
        {QStringLiteral("name"), s.name},
        {QStringLiteral("folder"), s.folder},
        {QStringLiteral("type"), typeKey(s.type)},
    };
    if (s.color.isValid()) o.insert(QStringLiteral("color"), s.color.name());
    if (!s.os.isEmpty()) o.insert(QStringLiteral("os"), s.os);
    if (s.type == SessionInfo::Type::Local) {
        o.insert(QStringLiteral("program"), s.program);
        o.insert(QStringLiteral("arguments"), QJsonArray::fromStringList(s.arguments));
        o.insert(QStringLiteral("workingDirectory"), s.workingDirectory);
    } else if (s.type == SessionInfo::Type::Serial) {
        o.insert(QStringLiteral("serialPort"), s.serialPort);
        o.insert(QStringLiteral("baudRate"), s.baudRate);
        o.insert(QStringLiteral("dataBits"), s.dataBits);
        o.insert(QStringLiteral("parity"), QString(s.parity));
        o.insert(QStringLiteral("stopBits"), s.stopBits);
        o.insert(QStringLiteral("flowControl"), s.flowControl);
        if (s.sendCrLf) o.insert(QStringLiteral("sendCrLf"), true);
        if (s.backspaceCtrlH) o.insert(QStringLiteral("backspaceCtrlH"), true);
    } else {
        o.insert(QStringLiteral("host"), s.host);
        o.insert(QStringLiteral("port"), s.port);
        if (s.legacyAlgorithms) o.insert(QStringLiteral("legacyAlgorithms"), true);
        if (withLogins) {
            o.insert(QStringLiteral("user"), s.user);
            o.insert(QStringLiteral("identityFile"), s.identityFile);
            if (!s.credentialId.isEmpty()) o.insert(QStringLiteral("credential"), s.credentialId);
        }
    }
    if (!s.profileId.isEmpty()) o.insert(QStringLiteral("profile"), s.profileId);
    return o;
}

SessionInfo sessionFromJson(const QJsonObject &o) {
    SessionInfo s;
    s.id = o.value(QStringLiteral("id")).toString();
    s.name = o.value(QStringLiteral("name")).toString();
    s.folder = cleanFolder(o.value(QStringLiteral("folder")).toString());
    s.type = typeFromKey(o.value(QStringLiteral("type")).toString());
    s.color = QColor(o.value(QStringLiteral("color")).toString());
    s.os = o.value(QStringLiteral("os")).toString();
    s.program = o.value(QStringLiteral("program")).toString();
    for (const QJsonValue &a : o.value(QStringLiteral("arguments")).toArray()) s.arguments << a.toString();
    s.workingDirectory = o.value(QStringLiteral("workingDirectory")).toString();
    s.host = o.value(QStringLiteral("host")).toString();
    s.port = o.value(QStringLiteral("port")).toInt(SessionInfo::defaultPort(s.type));
    s.user = o.value(QStringLiteral("user")).toString();
    s.identityFile = o.value(QStringLiteral("identityFile")).toString();
    s.protectedPassword = o.value(QStringLiteral("password")).toString().toLatin1();
    s.legacyAlgorithms = o.value(QStringLiteral("legacyAlgorithms")).toBool();
    s.serialPort = o.value(QStringLiteral("serialPort")).toString();
    s.baudRate = o.value(QStringLiteral("baudRate")).toInt(9600);
    s.dataBits = o.value(QStringLiteral("dataBits")).toInt(8);
    const QString parity = o.value(QStringLiteral("parity")).toString();
    s.parity = parity.isEmpty() ? QLatin1Char('N') : parity.at(0);
    s.stopBits = o.value(QStringLiteral("stopBits")).toString(QStringLiteral("1"));
    s.flowControl = o.value(QStringLiteral("flowControl")).toString(QStringLiteral("none"));
    s.sendCrLf = o.value(QStringLiteral("sendCrLf")).toBool();
    s.backspaceCtrlH = o.value(QStringLiteral("backspaceCtrlH")).toBool();
    s.credentialId = o.value(QStringLiteral("credential")).toString();
    s.profileId = o.value(QStringLiteral("profile")).toString();
    return s;
}

QJsonObject toJson(const Credential &c) {
    return QJsonObject{{QStringLiteral("id"), c.id},
                       {QStringLiteral("name"), c.name},
                       {QStringLiteral("user"), c.user},
                       {QStringLiteral("identityFile"), c.identityFile}};
}

Credential credentialFromJson(const QJsonObject &o) {
    Credential c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.name = o.value(QStringLiteral("name")).toString();
    c.user = o.value(QStringLiteral("user")).toString();
    c.protectedPassword = o.value(QStringLiteral("password")).toString().toLatin1();
    c.identityFile = o.value(QStringLiteral("identityFile")).toString();
    return c;
}

QJsonObject toJson(const DeviceProfile &p) {
    return QJsonObject{
        {QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}, {QStringLiteral("tag"), p.tag},
        {QStringLiteral("color"), p.color.isValid() ? p.color.name() : QString()},
        {QStringLiteral("baudRate"), p.baudRate}, {QStringLiteral("dataBits"), p.dataBits},
        {QStringLiteral("parity"), QString(p.parity)}, {QStringLiteral("stopBits"), p.stopBits},
        {QStringLiteral("flowControl"), p.flowControl}, {QStringLiteral("sendCrLf"), p.sendCrLf},
        {QStringLiteral("backspaceCtrlH"), p.backspaceCtrlH}};
}

DeviceProfile profileFromJson(const QJsonObject &o) {
    DeviceProfile p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.name = o.value(QStringLiteral("name")).toString();
    p.tag = o.value(QStringLiteral("tag")).toString();
    p.color = QColor(o.value(QStringLiteral("color")).toString());
    p.baudRate = o.value(QStringLiteral("baudRate")).toInt(9600);
    p.dataBits = o.value(QStringLiteral("dataBits")).toInt(8);
    const QString parity = o.value(QStringLiteral("parity")).toString();
    p.parity = parity.isEmpty() ? QLatin1Char('N') : parity.at(0);
    p.stopBits = o.value(QStringLiteral("stopBits")).toString(QStringLiteral("1"));
    p.flowControl = o.value(QStringLiteral("flowControl")).toString(QStringLiteral("none"));
    p.sendCrLf = o.value(QStringLiteral("sendCrLf")).toBool();
    p.backspaceCtrlH = o.value(QStringLiteral("backspaceCtrlH")).toBool();
    return p;
}

QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

constexpr char ExportFormat[] = "emfhexa-sessions";
constexpr int ExportPbkdf2Iterations = 600000;  // OWASP 2023 for PBKDF2-HMAC-SHA256
// An imported file chooses its own iteration count: refuse absurd values
// (too weak, or so large that importing would hang).
constexpr int MinImportIterations = 100000;
constexpr int MaxImportIterations = 10000000;

}  // namespace


QString SessionInfo::password() const {
    return protectedPassword.isEmpty() ? QString()
                                       : QString::fromUtf8(unprotect(QByteArray::fromBase64(protectedPassword)));
}

void SessionInfo::setPassword(const QString &password) {
    protectedPassword = password.isEmpty() ? QByteArray() : protect(password.toUtf8()).toBase64();
}

QString SessionInfo::typeName() const {
    switch (type) {
    case Type::Local: return QStringLiteral("Shell");
    case Type::Telnet: return QStringLiteral("Telnet");
    case Type::Serial: return QStringLiteral("Serial");
    case Type::Rdp: return QStringLiteral("RDP");
    default: return QStringLiteral("SSH");
    }
}

int SessionInfo::defaultPort(Type type) {
    switch (type) {
    case Type::Telnet: return 23;
    case Type::Rdp: return 3389;
    default: return 22;
    }
}

SessionStore::SessionStore(QObject *parent) : QObject(parent) {
    load();
    ensureBuiltinProfiles();
    detectShells();
}

QString Credential::password() const {
    return protectedPassword.isEmpty() ? QString()
                                       : QString::fromUtf8(unprotect(QByteArray::fromBase64(protectedPassword)));
}

void Credential::setPassword(const QString &password) {
    protectedPassword = password.isEmpty() ? QByteArray() : protect(password.toUtf8()).toBase64();
}

void SessionStore::ensureBuiltinProfiles() {
    // Cisco console: 9600 8N1, no flow control (the Cisco default for decades).
    DeviceProfile cisco;
    cisco.id = QStringLiteral("cisco");
    cisco.name = QStringLiteral("Cisco");
    cisco.tag = QStringLiteral("CISCO");
    cisco.color = QColor(0x04, 0x9F, 0xD9);
    cisco.baudRate = 9600;
    // FS switches: console at 115200 8N1, no flow control.
    DeviceProfile fs;
    fs.id = QStringLiteral("fs");
    fs.name = QStringLiteral("FS");
    fs.tag = QStringLiteral("FS");
    fs.color = QColor(0xE5, 0x48, 0x4D);
    fs.baudRate = 115200;
    for (DeviceProfile p : {cisco, fs}) {
        p.builtin = true;
        bool found = false;
        for (DeviceProfile &existing : m_profiles) {
            if (existing.id == p.id) {
                existing.builtin = true;  // keep the user's edits
                found = true;
            }
        }
        if (!found) m_profiles.prepend(p);
    }
}

const Credential *SessionStore::findCredential(const QString &id) const {
    for (const Credential &c : m_credentials) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

void SessionStore::upsertCredential(Credential credential) {
    if (credential.id.isEmpty()) credential.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (credential.name.trimmed().isEmpty()) credential.name = credential.user;
    for (Credential &c : m_credentials) {
        if (c.id == credential.id) {
            c = credential;
            save();
            return;
        }
    }
    m_credentials << credential;
    save();
}

void SessionStore::removeCredential(const QString &id) {
    m_credentials.removeIf([&](const Credential &c) { return c.id == id; });
    for (SessionInfo &s : m_sessions) {
        if (s.credentialId == id) s.credentialId.clear();
    }
    save();
}

const DeviceProfile *SessionStore::findProfile(const QString &id) const {
    for (const DeviceProfile &p : m_profiles) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

void SessionStore::upsertProfile(DeviceProfile profile) {
    if (profile.id.isEmpty()) profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (DeviceProfile &p : m_profiles) {
        if (p.id == profile.id) {
            profile.builtin = p.builtin;
            p = profile;
            save();
            return;
        }
    }
    m_profiles << profile;
    save();
}

void SessionStore::removeProfile(const QString &id) {
    const DeviceProfile *p = findProfile(id);
    if (!p || p->builtin) return;
    m_profiles.removeIf([&](const DeviceProfile &x) { return x.id == id; });
    for (SessionInfo &s : m_sessions) {
        if (s.profileId == id) s.profileId.clear();
    }
    save();
}

namespace {
QString &dataDirectoryOverride() {
    static QString dir;
    return dir;
}
}  // namespace

void SessionStore::setDataDirectory(const QString &directory) {
    dataDirectoryOverride() = directory;
}

QString SessionStore::filePath() const {
    const QString dir = dataDirectoryOverride().isEmpty()
                            ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            : dataDirectoryOverride();
    return dir + QStringLiteral("/sessions.json");
}

const SessionInfo *SessionStore::find(const QString &id) const {
    for (const SessionInfo &s : m_sessions) {
        if (s.id == id) return &s;
    }
    for (const SessionInfo &s : m_shells) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

void SessionStore::load() {
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        // First run: a starter folder so the tree is not empty.
        m_folders << QStringLiteral("SSH sessions");
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &f : root.value(QStringLiteral("folders")).toArray()) {
        const QString folder = cleanFolder(f.toString());
        if (!folder.isEmpty() && !m_folders.contains(folder)) m_folders << folder;
    }
    for (const QJsonValue &v : root.value(QStringLiteral("sessions")).toArray()) {
        SessionInfo s = sessionFromJson(v.toObject());
        if (s.id.isEmpty()) s.id = newId();
        m_sessions << s;
    }
    const QJsonObject folderColors = root.value(QStringLiteral("folderColors")).toObject();
    for (auto it = folderColors.begin(); it != folderColors.end(); ++it) {
        const QColor c(it.value().toString());
        if (c.isValid()) m_folderColors.insert(cleanFolder(it.key()), c);
    }
    for (const QJsonValue &v : root.value(QStringLiteral("credentials")).toArray()) {
        const Credential c = credentialFromJson(v.toObject());
        if (!c.id.isEmpty()) m_credentials << c;
    }
    for (const QJsonValue &v : root.value(QStringLiteral("profiles")).toArray()) {
        const DeviceProfile p = profileFromJson(v.toObject());
        if (!p.id.isEmpty()) m_profiles << p;
    }
}

void SessionStore::save() {
    QJsonArray sessions;
    for (const SessionInfo &s : m_sessions) {
        QJsonObject o = toJson(s, true);
        // On this machine, passwords are kept as DPAPI blobs.
        if (!s.protectedPassword.isEmpty()) o.insert(QStringLiteral("password"), QString::fromLatin1(s.protectedPassword));
        sessions.append(o);
    }
    QJsonArray credentials;
    for (const Credential &c : m_credentials) {
        QJsonObject o = toJson(c);
        if (!c.protectedPassword.isEmpty()) o.insert(QStringLiteral("password"), QString::fromLatin1(c.protectedPassword));
        credentials.append(o);
    }
    QJsonArray profiles;
    for (const DeviceProfile &p : m_profiles) {
        profiles.append(toJson(p));
    }
    QJsonObject folderColors;
    for (auto it = m_folderColors.cbegin(); it != m_folderColors.cend(); ++it) {
        folderColors.insert(it.key(), it.value().name());
    }
    const QJsonObject root{
        {QStringLiteral("version"), 1},
        {QStringLiteral("folders"), QJsonArray::fromStringList(m_folders)},
        {QStringLiteral("sessions"), sessions},
        {QStringLiteral("credentials"), credentials},
        {QStringLiteral("profiles"), profiles},
        {QStringLiteral("folderColors"), folderColors},
    };
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile file(filePath());  // atomic: never leaves a half-written file
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
    Q_EMIT changed();
}

// --- import / export ---------------------------------------------------------------

QByteArray SessionStore::exportSessions(const QString &folder, ExportLogins logins, const QString &passphrase) const {
    const QString root = cleanFolder(folder);
    // Paths are stored relative to the parent of the exported folder, so
    // exporting "Sites/Paris" gives "Paris/..." in the file.
    const QString base = root.contains(QLatin1Char('/')) ? root.section(QLatin1Char('/'), 0, -2) : QString();
    auto relative = [&](const QString &path) {
        return base.isEmpty() ? path : path.mid(base.size() + 1);
    };
    const bool withLogins = logins != ExportLogins::None;
    const bool withPasswords = logins == ExportLogins::UsersAndPasswords && !passphrase.isEmpty();

    QJsonArray folders;
    QJsonObject folderColors;
    for (const QString &f : m_folders) {
        if (root.isEmpty() || inFolder(f, root)) {
            folders.append(relative(f));
            if (m_folderColors.contains(f)) folderColors.insert(relative(f), m_folderColors.value(f).name());
        }
    }

    QJsonArray sessions;
    QSet<QString> usedCredentials;
    QSet<QString> usedProfiles;
    QJsonObject secrets;  // id -> clear password, encrypted as a whole below
    for (const SessionInfo &s : m_sessions) {
        if (!root.isEmpty() && !inFolder(s.folder, root)) continue;
        SessionInfo copy = s;
        copy.folder = relative(s.folder);
        sessions.append(toJson(copy, withLogins));
        if (withLogins && !s.credentialId.isEmpty()) usedCredentials.insert(s.credentialId);
        if (!s.profileId.isEmpty()) usedProfiles.insert(s.profileId);
        if (withPasswords && !s.protectedPassword.isEmpty()) {
            secrets.insert(QStringLiteral("session:") + s.id, s.password());
        }
    }

    QJsonArray credentials;
    for (const Credential &c : m_credentials) {
        if (!usedCredentials.contains(c.id)) continue;
        credentials.append(toJson(c));
        if (withPasswords && !c.protectedPassword.isEmpty()) {
            secrets.insert(QStringLiteral("credential:") + c.id, c.password());
        }
    }
    QJsonArray profiles;
    for (const DeviceProfile &p : m_profiles) {
        if (usedProfiles.contains(p.id)) profiles.append(toJson(p));
    }

    QJsonObject out{
        {QStringLiteral("format"), QLatin1String(ExportFormat)},
        {QStringLiteral("version"), 1},
        {QStringLiteral("exported"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("folders"), folders},
        {QStringLiteral("folderColors"), folderColors},
        {QStringLiteral("sessions"), sessions},
        {QStringLiteral("credentials"), credentials},
        {QStringLiteral("profiles"), profiles},
    };
    if (withPasswords && !secrets.isEmpty()) {
        // Portable: AES-256-GCM with a key derived from the export password,
        // instead of the DPAPI blobs that only work for this Windows user.
        const QByteArray salt = Cryptographic::salt();
        const QByteArray key = Cryptographic::deriveKey(passphrase.toUtf8(), salt, ExportPbkdf2Iterations);
        out.insert(QStringLiteral("secrets"),
                   QJsonObject{{QStringLiteral("salt"), QString::fromLatin1(salt.toBase64())},
                               {QStringLiteral("iterations"), ExportPbkdf2Iterations},
                               {QStringLiteral("data"), QString::fromLatin1(Cryptographic::encrypt(
                                                            QJsonDocument(secrets).toJson(QJsonDocument::Compact), key))}});
    }
    return QJsonDocument(out).toJson(QJsonDocument::Indented);
}

SessionStore::ImportResult SessionStore::importSessions(const QByteArray &data, const QString &targetFolder,
                                                        const QString &passphrase) {
    ImportResult result;
    const QJsonObject in = QJsonDocument::fromJson(data).object();
    if (in.value(QStringLiteral("format")).toString() != QLatin1String(ExportFormat)) {
        result.error = tr("This file is not a FastShell session export.");
        return result;
    }

    QJsonObject secrets;
    const QJsonObject secretBlock = in.value(QStringLiteral("secrets")).toObject();
    if (!secretBlock.isEmpty()) {
        if (passphrase.isEmpty()) {
            result.needsPassphrase = true;
            return result;
        }
        const QByteArray salt = QByteArray::fromBase64(secretBlock.value(QStringLiteral("salt")).toString().toLatin1());
        const int iterations = secretBlock.value(QStringLiteral("iterations")).toInt(ExportPbkdf2Iterations);
        if (salt.size() < 16 || iterations < MinImportIterations || iterations > MaxImportIterations) {
            result.error = tr("The passwords in this file are not protected correctly; it was not imported.");
            return result;
        }
        const QByteArray key = Cryptographic::deriveKey(passphrase.toUtf8(), salt, iterations);
        const auto plain = Cryptographic::decrypt(secretBlock.value(QStringLiteral("data")).toString().toLatin1(), key);
        if (!plain) {
            result.error = tr("Wrong export password.");
            return result;
        }
        secrets = QJsonDocument::fromJson(*plain).object();
    }

    const QString target = cleanFolder(targetFolder);
    auto place = [&](const QString &path) {
        const QString p = cleanFolder(path);
        return target.isEmpty() ? p : (p.isEmpty() ? target : target + QLatin1Char('/') + p);
    };

    // Device profiles: keep existing ones (same id), add the others.
    for (const QJsonValue &v : in.value(QStringLiteral("profiles")).toArray()) {
        DeviceProfile p = profileFromJson(v.toObject());
        if (!p.id.isEmpty() && !findProfile(p.id)) m_profiles << p;
    }

    // Shared users: reuse an identical one, otherwise add with a new id.
    QHash<QString, QString> credentialIds;
    for (const QJsonValue &v : in.value(QStringLiteral("credentials")).toArray()) {
        Credential c = credentialFromJson(v.toObject());
        const QString oldId = c.id;
        c.protectedPassword.clear();
        const QString password = secrets.value(QStringLiteral("credential:") + oldId).toString();
        // Reuse one of your shared users only if the file proves it knows it
        // (same password). Otherwise a file could point its own servers at
        // your saved "admin" account and receive its password.
        QString id;
        for (const Credential &existing : m_credentials) {
            if (existing.user == c.user && existing.name == c.name && !password.isEmpty()
                && existing.password() == password) {
                id = existing.id;
            }
        }
        if (id.isEmpty()) {
            c.id = newId();
            if (!password.isEmpty()) c.setPassword(password);
            m_credentials << c;
            id = c.id;
            ++result.credentials;
        }
        credentialIds.insert(oldId, id);
    }

    for (const QJsonValue &f : in.value(QStringLiteral("folders")).toArray()) {
        const QString folder = place(f.toString());
        if (!folder.isEmpty() && !m_folders.contains(folder)) m_folders << folder;
    }
    const QJsonObject colors = in.value(QStringLiteral("folderColors")).toObject();
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        const QColor c(it.value().toString());
        if (c.isValid()) m_folderColors.insert(place(it.key()), c);
    }

    for (const QJsonValue &v : in.value(QStringLiteral("sessions")).toArray()) {
        SessionInfo s = sessionFromJson(v.toObject());
        // A local shell from a file would run its program on this PC with one
        // double-click: never imported.
        if (s.type == SessionInfo::Type::Local) {
            ++result.skippedLocal;
            continue;
        }
        if (s.port < 1 || s.port > 65535) s.port = SessionInfo::defaultPort(s.type);
        const QString oldId = s.id;
        s.id = newId();  // never overwrite an existing session
        s.folder = place(s.folder);
        s.protectedPassword.clear();
        s.credentialId = credentialIds.value(s.credentialId);
        const QString password = secrets.value(QStringLiteral("session:") + oldId).toString();
        if (!password.isEmpty()) s.setPassword(password);
        if (!s.folder.isEmpty() && !m_folders.contains(s.folder)) m_folders << s.folder;
        m_sessions << s;
        ++result.sessions;
    }
    if (!target.isEmpty() && !m_folders.contains(target)) m_folders << target;
    save();
    return result;
}


void SessionStore::upsert(SessionInfo session) {
    session.folder = cleanFolder(session.folder);
    if (session.id.isEmpty()) {
        session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (!session.folder.isEmpty() && !m_folders.contains(session.folder)) {
        m_folders << session.folder;
    }
    for (SessionInfo &s : m_sessions) {
        if (s.id == session.id) {
            s = session;
            save();
            return;
        }
    }
    m_sessions << session;
    save();
}

void SessionStore::remove(const QString &id) {
    m_sessions.removeIf([&](const SessionInfo &s) { return s.id == id; });
    save();
}

void SessionStore::moveToFolder(const QString &id, const QString &folder) {
    for (SessionInfo &s : m_sessions) {
        if (s.id == id) {
            s.folder = cleanFolder(folder);
            if (!s.folder.isEmpty() && !m_folders.contains(s.folder)) m_folders << s.folder;
        }
    }
    save();
}

void SessionStore::setFolderColor(const QString &folder, const QColor &color) {
    const QString f = cleanFolder(folder);
    if (color.isValid()) m_folderColors.insert(f, color);
    else m_folderColors.remove(f);
    save();
}

void SessionStore::addFolder(const QString &folder) {
    const QString clean = cleanFolder(folder);
    if (!clean.isEmpty() && !m_folders.contains(clean)) {
        m_folders << clean;
        save();
    }
}

void SessionStore::renameFolder(const QString &from, const QString &to) {
    const QString a = cleanFolder(from);
    const QString b = cleanFolder(to);
    if (a.isEmpty() || b.isEmpty() || a == b) return;
    auto rebase = [&](QString &path) {
        if (inFolder(path, a)) path = b + path.mid(a.size());
    };
    for (QString &f : m_folders) rebase(f);
    QHash<QString, QColor> colors;
    for (auto it = m_folderColors.cbegin(); it != m_folderColors.cend(); ++it) {
        QString key = it.key();
        rebase(key);
        colors.insert(key, it.value());
    }
    m_folderColors = colors;
    for (SessionInfo &s : m_sessions) rebase(s.folder);
    m_folders.removeDuplicates();
    save();
}

void SessionStore::removeFolder(const QString &folder) {
    const QString f = cleanFolder(folder);
    for (const QString &key : m_folderColors.keys()) {
        if (inFolder(key, f)) m_folderColors.remove(key);
    }
    m_folders.removeIf([&](const QString &p) { return inFolder(p, f); });
    m_sessions.removeIf([&](const SessionInfo &s) { return inFolder(s.folder, f); });
    save();
}

void SessionStore::detectShells() {
    auto addShell = [this](const QString &name, const QString &program, const QStringList &args = {}) {
        SessionInfo s;
        s.id = QStringLiteral("builtin:") + name;
        s.name = name;
        s.type = SessionInfo::Type::Local;
        s.program = program;
        s.arguments = args;
        s.builtin = true;
        m_shells << s;
    };

#ifdef _WIN32
    if (const QString pwsh = QStandardPaths::findExecutable(QStringLiteral("pwsh.exe")); !pwsh.isEmpty()) {
        addShell(QStringLiteral("PowerShell 7"), pwsh);
    }
    const QString system = QString::fromLocal8Bit(qgetenv("SystemRoot")) + QStringLiteral("/System32");
    addShell(QStringLiteral("Windows PowerShell"), system + QStringLiteral("/WindowsPowerShell/v1.0/powershell.exe"));
    addShell(QStringLiteral("cmd"), system + QStringLiteral("/cmd.exe"));
    for (const QString &root : {QStringLiteral("C:/Program Files/Git"), QStringLiteral("C:/Program Files (x86)/Git")}) {
        if (QFileInfo::exists(root + QStringLiteral("/bin/bash.exe"))) {
            addShell(QStringLiteral("Git Bash"), root + QStringLiteral("/bin/bash.exe"),
                     {QStringLiteral("--login"), QStringLiteral("-i")});
            break;
        }
    }
    // WSL distributions ("wsl -l -q" prints UTF-16).
    const QString wsl = system + QStringLiteral("/wsl.exe");
    if (QFileInfo::exists(wsl)) {
        QProcess process;
        process.start(wsl, {QStringLiteral("-l"), QStringLiteral("-q")});
        if (process.waitForFinished(2000) && process.exitCode() == 0) {
            const QByteArray raw = process.readAllStandardOutput();
            const QString text = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData()), raw.size() / 2);
            for (QString distro : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                distro = distro.trimmed().remove(QChar(0));
                if (!distro.isEmpty()) {
                    addShell(distro, wsl, {QStringLiteral("-d"), distro});
                }
            }
        }
    }
#else
    for (const QString &shell : {QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("fish")}) {
        if (const QString path = QStandardPaths::findExecutable(shell); !path.isEmpty()) {
            addShell(shell, path, {QStringLiteral("-l")});
        }
    }
#endif
}

std::optional<SessionInfo> SessionStore::parseQuickConnect(const QString &input) {
    const QString text = input.trimmed();
    if (text.isEmpty()) return std::nullopt;
    SessionInfo s;
    s.name = text;

    static const QRegularExpression serial(QStringLiteral(R"(^(COM\d+|/dev/\S+)(?:\s+(\d+))?$)"),
                                           QRegularExpression::CaseInsensitiveOption);
    if (const auto m = serial.match(text); m.hasMatch()) {
        s.type = SessionInfo::Type::Serial;
        s.serialPort = m.captured(1).toUpper().startsWith(QLatin1String("COM")) ? m.captured(1).toUpper() : m.captured(1);
        s.baudRate = m.captured(2).isEmpty() ? 9600 : m.captured(2).toInt();
        return s;
    }

    static const QRegularExpression remote(
        QStringLiteral(R"(^(?:(ssh|telnet|rdp)(?:\s+|://))?(?:([^\s@]+)@)?(\[[0-9a-fA-F:]+\]|[^\s:@/\[\]]+)(?::(\d{1,5}))?/?$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = remote.match(text);
    if (!m.hasMatch()) return std::nullopt;
    const QString scheme = m.captured(1).toLower();
    const bool telnet = scheme == QLatin1String("telnet");
    s.type = telnet ? SessionInfo::Type::Telnet
           : scheme == QLatin1String("rdp") ? SessionInfo::Type::Rdp
                                            : SessionInfo::Type::Ssh;
    s.user = telnet ? QString() : m.captured(2);
    s.host = m.captured(3);
    if (s.host.startsWith(QLatin1Char('['))) s.host = s.host.mid(1, s.host.size() - 2);  // [IPv6]
    s.port = m.captured(4).isEmpty() ? SessionInfo::defaultPort(s.type) : m.captured(4).toInt();
    if (s.port < 1 || s.port > 65535) return std::nullopt;
    return s;
}
