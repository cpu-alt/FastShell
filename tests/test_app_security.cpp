// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Security of the application layer: text from outside (titles, pastes,
// SFTP file names) and session files from someone else.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "app/SessionStore.h"
#include "app/TextSafety.h"

namespace {

const QChar Rlo(0x202E);  // right-to-left override, written as a number on purpose

struct AppSecurity : ::testing::Test {
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QStringLiteral("FastShell-security-tests"));
        SessionStore probe;
        QFile::remove(probe.filePath());
    }
};

QByteArray exportFile(const QJsonArray &sessions, const QJsonArray &credentials = {},
                      const QJsonObject &secrets = {}) {
    QJsonObject root{{QStringLiteral("format"), QStringLiteral("emfhexa-sessions")},
                     {QStringLiteral("sessions"), sessions},
                     {QStringLiteral("credentials"), credentials}};
    if (!secrets.isEmpty()) root.insert(QStringLiteral("secrets"), secrets);
    return QJsonDocument(root).toJson();
}

}  // namespace

// --- titles ------------------------------------------------------------------------------

TEST(TextSafety, TitlesHaveNoControlOrDirectionCharacters) {
    EXPECT_EQ(safety::displayTitle(QStringLiteral("user@host: ~")), QStringLiteral("user@host: ~"));
    // A right-to-left override (U+202E) would make "invoice<RLO>txt.exe" look like "invoiceexe.txt".
    EXPECT_EQ(safety::displayTitle(QStringLiteral("invoice") + Rlo + QStringLiteral("txt.exe")), QStringLiteral("invoice txt.exe"));
    EXPECT_EQ(safety::displayTitle(QStringLiteral("a\x1b[31mb\nc")), QStringLiteral("a [31mb c"));
    EXPECT_LE(safety::displayTitle(QString(5000, QLatin1Char('x'))).size(), 120);
}

// --- paste -------------------------------------------------------------------------------

TEST(TextSafety, PasteCarriesNoHiddenKeys) {
    // Line breaks become Enter, tabs stay; ESC, Ctrl+C, Ctrl+D... are dropped.
    EXPECT_EQ(safety::pasteBytes(QStringLiteral("ls\r\npwd\n"), false), QByteArray("ls\rpwd\r"));
    EXPECT_EQ(safety::pasteBytes(QStringLiteral("a\tb"), false), QByteArray("a\tb"));
    EXPECT_EQ(safety::pasteBytes(QStringLiteral("echo hi\x1b[201~\x03\x04\x1b"), false), QByteArray("echo hi[201~"));
    // Bracketed paste: the content cannot close the bracket early.
    const QByteArray bracketed = safety::pasteBytes(QStringLiteral("x\x1b[201~; rm -rf /\n"), true);
    EXPECT_TRUE(bracketed.startsWith("\x1b[200~"));
    EXPECT_TRUE(bracketed.endsWith("\x1b[201~"));
    EXPECT_EQ(bracketed.count("\x1b"), 2);
    EXPECT_EQ(safety::pasteBytes(QStringLiteral("café"), false), QByteArray("caf\xc3\xa9"));
}

TEST(TextSafety, CountsPastedLines) {
    EXPECT_EQ(safety::pasteLineCount(QStringLiteral("one line")), 1);
    EXPECT_EQ(safety::pasteLineCount(QStringLiteral("one line\n")), 1);  // a final Enter is not a new command
    EXPECT_EQ(safety::pasteLineCount(QStringLiteral("a\r\nb\r\nc")), 3);
    EXPECT_EQ(safety::pasteLineCount(QStringLiteral("a\nb")), 2);
}

// --- SFTP file names ---------------------------------------------------------------------

TEST(TextSafety, ServerFileNamesStayInTheDownloadFolder) {
    EXPECT_EQ(safety::localFileName(QStringLiteral("report.txt")), QStringLiteral("report.txt"));
    EXPECT_EQ(safety::localFileName(QStringLiteral("..\\..\\AppData\\evil.lnk")), QStringLiteral("evil.lnk"));
    EXPECT_EQ(safety::localFileName(QStringLiteral("../../x")), QStringLiteral("x"));
    EXPECT_EQ(safety::localFileName(QStringLiteral("C:evil.exe")), QStringLiteral("C_evil.exe"));
    EXPECT_EQ(safety::localFileName(QStringLiteral("notes.txt:hidden")), QStringLiteral("notes.txt_hidden"));
    EXPECT_EQ(safety::localFileName(QStringLiteral("CON")), QStringLiteral("_CON"));
    EXPECT_EQ(safety::localFileName(QStringLiteral("nul.txt")), QStringLiteral("_nul.txt"));
    EXPECT_EQ(safety::localFileName(QStringLiteral("trailing. . ")), QStringLiteral("trailing"));
    EXPECT_EQ(safety::localFileName(QStringLiteral("..")), QStringLiteral("download"));
    EXPECT_EQ(safety::localFileName(QStringLiteral("gpj") + Rlo + QStringLiteral(".exe")), QStringLiteral("gpj.exe"));
    EXPECT_EQ(safety::localFileName(QStringLiteral("a<b>c|d?e*f\"g")), QStringLiteral("a_b_c_d_e_f_g"));
}

TEST(TextSafety, DownloadsNeverOverwrite) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    EXPECT_EQ(safety::uniqueFilePath(dir.path(), QStringLiteral("a.txt")), dir.filePath(QStringLiteral("a.txt")));
    QFile existing(dir.filePath(QStringLiteral("a.txt")));
    ASSERT_TRUE(existing.open(QIODevice::WriteOnly));
    existing.close();
    EXPECT_EQ(safety::uniqueFilePath(dir.path(), QStringLiteral("a.txt")), dir.filePath(QStringLiteral("a (1).txt")));
}

// --- session files from someone else ----------------------------------------------------

TEST_F(AppSecurity, ImportNeverAddsLocalPrograms) {
    SessionStore store;
    const QByteArray data = exportFile(QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("x")}, {QStringLiteral("name"), QStringLiteral("looks harmless")},
                    {QStringLiteral("type"), QStringLiteral("local")},
                    {QStringLiteral("program"), QStringLiteral("cmd.exe")},
                    {QStringLiteral("arguments"), QJsonArray{QStringLiteral("/c"), QStringLiteral("evil.bat")}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("y")}, {QStringLiteral("name"), QStringLiteral("switch")},
                    {QStringLiteral("type"), QStringLiteral("ssh")}, {QStringLiteral("host"), QStringLiteral("10.0.0.1")},
                    {QStringLiteral("port"), 99999}}});
    const auto result = store.importSessions(data, QString());
    EXPECT_TRUE(result.error.isEmpty());
    EXPECT_EQ(result.sessions, 1);
    EXPECT_EQ(result.skippedLocal, 1);
    ASSERT_EQ(store.sessions().size(), 1);
    EXPECT_EQ(store.sessions().first().type, SessionInfo::Type::Ssh);
    EXPECT_EQ(store.sessions().first().port, 22);  // invalid port replaced
}

TEST_F(AppSecurity, ImportCannotBorrowYourSavedPassword) {
    SessionStore store;
    Credential admin;
    admin.name = QStringLiteral("admin");
    admin.user = QStringLiteral("admin");
    admin.setPassword(QStringLiteral("my-real-password"));
    store.upsertCredential(admin);
    const QString myId = store.credentials().first().id;

    // A file whose server uses "the same" shared user, without knowing its password.
    const QByteArray data = exportFile(
        QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("s")}, {QStringLiteral("name"), QStringLiteral("srv")},
                               {QStringLiteral("type"), QStringLiteral("ssh")},
                               {QStringLiteral("host"), QStringLiteral("attacker.example")},
                               {QStringLiteral("credential"), QStringLiteral("c")}}},
        QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("c")}, {QStringLiteral("name"), QStringLiteral("admin")},
                               {QStringLiteral("user"), QStringLiteral("admin")}}});
    ASSERT_TRUE(store.importSessions(data, QString()).error.isEmpty());
    const SessionInfo *imported = nullptr;
    for (const SessionInfo &s : store.sessions()) {
        if (s.host == QStringLiteral("attacker.example")) imported = &s;
    }
    ASSERT_NE(imported, nullptr);
    EXPECT_NE(imported->credentialId, myId);
    const Credential *used = store.findCredential(imported->credentialId);
    ASSERT_NE(used, nullptr);
#ifdef _WIN32
    EXPECT_TRUE(used->password().isEmpty());
#endif
}

TEST_F(AppSecurity, ImportRefusesAbsurdKeyDerivation) {
    SessionStore store;
    QElapsedTimer timer;
    timer.start();
    for (int iterations : {1, 2000000000}) {
        const QByteArray data = exportFile(
            QJsonArray{}, QJsonArray{},
            QJsonObject{{QStringLiteral("salt"), QString::fromLatin1(QByteArray(16, 's').toBase64())},
                        {QStringLiteral("iterations"), iterations},
                        {QStringLiteral("data"), QStringLiteral("AAAA")}});
        const auto result = store.importSessions(data, QString(), QStringLiteral("pass"));
        EXPECT_FALSE(result.error.isEmpty()) << iterations;
    }
    EXPECT_LT(timer.elapsed(), 5000);  // refused at once, no hang
}

TEST_F(AppSecurity, ExportUsesStrongKeyDerivation) {
    SessionStore store;
    SessionInfo s;
    s.name = QStringLiteral("srv");
    s.type = SessionInfo::Type::Ssh;
    s.host = QStringLiteral("10.0.0.9");
    s.setPassword(QStringLiteral("secret"));
    store.upsert(s);
    const QByteArray data = store.exportSessions(QString(), SessionStore::ExportLogins::UsersAndPasswords,
                                                 QStringLiteral("export"));
    const QJsonObject secrets = QJsonDocument::fromJson(data).object().value(QStringLiteral("secrets")).toObject();
    EXPECT_GE(secrets.value(QStringLiteral("iterations")).toInt(), 600000);
    EXPECT_FALSE(data.contains("secret\""));
}

TEST_F(AppSecurity, ImportSurvivesGarbage) {
    SessionStore store;
    for (const QByteArray &junk : {QByteArray(), QByteArray("{"), QByteArray("[]"), QByteArray(100000, '{'),
                                   QByteArray("{\"format\":\"emfhexa-sessions\",\"sessions\":\"nope\"}"),
                                   QByteArray("{\"format\":\"emfhexa-sessions\",\"sessions\":[1,null,\"x\",[]]}")}) {
        store.importSessions(junk, QStringLiteral("Imported"));
    }
    SUCCEED();
}
