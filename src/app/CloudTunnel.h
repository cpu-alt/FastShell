// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Cloud access for the Claude bridge: runs cloudflared ("quick tunnel", no
// account) to give the local MCP server a public HTTPS address that Claude on
// claude.ai can reach. Only the secret path /mcp/<secret> answers through it
// (mcp::checkHttp). The address changes each time the tunnel starts.
#pragma once

#include <QObject>
#include <QString>

class QProcess;

class CloudTunnel : public QObject {
    Q_OBJECT

public:
    explicit CloudTunnel(QObject *parent = nullptr);
    ~CloudTunnel() override;

    // cloudflared.exe: next to FastShell, in the PATH or where winget puts it.
    // Empty if not installed.
    static QString findProgram();
    // Public base address ("https://xyz.trycloudflare.com") in cloudflared's
    // log, or empty.
    static QString parseUrl(const QString &log);

    bool start(quint16 port);  // false if cloudflared is missing
    void stop();
    bool isRunning() const;
    QString url() const { return m_url; }  // empty until the tunnel is up

Q_SIGNALS:
    void urlChanged(const QString &url);
    void failed(const QString &message);

private:
    QProcess *m_process = nullptr;
    QString m_url;
    QString m_log;
    bool m_stopping = false;
};
