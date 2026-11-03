// Copyright 2026 WindTerm NG contributors. Apache-2.0.
//
// Saved sessions organised in folders, stored as JSON in the user's
// application data directory. Passwords are encrypted for the current
// Windows user with DPAPI (never written in clear text).
#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <optional>

struct SessionInfo {
    enum class Type { Local, Ssh, Telnet, Serial };

    QString id;
    QString name;
    QString folder;  // "Servers/Prod" style path; empty = root
    Type type = Type::Ssh;
    QColor color;

    // Local shell
    QString program;
    QStringList arguments;
    QString workingDirectory;

    // Remote
    QString host;
    int port = 22;
    QString user;
    QString identityFile;
    QByteArray protectedPassword;  // DPAPI blob, base64
    bool legacyAlgorithms = false; // old devices: also allow SHA-1 / CBC / group1-sha1

    // Serial
    QString serialPort;          // COM3, /dev/ttyUSB0
    int baudRate = 9600;
    int dataBits = 8;            // 5-8
    QChar parity = QLatin1Char('N');  // N, E, O, M, S
    QString stopBits = QStringLiteral("1");  // 1, 1.5, 2
    QString flowControl = QStringLiteral("none");  // none, rtscts, xonxoff
    bool sendCrLf = false;
    bool backspaceCtrlH = false;  // Backspace sends ^H instead of DEL

    QString credentialId;  // shared user (see Credential), empty = own fields
    QString profileId;     // device profile / tag (Cisco, FS...), empty = generic

    bool builtin = false;  // auto-detected shell, not saved

    QString password() const;
    void setPassword(const QString &password);
    QString typeName() const;
};

// A user account shared by many sessions ("admin" on all the switches...).
struct Credential {
    QString id;
    QString name;  // shown in the lists
    QString user;
    QByteArray protectedPassword;  // DPAPI blob, base64
    QString identityFile;

    QString password() const;
    void setPassword(const QString &password);
};

// Connection preset and tag for a family of devices.
struct DeviceProfile {
    QString id;   // "cisco", "fs" for the built-in ones
    QString name;
    QString tag;  // short label shown in the session tree
    QColor color;
    int baudRate = 9600;
    int dataBits = 8;
    QChar parity = QLatin1Char('N');
    QString stopBits = QStringLiteral("1");
    QString flowControl = QStringLiteral("none");
    bool sendCrLf = false;
    bool backspaceCtrlH = false;
    bool builtin = false;
};

class SessionStore : public QObject {
    Q_OBJECT

public:
    explicit SessionStore(QObject *parent = nullptr);

    const QList<SessionInfo> &sessions() const { return m_sessions; }
    const QStringList &folders() const { return m_folders; }
    const QList<SessionInfo> &detectedShells() const { return m_shells; }
    const SessionInfo *find(const QString &id) const;

    void upsert(SessionInfo session);
    void remove(const QString &id);
    void moveToFolder(const QString &id, const QString &folder);
    void addFolder(const QString &folder);
    void renameFolder(const QString &from, const QString &to);
    void removeFolder(const QString &folder);  // and everything inside
    QColor folderColor(const QString &folder) const { return m_folderColors.value(folder); }
    void setFolderColor(const QString &folder, const QColor &color);  // invalid = default

    const QList<Credential> &credentials() const { return m_credentials; }
    const Credential *findCredential(const QString &id) const;
    void upsertCredential(Credential credential);
    void removeCredential(const QString &id);

    const QList<DeviceProfile> &profiles() const { return m_profiles; }
    const DeviceProfile *findProfile(const QString &id) const;
    void upsertProfile(DeviceProfile profile);
    void removeProfile(const QString &id);  // built-in ones cannot be removed

    // --- import / export ------------------------------------------------
    enum class ExportLogins {
        None,               // names and addresses (IP / DNS) only
        UsersOnly,          // plus user names and key paths, no password
        UsersAndPasswords,  // plus passwords, encrypted with an export password
    };
    // Exports a folder and its sub-folders (empty folder = everything).
    QByteArray exportSessions(const QString &folder, ExportLogins logins, const QString &passphrase = QString()) const;

    struct ImportResult {
        int sessions = 0;
        int credentials = 0;
        bool needsPassphrase = false;  // the file has passwords: call again with it
        QString error;
    };
    // Imports under targetFolder (empty = root). Never overwrites existing sessions.
    ImportResult importSessions(const QByteArray &data, const QString &targetFolder,
                                const QString &passphrase = QString());

    // "user@host:port", "ssh host", "telnet host:23", "COM3 115200" -> a temporary session.
    static std::optional<SessionInfo> parseQuickConnect(const QString &text);

    QString filePath() const;

Q_SIGNALS:
    void changed();

private:
    void load();
    void save();
    void detectShells();
    void ensureBuiltinProfiles();

    QList<SessionInfo> m_sessions;
    QStringList m_folders;
    QList<SessionInfo> m_shells;
    QList<Credential> m_credentials;
    QList<DeviceProfile> m_profiles;
    QHash<QString, QColor> m_folderColors;
};
