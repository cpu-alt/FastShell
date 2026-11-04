// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Update check against the GitHub releases of FastShell, without GUI so it
// can be tested: reads the "latest release" answer of the GitHub API, picks
// the Windows package and its checksum, and only trusts HTTPS links of the
// project's own releases. The package is installed only if its SHA-256
// matches the published one.
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <optional>

namespace update {

inline constexpr char Repository[] = "cpu-alt/FastShell";

// https://api.github.com/repos/<Repository>/releases/latest
QUrl latestReleaseApiUrl();

struct Release {
    QString version;     // "1.2.0" (tag "v1.2.0")
    QUrl packageUrl;     // FastShell-1.2.0-windows-x64.zip
    QUrl checksumUrl;    // the same + ".sha256"
    QString notes;       // release text, shown before installing
    QUrl pageUrl;        // the release page on GitHub
};

// Reads the GitHub API answer. nullopt if anything is missing or untrusted
// (draft, pre-release, foreign link, no checksum).
std::optional<Release> parseLatestRelease(const QByteArray &json);

// True when `candidate` ("1.10.0", "v1.10.0") is newer than `current`.
bool isNewer(const QString &candidate, const QString &current);

// Only HTTPS downloads of this project's releases are accepted.
bool isTrustedDownload(const QUrl &url);

// `checksumFile` is the published "<sha256 hex>  <file name>" text.
bool checksumMatches(const QByteArray &package, const QByteArray &checksumFile);

}  // namespace update
