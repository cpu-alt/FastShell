// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Password vault: prompt detection, what is typed, storage.
#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

#include "app/SessionStore.h"
#include "app/Vault.h"

using vault::Prompt;

TEST(Vault, RecognisesUserPrompts) {
    for (const char *line : {"login as:", "login as: ", "Username:", "User Name:", "login:", "router login:",
                             "User:", "Nom d'utilisateur :"}) {
        EXPECT_EQ(vault::promptKind(QString::fromUtf8(line)), Prompt::User) << line;
    }
}

TEST(Vault, RecognisesPasswordPrompts) {
    for (const char *line : {"Password:", "admin@10.0.0.1's password:", "Password: ", "Mot de passe :",
                             "Enter passphrase for key 'id':"}) {
        EXPECT_EQ(vault::promptKind(QString::fromUtf8(line)), Prompt::Password) << line;
    }
}

TEST(Vault, IgnoresOtherLines) {
    for (const char *line : {"", "Switch#", "admin@host:~$", "Last login: Tue Nov 24 14:00:01 2026 from 10.0.0.2",
                             "login as: admin", "show users", "Password: changed successfully"}) {
        EXPECT_EQ(vault::promptKind(QString::fromUtf8(line)), Prompt::None) << line;
    }
    // A long line ending with "user:" is output, not a prompt.
    EXPECT_EQ(vault::promptKind(QString(100, QLatin1Char('x')) + QStringLiteral(" user:")), Prompt::None);
}

TEST(Vault, TypesTheRightThing) {
    EXPECT_EQ(vault::answer(Prompt::User, QStringLiteral("admin"), QStringLiteral("s3cret")), QByteArray("admin\r"));
    EXPECT_EQ(vault::answer(Prompt::Password, QStringLiteral("admin"), QStringLiteral("s3cret")), QByteArray("s3cret\r"));
    EXPECT_TRUE(vault::answer(Prompt::User, QString(), QStringLiteral("s3cret")).isEmpty());
    EXPECT_TRUE(vault::answer(Prompt::None, QStringLiteral("admin"), QStringLiteral("s3cret")).isEmpty());
}

TEST(Vault, SavesAndLoadsWithoutClearPasswords) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    SessionStore::setDataDirectory(dir.path());
    {
        vault::Store store;
        vault::Entry e;
        e.name = QStringLiteral("Core switches");
        e.user = QStringLiteral("admin");
        e.setPassword(QStringLiteral("Sup3r-Secret!"));
        e.note = QStringLiteral("building A");
        store.upsert(e);
        ASSERT_EQ(store.entries().size(), 1);
        EXPECT_FALSE(store.entries().first().id.isEmpty());
    }
    QFile file(dir.path() + QStringLiteral("/vault.json"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_FALSE(file.readAll().contains("Sup3r-Secret!"));

    vault::Store loaded;
    loaded.load();
    ASSERT_EQ(loaded.entries().size(), 1);
    const vault::Entry &e = loaded.entries().first();
    EXPECT_EQ(e.name, QStringLiteral("Core switches"));
    EXPECT_EQ(e.user, QStringLiteral("admin"));
    EXPECT_EQ(e.note, QStringLiteral("building A"));
#ifdef _WIN32
    EXPECT_EQ(e.password(), QStringLiteral("Sup3r-Secret!"));
#endif
    loaded.remove(e.id);
    EXPECT_TRUE(loaded.entries().isEmpty());
    SessionStore::setDataDirectory(QString());
}

TEST(Vault, LinksToFoldersAndSessions) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    SessionStore::setDataDirectory(dir.path());
    vault::Store store;
    vault::Entry network;
    network.name = QStringLiteral("network admin");
    network.folders << QStringLiteral("Network");
    vault::Entry core;
    core.name = QStringLiteral("core admin");
    core.folders << QStringLiteral("Network/Core");
    core.claude = true;
    vault::Entry special;
    special.name = QStringLiteral("special");
    special.sessions << QStringLiteral("sw-42");
    store.upsert(network);
    store.upsert(core);
    store.upsert(special);

    auto nameFor = [&](const QString &id, const QString &folder, bool claudeOnly = false) {
        const vault::Entry *e = store.forSession(id, folder, claudeOnly);
        return e ? e->name : QString();
    };
    EXPECT_EQ(nameFor(QStringLiteral("x"), QStringLiteral("Network/Access")), QStringLiteral("network admin"));
    EXPECT_EQ(nameFor(QStringLiteral("x"), QStringLiteral("Network/Core/Room 2")), QStringLiteral("core admin"));  // deepest
    EXPECT_EQ(nameFor(QStringLiteral("sw-42"), QStringLiteral("Network/Core")), QStringLiteral("special"));  // direct link
    EXPECT_EQ(nameFor(QStringLiteral("x"), QStringLiteral("Networking")), QString());  // not a sub-folder
    EXPECT_EQ(nameFor(QStringLiteral("x"), QStringLiteral("Network/Access"), true), QString());
    EXPECT_EQ(nameFor(QStringLiteral("sw-42"), QStringLiteral("Network/Core"), true), QStringLiteral("core admin"));

    store.renameFolder(QStringLiteral("Network"), QStringLiteral("LAN"));
    EXPECT_EQ(nameFor(QStringLiteral("x"), QStringLiteral("LAN/Core")), QStringLiteral("core admin"));
    vault::Store loaded;
    loaded.load();
    ASSERT_EQ(loaded.entries().size(), 3);
    EXPECT_EQ(loaded.entries().at(1).folders, QStringList{QStringLiteral("LAN/Core")});
    EXPECT_TRUE(loaded.entries().at(1).claude);
    EXPECT_EQ(loaded.entries().at(2).sessions, QStringList{QStringLiteral("sw-42")});
    SessionStore::setDataDirectory(QString());
}
