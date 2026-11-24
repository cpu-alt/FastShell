// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Built-in file server (TFTP / HTTP), GUI side: the window, the status bar
// indicator and the file_server tool for Claude. The server is net/FileServer.
#include "app/MainWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QStatusBar>
#include <QTimer>

#include <QPointer>

#include <algorithm>

#include "app/FileServerDialog.h"
#include "net/FileServer.h"

void MainWindow::createFileServer() {
    m_fileServer = new FileServer(this);
    m_statusFiles = new QLabel(this);
    m_statusFiles->setContentsMargins(8, 0, 8, 0);
    m_statusFiles->setCursor(Qt::PointingHandCursor);
    m_statusFiles->setToolTip(tr("File server: click to open"));
    m_statusFiles->installEventFilter(this);
    m_statusFiles->hide();
    statusBar()->addWidget(m_statusFiles);
    auto update = [this] { updateFileServerIndicator(); };
    connect(m_fileServer, &FileServer::runningChanged, this, update);
    connect(m_fileServer, &FileServer::transferStarted, this, update);
    connect(m_fileServer, &FileServer::transferFinished, this, update);
    connect(m_statusTimer, &QTimer::timeout, this, update);
}

void MainWindow::showFileServer() {
    if (!m_fileDialog) m_fileDialog = new FileServerDialog(m_fileServer, this);
    m_fileDialog->show();
    m_fileDialog->raise();
    m_fileDialog->activateWindow();
}

void MainWindow::updateFileServerIndicator() {
    if (!m_statusFiles) return;
    if (!m_fileServer->isRunning()) {
        m_statusFiles->hide();
        return;
    }
    QString text = tr("  TFTP on  ");
    for (const FileServer::Transfer &t : m_fileServer->transfers()) {
        if (t.state != FileServer::Transfer::State::Running) continue;
        text = t.total > 0 ? tr("  TFTP: %1 %2 %  ").arg(t.file).arg(t.bytes * 100 / t.total)
                           : tr("  TFTP: %1 %2  ").arg(t.file, FileServer::sizeText(t.bytes));
        break;
    }
    m_statusFiles->setText(text);
    m_statusFiles->setStyleSheet(QStringLiteral("background:#2E86DE; color:white; font-weight:bold; border-radius:3px"));
    m_statusFiles->show();
}

bool MainWindow::fileServerClicked(QObject *watched, QEvent *event) {
    if (watched != m_statusFiles || event->type() != QEvent::MouseButtonRelease) return false;
    showFileServer();
    return true;
}

// --- Claude: file_server tool -----------------------------------------------------

void MainWindow::fileServer(const QString &action, const QJsonObject &args, std::function<void(const QString &, bool)> done) {
    FileServer *server = m_fileServer;
    if (action == QLatin1String("status")) {
        done(server->summary(), false);
        return;
    }
    if (action == QLatin1String("stop")) {
        if (server->runningTransfers() > 0) {
            done(QStringLiteral("A transfer is running: wait for it (action=wait) or ask the user to stop it.\n") + server->summary(), true);
            return;
        }
        server->stop();
        done(QStringLiteral("File server stopped."), false);
        return;
    }
    if (action == QLatin1String("start")) {
        if (server->isRunning()) {
            done(QStringLiteral("Already running.\n") + server->summary(), false);
            return;
        }
        const QString folder = args.value(QStringLiteral("folder")).toString().trimmed();
        const FileServer::Config config = FileServerDialog::savedConfig(QDir::fromNativeSeparators(folder));
        if (!QFileInfo(config.root).isDir()) {
            done(QStringLiteral("The folder \"%1\" does not exist on this PC.").arg(config.root), true);
            return;
        }
        // Sharing a folder on the network: always the user's decision.
        auto *box = new QMessageBox(QMessageBox::Question, tr("Claude Code"),
                                    tr("Claude wants to start the file server.\n\nFolder: %1\nInterface: %2, TFTP port %3%4\n\n"
                                       "Devices on the network will be able to read the files of this folder.")
                                        .arg(QDir::toNativeSeparators(config.root),
                                             config.address == QHostAddress(QHostAddress::AnyIPv4) ? tr("all") : config.address.toString())
                                        .arg(config.tftpPort)
                                        .arg(config.http ? tr(", HTTP port %1").arg(config.httpPort) : QString()),
                                    QMessageBox::Yes | QMessageBox::No, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setTextFormat(Qt::PlainText);
        connect(box, &QMessageBox::finished, this, [this, box, config, done] {
            if (box->result() != QMessageBox::Yes) {
                done(QStringLiteral("The user refused."), true);
                return;
            }
            FileServerDialog::saveConfig(config);
            QString error;
            if (!m_fileServer->start(config, &error)) {
                done(error + QStringLiteral(" Another TFTP server may already use the port."), true);
                return;
            }
            done(m_fileServer->summary(), false);
        });
        box->open();
        box->raise();
        QApplication::alert(this);
        return;
    }
    // wait: until the transfers end (or one starts and ends, when none runs yet).
    if (!server->isRunning()) {
        done(QStringLiteral("The file server is not running (action=start)."), true);
        return;
    }
    const double seconds = args.value(QStringLiteral("timeout_seconds")).toDouble(600);
    const qint64 end = QDateTime::currentMSecsSinceEpoch() + qint64(std::clamp(seconds, 1.0, 1800.0) * 1000);
    // Transfers after this id count; a transfer running now counts too.
    int after = 0;
    for (const FileServer::Transfer &t : server->transfers()) {
        after = std::max(after, t.id);
    }
    for (const FileServer::Transfer &t : server->transfers()) {
        if (t.state == FileServer::Transfer::State::Running) after = std::min(after, t.id - 1);
    }
    auto *timer = new QTimer(this);
    timer->setInterval(500);
    QPointer<FileServer> guard = server;
    connect(timer, &QTimer::timeout, this, [timer, guard, after, end, done] {
        QString text;
        bool error = false;
        if (!guard || !guard->isRunning()) {
            text = QStringLiteral("The file server was stopped.");
            error = true;
        } else {
            bool newOne = false;
            for (const FileServer::Transfer &t : guard->transfers()) newOne = newOne || t.id > after;
            const bool timeout = QDateTime::currentMSecsSinceEpoch() >= end;
            if (!timeout && (guard->runningTransfers() > 0 || !newOne)) return;
            text = (timeout ? QStringLiteral("Timeout, still waiting.\n") : QString()) + guard->summary();
        }
        timer->stop();
        timer->deleteLater();
        done(text, error);
    });
    timer->start();
}
