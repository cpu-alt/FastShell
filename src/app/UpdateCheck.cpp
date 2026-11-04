// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/UpdateCheck.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>

namespace update {

namespace {

QList<int> versionParts(QString text) {
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('v')) || text.startsWith(QLatin1Char('V'))) text.remove(0, 1);
    static const QRegularExpression pattern(QStringLiteral(R"(^(\d{1,6})\.(\d{1,6})(?:\.(\d{1,6}))?$)"));
    const auto m = pattern.match(text);
    if (!m.hasMatch()) return {};
    return {m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).isEmpty() ? 0 : m.captured(3).toInt()};
}

QString releasesPrefix() {
    return QStringLiteral("/%1/releases/download/").arg(QLatin1String(Repository));
}

}  // namespace

QUrl latestReleaseApiUrl() {
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(QLatin1String(Repository)));
}

bool isNewer(const QString &candidate, const QString &current) {
    const QList<int> a = versionParts(candidate);
    const QList<int> b = versionParts(current);
    if (a.isEmpty() || b.isEmpty()) return false;
    return a > b;  // lexicographic on (major, minor, patch)
}

bool isTrustedDownload(const QUrl &url) {
    return url.isValid() && url.scheme() == QLatin1String("https") && url.host() == QLatin1String("github.com")
        && url.port(443) == 443 && url.userInfo().isEmpty()
        && url.path().startsWith(releasesPrefix(), Qt::CaseInsensitive) && !url.path().contains(QLatin1String(".."));
}

bool checksumMatches(const QByteArray &package, const QByteArray &checksumFile) {
    const QByteArray expected = checksumFile.trimmed().split(' ').value(0).trimmed().toLower();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    if (!hex.match(QString::fromLatin1(expected)).hasMatch()) return false;
    const QByteArray actual = QCryptographicHash::hash(package, QCryptographicHash::Sha256).toHex();
    return actual == expected;
}

std::optional<Release> parseLatestRelease(const QByteArray &json) {
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.isEmpty() || root.value(QStringLiteral("draft")).toBool() || root.value(QStringLiteral("prerelease")).toBool()) {
        return std::nullopt;
    }
    Release release;
    release.version = root.value(QStringLiteral("tag_name")).toString();
    if (release.version.startsWith(QLatin1Char('v')) || release.version.startsWith(QLatin1Char('V'))) {
        release.version.remove(0, 1);
    }
    if (versionParts(release.version).isEmpty()) return std::nullopt;
    release.notes = root.value(QStringLiteral("body")).toString().left(4000);
    const QUrl page(root.value(QStringLiteral("html_url")).toString());
    if (page.scheme() == QLatin1String("https") && page.host() == QLatin1String("github.com")) release.pageUrl = page;

    for (const QJsonValue &v : root.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = v.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (!isTrustedDownload(url)) continue;
        if (name.endsWith(QLatin1String("-windows-x64.zip"), Qt::CaseInsensitive)) release.packageUrl = url;
        if (name.endsWith(QLatin1String("-windows-x64.zip.sha256"), Qt::CaseInsensitive)) release.checksumUrl = url;
    }
    if (release.packageUrl.isEmpty() || release.checksumUrl.isEmpty()) return std::nullopt;
    if (release.checksumUrl.toString() != release.packageUrl.toString() + QStringLiteral(".sha256")) return std::nullopt;
    return release;
}

}  // namespace update
