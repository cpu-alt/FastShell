// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Update check: only newer versions, only this project's HTTPS release
// links, and a package is accepted only with its published SHA-256.
#include <gtest/gtest.h>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "app/UpdateCheck.h"

namespace {

const QString Base = QStringLiteral("https://github.com/cpu-alt/FastShell/releases/download/v1.2.0/");

QJsonObject asset(const QString &name, const QString &url) {
    return QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("browser_download_url"), url}};
}

QByteArray release(const QString &tag, const QJsonArray &assets, bool prerelease = false) {
    return QJsonDocument(QJsonObject{{QStringLiteral("tag_name"), tag},
                                     {QStringLiteral("prerelease"), prerelease},
                                     {QStringLiteral("draft"), false},
                                     {QStringLiteral("body"), QStringLiteral("notes")},
                                     {QStringLiteral("html_url"), QStringLiteral("https://github.com/cpu-alt/FastShell/releases/tag/v1.2.0")},
                                     {QStringLiteral("assets"), assets}})
        .toJson();
}

QJsonArray goodAssets() {
    return {asset(QStringLiteral("FastShell-1.2.0-windows-x64.zip"), Base + QStringLiteral("FastShell-1.2.0-windows-x64.zip")),
            asset(QStringLiteral("FastShell-1.2.0-windows-x64.zip.sha256"),
                  Base + QStringLiteral("FastShell-1.2.0-windows-x64.zip.sha256"))};
}

}  // namespace

TEST(Update, ComparesVersions) {
    EXPECT_TRUE(update::isNewer(QStringLiteral("1.0.1"), QStringLiteral("1.0.0")));
    EXPECT_TRUE(update::isNewer(QStringLiteral("v1.10.0"), QStringLiteral("1.9.9")));  // not as text
    EXPECT_TRUE(update::isNewer(QStringLiteral("2.0"), QStringLiteral("1.99.99")));
    EXPECT_FALSE(update::isNewer(QStringLiteral("1.0.0"), QStringLiteral("1.0.0")));
    EXPECT_FALSE(update::isNewer(QStringLiteral("0.9.0"), QStringLiteral("1.0.0")));  // never downgrade
    EXPECT_FALSE(update::isNewer(QStringLiteral("1.0.0-evil"), QStringLiteral("0.1.0")));
    EXPECT_FALSE(update::isNewer(QString(), QStringLiteral("1.0.0")));
}

TEST(Update, ParsesTheLatestRelease) {
    const auto r = update::parseLatestRelease(release(QStringLiteral("v1.2.0"), goodAssets()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version, QStringLiteral("1.2.0"));
    EXPECT_EQ(r->packageUrl.toString(), Base + QStringLiteral("FastShell-1.2.0-windows-x64.zip"));
    EXPECT_EQ(r->checksumUrl.toString(), r->packageUrl.toString() + QStringLiteral(".sha256"));
}

TEST(Update, RejectsUntrustedReleases) {
    EXPECT_FALSE(update::parseLatestRelease("not json").has_value());
    EXPECT_FALSE(update::parseLatestRelease(release(QStringLiteral("v1.2.0"), goodAssets(), true)).has_value());  // pre-release
    EXPECT_FALSE(update::parseLatestRelease(release(QStringLiteral("latest"), goodAssets())).has_value());        // no version

    // Package hosted elsewhere, or without checksum.
    QJsonArray foreign = goodAssets();
    foreign[0] = asset(QStringLiteral("FastShell-1.2.0-windows-x64.zip"),
                       QStringLiteral("https://evil.example/FastShell-1.2.0-windows-x64.zip"));
    EXPECT_FALSE(update::parseLatestRelease(release(QStringLiteral("v1.2.0"), foreign)).has_value());
    EXPECT_FALSE(update::parseLatestRelease(release(QStringLiteral("v1.2.0"), QJsonArray{goodAssets()[0]})).has_value());
}

TEST(Update, TrustsOnlyThisProjectsHttpsReleases) {
    EXPECT_TRUE(update::isTrustedDownload(QUrl(Base + QStringLiteral("a.zip"))));
    EXPECT_FALSE(update::isTrustedDownload(QUrl(QStringLiteral("http://github.com/cpu-alt/FastShell/releases/download/v1/a.zip"))));
    EXPECT_FALSE(update::isTrustedDownload(QUrl(QStringLiteral("https://github.com/someone/FastShell/releases/download/v1/a.zip"))));
    EXPECT_FALSE(update::isTrustedDownload(QUrl(QStringLiteral("https://github.com.evil.example/cpu-alt/FastShell/releases/download/v1/a.zip"))));
    EXPECT_FALSE(update::isTrustedDownload(QUrl(QStringLiteral("https://user@github.com/cpu-alt/FastShell/releases/download/v1/a.zip"))));
    EXPECT_FALSE(update::isTrustedDownload(QUrl(QStringLiteral("https://github.com:8443/cpu-alt/FastShell/releases/download/v1/a.zip"))));
    EXPECT_FALSE(update::isTrustedDownload(QUrl(QStringLiteral("https://github.com/cpu-alt/FastShell/releases/download/../../other/a.zip"))));
}

TEST(Update, VerifiesTheChecksum) {
    const QByteArray package = "FastShell package bytes";
    const QByteArray hash = QCryptographicHash::hash(package, QCryptographicHash::Sha256).toHex();
    EXPECT_TRUE(update::checksumMatches(package, hash + "  FastShell-1.2.0-windows-x64.zip\n"));
    EXPECT_TRUE(update::checksumMatches(package, hash.toUpper()));
    EXPECT_FALSE(update::checksumMatches(package + "x", hash));    // modified package
    EXPECT_FALSE(update::checksumMatches(package, QByteArray()));  // no checksum
    EXPECT_FALSE(update::checksumMatches(package, "deadbeef"));    // not a SHA-256
}
