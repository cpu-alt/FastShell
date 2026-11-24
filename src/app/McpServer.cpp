// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/McpServer.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace {
// Per connection: unread bytes, and whether a request is being answered
// (commands wait for their output; the next request waits its turn).
constexpr char BufferProperty[] = "mcpBuffer";
constexpr char BusyProperty[] = "mcpBusy";
}  // namespace

McpServer::McpServer(mcp::Host *host, QObject *parent)
    : QObject(parent), m_protocol(host), m_server(new QTcpServer(this)) {
    connect(m_server, &QTcpServer::newConnection, this, &McpServer::onConnection);
}

McpServer::~McpServer() = default;

bool McpServer::start(quint16 port, const QByteArray &token) {
    stop();
    m_token = token;
    if (!m_server->listen(QHostAddress::LocalHost, port)) return false;  // 127.0.0.1 only
    m_port = m_server->serverPort();
    return true;
}

void McpServer::stop() {
    m_server->close();
    for (QTcpSocket *socket : findChildren<QTcpSocket *>()) socket->abort();
    m_port = 0;
}

bool McpServer::isListening() const {
    return m_server->isListening();
}

void McpServer::onConnection() {
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            socket->setProperty(BufferProperty, socket->property(BufferProperty).toByteArray() + socket->readAll());
            process(socket);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void McpServer::process(QTcpSocket *socket) {
    if (socket->property(BusyProperty).toBool()) return;
    QByteArray buffer = socket->property(BufferProperty).toByteArray();
    mcp::HttpRequest request;
    const mcp::Parse parsed = mcp::parseHttp(buffer, request);
    socket->setProperty(BufferProperty, buffer);
    if (parsed == mcp::Parse::Incomplete) return;
    if (parsed == mcp::Parse::Bad) {
        socket->write(mcp::httpResponse(400, {}));
        socket->disconnectFromHost();
        return;
    }
    auto answer = [socket](int status, const QByteArray &body) { socket->write(mcp::httpResponse(status, body)); };
    if (const int status = mcp::checkHttp(request, m_token, m_port, m_remoteSecret); status != 0) {
        answer(status, {});
        socket->disconnectFromHost();
        return;
    }
    if (request.method != "POST") {  // no server-sent events stream
        answer(405, {});
        process(socket);
        return;
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(request.body, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        answer(400, R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})");
        process(socket);
        return;
    }
    socket->setProperty(BusyProperty, true);
    QPointer<QTcpSocket> guard(socket);
    m_protocol.handle(doc.object(), [this, guard](const QJsonObject &reply) {
        if (!guard) return;  // Claude went away while a command was running
        guard->setProperty(BusyProperty, false);
        if (reply.isEmpty()) {
            guard->write(mcp::httpResponse(202, {}));
        } else {
            guard->write(mcp::httpResponse(200, QJsonDocument(reply).toJson(QJsonDocument::Compact)));
        }
        process(guard);
    });
}
