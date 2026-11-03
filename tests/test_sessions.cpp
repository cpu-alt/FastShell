// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>

#include "app/SessionStore.h"

namespace {

// Writes to a throw-away test location, never the user's real sessions.
struct TestStore : ::testing::Test {
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QStringLiteral("WindTermNG-tests"));
        SessionStore probe;
        QFile::remove(probe.filePath());
    }
};

SessionInfo ssh(const QString &name, const QString &folder) {
    SessionInfo s;
    s.name = name;
    s.folder = folder;
    s.type = SessionInfo::Type::Ssh;
    s.host = name + QStringLiteral(".example.com");
    return s;
}

}  // namespace

TEST_F(TestStore, SavesAndReloadsFolders) {
    {
        SessionStore store;
        store.upsert(ssh(QStringLiteral("web"), QStringLiteral("Prod/Web")));
        store.upsert(ssh(QStringLiteral("db"), QStringLiteral(" Prod / DB ")));  // cleaned
    }
    SessionStore reloaded;
    ASSERT_EQ(reloaded.sessions().size(), 2);
    EXPECT_TRUE(reloaded.folders().contains(QStringLiteral("Prod/Web")));
    EXPECT_TRUE(reloaded.folders().contains(QStringLiteral("Prod/DB")));
    EXPECT_EQ(reloaded.sessions()[1].folder, QStringLiteral("Prod/DB"));
}

TEST_F(TestStore, RenameMoveAndDeleteFolders) {
    SessionStore store;
    store.upsert(ssh(QStringLiteral("a"), QStringLiteral("Lab/Linux")));
    store.upsert(ssh(QStringLiteral("b"), QStringLiteral("Other")));
    store.renameFolder(QStringLiteral("Lab"), QStringLiteral("Test lab"));
    EXPECT_EQ(store.sessions()[0].folder, QStringLiteral("Test lab/Linux"));

    store.moveToFolder(store.sessions()[1].id, QStringLiteral("Test lab"));
    EXPECT_EQ(store.sessions()[1].folder, QStringLiteral("Test lab"));

    store.removeFolder(QStringLiteral("Test lab"));
    EXPECT_TRUE(store.sessions().isEmpty());
    EXPECT_FALSE(store.folders().contains(QStringLiteral("Test lab/Linux")));
}

TEST_F(TestStore, PasswordIsNeverStoredInClear) {
    SessionInfo s = ssh(QStringLiteral("secure"), QString());
    s.setPassword(QStringLiteral("hunter2-très-secret"));
#ifdef _WIN32
    EXPECT_EQ(s.password(), QStringLiteral("hunter2-très-secret"));
#endif
    SessionStore store;
    store.upsert(s);
    QFile file(store.filePath());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_FALSE(file.readAll().contains("hunter2"));
}

TEST_F(TestStore, DetectsLocalShells) {
    SessionStore store;
    EXPECT_FALSE(store.detectedShells().isEmpty());
    for (const SessionInfo &s : store.detectedShells()) {
        EXPECT_TRUE(s.builtin);
        EXPECT_EQ(s.type, SessionInfo::Type::Local);
    }
}

TEST_F(TestStore, SerialSettingsRoundTrip) {
    {
        SessionStore store;
        SessionInfo s;
        s.name = QStringLiteral("Switch console");
        s.type = SessionInfo::Type::Serial;
        s.serialPort = QStringLiteral("COM7");
        s.baudRate = 115200;
        s.dataBits = 7;
        s.parity = QLatin1Char('E');
        s.stopBits = QStringLiteral("2");
        s.flowControl = QStringLiteral("rtscts");
        s.sendCrLf = true;
        store.upsert(s);
    }
    SessionStore reloaded;
    ASSERT_EQ(reloaded.sessions().size(), 1);
    const SessionInfo &s = reloaded.sessions().first();
    EXPECT_EQ(s.type, SessionInfo::Type::Serial);
    EXPECT_EQ(s.serialPort, QStringLiteral("COM7"));
    EXPECT_EQ(s.baudRate, 115200);
    EXPECT_EQ(s.dataBits, 7);
    EXPECT_EQ(s.parity, QLatin1Char('E'));
    EXPECT_EQ(s.stopBits, QStringLiteral("2"));
    EXPECT_EQ(s.flowControl, QStringLiteral("rtscts"));
    EXPECT_TRUE(s.sendCrLf);
}

TEST_F(TestStore, BuiltinCiscoAndFsProfiles) {
    SessionStore store;
    const DeviceProfile *cisco = store.findProfile(QStringLiteral("cisco"));
    const DeviceProfile *fs = store.findProfile(QStringLiteral("fs"));
    ASSERT_NE(cisco, nullptr);
    ASSERT_NE(fs, nullptr);
    EXPECT_EQ(cisco->tag, QStringLiteral("CISCO"));
    EXPECT_EQ(cisco->baudRate, 9600);
    EXPECT_EQ(fs->tag, QStringLiteral("FS"));
    EXPECT_EQ(fs->baudRate, 115200);
    store.removeProfile(QStringLiteral("cisco"));  // built-in: refused
    EXPECT_NE(store.findProfile(QStringLiteral("cisco")), nullptr);

    DeviceProfile edited = *fs;
    edited.baudRate = 9600;  // user edits are kept across restarts
    store.upsertProfile(edited);
    SessionStore reloaded;
    EXPECT_EQ(reloaded.findProfile(QStringLiteral("fs"))->baudRate, 9600);
    EXPECT_TRUE(reloaded.findProfile(QStringLiteral("fs"))->builtin);
}

TEST_F(TestStore, SharedUsers) {
    QString credentialId;
    {
        SessionStore store;
        Credential c;
        c.name = QStringLiteral("Admin switches");
        c.user = QStringLiteral("witzn");
        c.setPassword(QStringLiteral("s3cret-switch"));
        store.upsertCredential(c);
        credentialId = store.credentials().first().id;

        SessionInfo s = ssh(QStringLiteral("sw1"), QString());
        s.credentialId = credentialId;
        s.profileId = QStringLiteral("cisco");
        store.upsert(s);

        QFile file(store.filePath());
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        EXPECT_FALSE(file.readAll().contains("s3cret-switch"));
    }
    SessionStore reloaded;
    const Credential *c = reloaded.findCredential(credentialId);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->user, QStringLiteral("witzn"));
#ifdef _WIN32
    EXPECT_EQ(c->password(), QStringLiteral("s3cret-switch"));
#endif
    EXPECT_EQ(reloaded.sessions().first().credentialId, credentialId);
    EXPECT_EQ(reloaded.sessions().first().profileId, QStringLiteral("cisco"));

    reloaded.removeCredential(credentialId);
    EXPECT_TRUE(reloaded.sessions().first().credentialId.isEmpty());
}

namespace {
// Two switches in "Sites/Paris" sharing a user, one with its own password.
void fillSites(SessionStore &store) {
    Credential c;
    c.name = QStringLiteral("Admin");
    c.user = QStringLiteral("netadmin");
    c.setPassword(QStringLiteral("shared-pass"));
    store.upsertCredential(c);
    SessionInfo a = ssh(QStringLiteral("core-sw"), QStringLiteral("Sites/Paris"));
    a.credentialId = store.credentials().first().id;
    a.profileId = QStringLiteral("cisco");
    store.upsert(a);
    SessionInfo b = ssh(QStringLiteral("edge-sw"), QStringLiteral("Sites/Paris/Edge"));
    b.user = QStringLiteral("root");
    b.setPassword(QStringLiteral("own-pass"));
    store.upsert(b);
    store.upsert(ssh(QStringLiteral("elsewhere"), QStringLiteral("Sites/Lyon")));
    store.setFolderColor(QStringLiteral("Sites/Paris"), QColor(Qt::red));
}
}  // namespace

TEST_F(TestStore, ExportNamesAndAddressesOnly) {
    SessionStore store;
    fillSites(store);
    const QByteArray data = store.exportSessions(QStringLiteral("Sites/Paris"), SessionStore::ExportLogins::None);
    EXPECT_TRUE(data.contains("core-sw.example.com"));
    EXPECT_FALSE(data.contains("elsewhere"));       // other folder
    EXPECT_FALSE(data.contains("netadmin"));        // no users
    EXPECT_FALSE(data.contains("\"root\""));
    EXPECT_FALSE(data.contains("secrets"));
    EXPECT_TRUE(data.contains("\"Paris/Edge\""));   // paths relative to Sites
}

TEST_F(TestStore, ExportWithPasswordsRoundTrip) {
    QByteArray data;
    {
        SessionStore store;
        fillSites(store);
        data = store.exportSessions(QStringLiteral("Sites/Paris"), SessionStore::ExportLogins::UsersAndPasswords,
                                    QStringLiteral("export-key"));
        EXPECT_FALSE(data.contains("shared-pass"));  // encrypted, never in clear
        EXPECT_FALSE(data.contains("own-pass"));
        QFile::remove(store.filePath());             // import into an empty store
    }
    SessionStore target;
    auto first = target.importSessions(data, QStringLiteral("Imported"));
    EXPECT_TRUE(first.needsPassphrase);
    auto wrong = target.importSessions(data, QStringLiteral("Imported"), QStringLiteral("nope"));
    EXPECT_FALSE(wrong.error.isEmpty());
    EXPECT_EQ(target.sessions().size(), 0);

    auto ok = target.importSessions(data, QStringLiteral("Imported"), QStringLiteral("export-key"));
    EXPECT_TRUE(ok.error.isEmpty());
    EXPECT_EQ(ok.sessions, 2);
    EXPECT_EQ(ok.credentials, 1);
    EXPECT_TRUE(target.folders().contains(QStringLiteral("Imported/Paris/Edge")));
    EXPECT_EQ(target.folderColor(QStringLiteral("Imported/Paris")), QColor(Qt::red));
    const SessionInfo *edge = nullptr;
    const SessionInfo *core = nullptr;
    for (const SessionInfo &s : target.sessions()) {
        if (s.name == QStringLiteral("edge-sw")) edge = &s;
        if (s.name == QStringLiteral("core-sw")) core = &s;
    }
    ASSERT_NE(edge, nullptr);
    ASSERT_NE(core, nullptr);
    EXPECT_EQ(core->profileId, QStringLiteral("cisco"));
#ifdef _WIN32
    EXPECT_EQ(edge->password(), QStringLiteral("own-pass"));
    EXPECT_EQ(target.findCredential(core->credentialId)->password(), QStringLiteral("shared-pass"));
#endif
}

TEST_F(TestStore, ImportRejectsOtherFiles) {
    SessionStore store;
    EXPECT_FALSE(store.importSessions("{\"hello\": 1}", QString()).error.isEmpty());
    EXPECT_FALSE(store.importSessions("not json", QString()).error.isEmpty());
}

TEST(QuickConnect, Parses) {
    auto a = SessionStore::parseQuickConnect(QStringLiteral("admin@10.0.0.1"));
    ASSERT_TRUE(a);
    EXPECT_EQ(a->type, SessionInfo::Type::Ssh);
    EXPECT_EQ(a->user, QStringLiteral("admin"));
    EXPECT_EQ(a->host, QStringLiteral("10.0.0.1"));
    EXPECT_EQ(a->port, 22);

    auto b = SessionStore::parseQuickConnect(QStringLiteral("ssh sw-core.local:2222"));
    ASSERT_TRUE(b);
    EXPECT_EQ(b->host, QStringLiteral("sw-core.local"));
    EXPECT_EQ(b->port, 2222);

    auto c = SessionStore::parseQuickConnect(QStringLiteral("telnet 192.168.1.5"));
    ASSERT_TRUE(c);
    EXPECT_EQ(c->type, SessionInfo::Type::Telnet);
    EXPECT_EQ(c->port, 23);

    auto d = SessionStore::parseQuickConnect(QStringLiteral("com3 115200"));
    ASSERT_TRUE(d);
    EXPECT_EQ(d->type, SessionInfo::Type::Serial);
    EXPECT_EQ(d->serialPort, QStringLiteral("COM3"));
    EXPECT_EQ(d->baudRate, 115200);

    auto e = SessionStore::parseQuickConnect(QStringLiteral("root@[fe80::1]:22"));
    ASSERT_TRUE(e);
    EXPECT_EQ(e->host, QStringLiteral("fe80::1"));

    EXPECT_FALSE(SessionStore::parseQuickConnect(QStringLiteral("")));
    EXPECT_FALSE(SessionStore::parseQuickConnect(QStringLiteral("two words here")));
    EXPECT_FALSE(SessionStore::parseQuickConnect(QStringLiteral("host:99999")));
}
