// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/CloudTunnel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
constexpr qsizetype MaxLog = 64 * 1024;
}

CloudTunnel::CloudTunnel(QObject *parent) : QObject(parent) {}

CloudTunnel::~CloudTunnel() {
    stop();
}

QString CloudTunnel::findProgram() {
#ifdef _WIN32
    const QString exe = QStringLiteral("cloudflared.exe");
#else
    const QString exe = QStringLiteral("cloudflared");
#endif
    QStringList candidates{QCoreApplication::applicationDirPath() + QLatin1Char('/') + exe};
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("cloudflared"));
    if (!inPath.isEmpty()) candidates << inPath;
#ifdef _WIN32
    // winget (Cloudflare.cloudflared) and the MSI installer.
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (!local.isEmpty()) candidates << local + QStringLiteral("/Microsoft/WinGet/Links/cloudflared.exe");
    for (const char *var : {"ProgramFiles(x86)", "ProgramFiles"}) {
        const QString dir = qEnvironmentVariable(var);
        if (!dir.isEmpty()) candidates << dir + QStringLiteral("/cloudflared/cloudflared.exe");
    }
#endif
    for (const QString &c : candidates) {
        if (QFileInfo(c).isExecutable()) return QDir::toNativeSeparators(c);
    }
    return {};
}

QString CloudTunnel::parseUrl(const QString &log) {
    // cloudflared also logs its own API address (api.trycloudflare.com).
    static const QRegularExpression re(QStringLiteral(R"(https://([a-z0-9-]+)\.trycloudflare\.com\b(?![./]))"));
    for (auto it = re.globalMatch(log); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        if (m.captured(1) != QLatin1String("api")) return m.captured(0);
    }
    return {};
}

bool CloudTunnel::start(quint16 port) {
    stop();
    const QString program = findProgram();
    if (program.isEmpty()) return false;
    m_stopping = false;
    m_log.clear();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    const QString local = QStringLiteral("127.0.0.1:%1").arg(port);
    // --http-host-header: the server only answers requests for its local Host.
    m_process->setArguments({QStringLiteral("tunnel"), QStringLiteral("--no-autoupdate"), QStringLiteral("--url"),
                             QStringLiteral("http://") + local, QStringLiteral("--http-host-header"), local});
    m_process->setProgram(program);
    connect(m_process, &QProcess::readyRead, this, [this] {
        m_log += QString::fromUtf8(m_process->readAll());
        if (m_log.size() > MaxLog) m_log = m_log.right(MaxLog / 2);
        if (m_url.isEmpty()) {
            m_url = parseUrl(m_log);
            if (!m_url.isEmpty()) Q_EMIT urlChanged(m_url);
        }
    });
    connect(m_process, &QProcess::finished, this, [this] {
        const bool expected = m_stopping;
        const QString tail = m_log.right(400).trimmed();
        m_process->deleteLater();
        m_process = nullptr;
        if (!m_url.isEmpty()) {
            m_url.clear();
            Q_EMIT urlChanged(QString());
        }
        if (!expected) Q_EMIT failed(tr("The tunnel stopped.\n\n%1").arg(tail));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) Q_EMIT failed(tr("cloudflared could not start."));
    });
    m_process->start();
    return true;
}

void CloudTunnel::stop() {
    if (!m_process) return;
    m_stopping = true;
    QProcess *p = m_process;
    p->kill();
    p->waitForFinished(3000);
    if (m_process == p) {  // finished not delivered yet
        p->deleteLater();
        m_process = nullptr;
    }
    if (!m_url.isEmpty()) {
        m_url.clear();
        Q_EMIT urlChanged(QString());
    }
}

bool CloudTunnel::isRunning() const {
    return m_process != nullptr;
}
