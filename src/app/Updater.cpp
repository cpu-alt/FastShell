// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/Updater.h"

#include <QApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

constexpr qint64 MaxPackageSize = 400LL * 1024 * 1024;  // a package is ~40 MB

// Run by PowerShell once FastShell has closed: unpack, copy over the
// installed files, start the new version. Arguments are passed separately
// (never pasted into a command line).
constexpr char UpdateScript[] = R"ps(param([string]$Zip, [string]$Target, [int]$ProcessId, [string]$Exe)
$ErrorActionPreference = 'Stop'
$log = Join-Path ([IO.Path]::GetDirectoryName($Zip)) 'update.log'
try {
    Wait-Process -Id $ProcessId -Timeout 60 -ErrorAction SilentlyContinue
    $stage = Join-Path ([IO.Path]::GetDirectoryName($Zip)) 'stage'
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    Expand-Archive -LiteralPath $Zip -DestinationPath $stage -Force
    $source = Join-Path $stage 'FastShell'
    if (-not (Test-Path -LiteralPath (Join-Path $source 'FastShell.exe'))) { throw 'The package has no FastShell.exe' }
    for ($i = 0; $i -lt 20; $i++) {
        try { Copy-Item -Path (Join-Path $source '*') -Destination $Target -Recurse -Force; break }
        catch { if ($i -eq 19) { throw }; Start-Sleep -Milliseconds 500 }
    }
    Remove-Item -LiteralPath $stage -Recurse -Force
    Remove-Item -LiteralPath $Zip -Force
    "Updated $(Get-Date -Format s)" | Set-Content -LiteralPath $log
} catch {
    "Update failed $(Get-Date -Format s): $_" | Set-Content -LiteralPath $log
}
Start-Process -FilePath $Exe
)ps";

QString updateFolder() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty()) dir = QDir::tempPath() + QStringLiteral("/FastShell");
    dir += QStringLiteral("/update");
    QDir().mkpath(dir);
    return dir;
}

}  // namespace

Updater::Updater(QWidget *window) : QObject(window), m_window(window), m_network(new QNetworkAccessManager(this)) {
    // Only https links, never a redirect to a less safe scheme.
    m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

bool Updater::automaticChecks() {
    return QSettings().value(QStringLiteral("update/automatic"), true).toBool();
}

void Updater::setAutomaticChecks(bool on) {
    QSettings().setValue(QStringLiteral("update/automatic"), on);
}

void Updater::checkAutomatically() {
    if (!automaticChecks()) return;
    QSettings settings;
    const QDate last = settings.value(QStringLiteral("update/lastCheck")).toDate();
    if (last.isValid() && last == QDate::currentDate()) return;
    settings.setValue(QStringLiteral("update/lastCheck"), QDate::currentDate());
    check(false);
}

void Updater::checkNow() {
    check(true);
}

QNetworkReply *Updater::get(const QUrl &url) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("FastShell/%1").arg(QApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(30000);
    return m_network->get(request);
}

void Updater::check(bool interactive) {
    if (m_busy) return;
    m_busy = true;
    QNetworkReply *reply = get(update::latestReleaseApiUrl());
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive] {
        reply->deleteLater();
        m_busy = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            // 404: no release published yet.
            if (interactive && status == 404) {
                QMessageBox::information(m_window, tr("Updates"), tr("No release has been published yet."));
            } else if (interactive) {
                QMessageBox::warning(m_window, tr("Updates"),
                                     tr("Could not check for updates: %1").arg(reply->errorString()));
            }
            return;
        }
        const auto release = update::parseLatestRelease(reply->read(1024 * 1024));
        if (!release) {
            if (interactive) QMessageBox::warning(m_window, tr("Updates"), tr("The latest release has no Windows package."));
            return;
        }
        if (!update::isNewer(release->version, QApplication::applicationVersion())) {
            if (interactive) {
                QMessageBox::information(m_window, tr("Updates"),
                                         tr("FastShell %1 is up to date.").arg(QApplication::applicationVersion()));
            }
            return;
        }
        offer(*release, interactive);
    });
}

void Updater::offer(const update::Release &release, bool interactive) {
    QSettings settings;
    if (!interactive && settings.value(QStringLiteral("update/skip")).toString() == release.version) return;

    QMessageBox box(m_window);
    box.setWindowTitle(tr("Update available"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("<b>FastShell %1</b> is available (you have %2).").arg(release.version,
                                                                          QApplication::applicationVersion()));
    box.setInformativeText(tr("FastShell will close, update itself and start again. Your sessions are kept."));
    if (!release.notes.isEmpty()) box.setDetailedText(release.notes);
    QPushButton *installButton = box.addButton(tr("Install now"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    QPushButton *skipButton = box.addButton(tr("Skip this version"), QMessageBox::DestructiveRole);
    box.setDefaultButton(installButton);
    box.exec();
    if (box.clickedButton() == installButton) {
        install(release);
    } else if (box.clickedButton() == skipButton) {
        settings.setValue(QStringLiteral("update/skip"), release.version);
    }
}

void Updater::fail(const QString &message) {
    m_busy = false;
    if (m_progress) m_progress->close();
    QMessageBox::warning(m_window, tr("Update"), message);
}

void Updater::install(const update::Release &release) {
    const QString appDir = QApplication::applicationDirPath();
    if (!QFileInfo(appDir).isWritable()) {
        fail(tr("FastShell cannot write to %1. Run the install command again to update.").arg(appDir));
        return;
    }
    if (!update::isTrustedDownload(release.packageUrl) || !update::isTrustedDownload(release.checksumUrl)) {
        fail(tr("The download link is not a FastShell release."));
        return;
    }
    m_busy = true;
    m_progress = new QProgressDialog(tr("Downloading FastShell %1...").arg(release.version), tr("Cancel"), 0, 100, m_window);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->setAttribute(Qt::WA_DeleteOnClose);

    QNetworkReply *checksumReply = get(release.checksumUrl);
    connect(checksumReply, &QNetworkReply::finished, this, [this, checksumReply, release] {
        checksumReply->deleteLater();
        if (checksumReply->error() != QNetworkReply::NoError) {
            fail(tr("Download failed: %1").arg(checksumReply->errorString()));
            return;
        }
        const QByteArray checksum = checksumReply->read(4096);
        QNetworkReply *package = get(release.packageUrl);
        connect(m_progress, &QProgressDialog::canceled, package, &QNetworkReply::abort);
        connect(package, &QNetworkReply::downloadProgress, this, [this, package](qint64 done, qint64 total) {
            if (total > MaxPackageSize || done > MaxPackageSize) package->abort();
            if (m_progress && total > 0) m_progress->setValue(int(done * 100 / total));
        });
        connect(package, &QNetworkReply::finished, this, [this, package, checksum, release] {
            package->deleteLater();
            if (package->error() != QNetworkReply::NoError) {
                fail(package->error() == QNetworkReply::OperationCanceledError ? tr("Update cancelled.")
                                                                                : tr("Download failed: %1").arg(package->errorString()));
                return;
            }
            const QByteArray data = package->readAll();
            if (!update::checksumMatches(data, checksum)) {
                fail(tr("The downloaded file does not match its published SHA-256 checksum. Nothing was installed."));
                return;
            }
            const QString folder = updateFolder();
            const QString zipPath = folder + QStringLiteral("/FastShell-%1.zip").arg(release.version);
            const QString scriptPath = folder + QStringLiteral("/update.ps1");
            QSaveFile zip(zipPath);
            QSaveFile script(scriptPath);
            if (!zip.open(QIODevice::WriteOnly) || zip.write(data) != data.size() || !zip.commit()
                || !script.open(QIODevice::WriteOnly) || script.write(UpdateScript) < 0 || !script.commit()) {
                fail(tr("Cannot save the update in %1.").arg(folder));
                return;
            }
            const QStringList arguments{
                QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-File"),
                QDir::toNativeSeparators(scriptPath),
                QStringLiteral("-Zip"), QDir::toNativeSeparators(zipPath),
                QStringLiteral("-Target"), QDir::toNativeSeparators(QApplication::applicationDirPath()),
                QStringLiteral("-ProcessId"), QString::number(QApplication::applicationPid()),
                QStringLiteral("-Exe"), QDir::toNativeSeparators(QApplication::applicationFilePath())};
            if (!QProcess::startDetached(QStringLiteral("powershell.exe"), arguments)) {
                fail(tr("Cannot start the installer (powershell.exe)."));
                return;
            }
            if (m_progress) m_progress->close();
            QApplication::closeAllWindows();  // saves the layout; the script starts the new version
        });
    });
}
