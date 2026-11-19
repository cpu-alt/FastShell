// Copyright 2026 FastShell contributors. Apache-2.0.
//
// HTTP side of the Claude Code bridge: listens on 127.0.0.1 only, checks
// each request (mcp::checkHttp) and hands the JSON-RPC to mcp::Protocol.
#pragma once

#include <QObject>
#include <QPointer>

#include "app/McpProtocol.h"

class QTcpServer;
class QTcpSocket;

class McpServer : public QObject {
    Q_OBJECT

public:
    McpServer(mcp::Host *host, QObject *parent = nullptr);
    ~McpServer() override;

    bool start(quint16 port, const QByteArray &token);  // false if the port is taken
    void stop();
    bool isListening() const;
    quint16 port() const { return m_port; }
    void setToken(const QByteArray &token) { m_token = token; }

private:
    void onConnection();
    void process(QTcpSocket *socket);

    mcp::Protocol m_protocol;
    QTcpServer *m_server;
    QByteArray m_token;
    quint16 m_port = 0;
};
