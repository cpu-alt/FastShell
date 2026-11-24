// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/McpStdioBridge.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace mcp {

namespace {
constexpr int ConnectMs = 3000;
constexpr int ReplyMs = 10 * 60 * 1000;  // a command can wait for approval
constexpr qsizetype MaxResponse = 16 * 1024 * 1024;
}  // namespace

bool parseHttpResponse(QByteArray &buffer, int &status, QByteArray &body, bool &bad) {
    bad = false;
    const qsizetype end = buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        bad = buffer.size() > 64 * 1024;
        return false;
    }
    const QList<QByteArray> lines = buffer.left(end).split('\n');
    const QList<QByteArray> first = lines.first().trimmed().split(' ');
    bool ok = false;
    status = first.size() >= 2 ? first.at(1).toInt(&ok) : 0;
    if (!ok || !first.at(0).startsWith("HTTP/1.")) {
        bad = true;
        return false;
    }
    qint64 length = 0;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray &l = lines.at(i);
        const qsizetype colon = l.indexOf(':');
        if (colon > 0 && l.left(colon).trimmed().toLower() == "content-length") {
            length = l.mid(colon + 1).trimmed().toLongLong(&ok);
            if (!ok || length < 0 || length > MaxResponse) {
                bad = true;
                return false;
            }
        }
    }
    if (buffer.size() < end + 4 + length) return false;
    body = buffer.mid(end + 4, length);
    buffer.remove(0, end + 4 + length);
    return true;
}

QByteArray unreachableReply(const QByteArray &requestLine, const QString &message) {
    const QJsonObject request = QJsonDocument::fromJson(requestLine).object();
    if (!request.contains(QStringLiteral("id"))) return {};
    const QJsonObject reply{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), request.value(QStringLiteral("id"))},
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), -32000}, {QStringLiteral("message"), message}}}};
    return QJsonDocument(reply).toJson(QJsonDocument::Compact);
}

int runStdioBridge(quint16 port, const QByteArray &token) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    QTcpSocket socket;
    const QByteArray head = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port)
                            + "\r\nAuthorization: Bearer " + token
                            + "\r\nContent-Type: application/json\r\nAccept: application/json\r\nContent-Length: ";
    const QString offline = QStringLiteral("FastShell is not running, or Claude access is off "
                                           "(in FastShell: Claude > Let Claude use the terminals).");
    std::string line;
    while (std::getline(std::cin, line)) {
        QByteArray request = QByteArray::fromStdString(line).trimmed();
        if (request.isEmpty()) continue;

        QByteArray answer;
        bool sent = false;
        for (int attempt = 0; attempt < 2 && !sent; ++attempt) {
            if (socket.state() != QAbstractSocket::ConnectedState) {
                socket.abort();
                socket.connectToHost(QHostAddress::LocalHost, port);
                if (!socket.waitForConnected(ConnectMs)) break;
            }
            socket.write(head + QByteArray::number(request.size()) + "\r\n\r\n" + request);
            if (!socket.waitForBytesWritten(ConnectMs)) {
                socket.abort();
                continue;  // the server closed an idle connection: once more
            }
            QByteArray buffer;
            int status = 0;
            bool bad = false;
            bool done = false;
            while (!done && !bad && socket.waitForReadyRead(ReplyMs)) {
                buffer += socket.readAll();
                done = parseHttpResponse(buffer, status, answer, bad);
            }
            if (!done) {
                socket.abort();
                if (buffer.isEmpty()) continue;
                break;
            }
            sent = true;
            if (status == 401) {
                answer = unreachableReply(request, QStringLiteral("FastShell refused the key: restart the Claude app."));
            } else if (status != 200 && status != 202) {
                answer = unreachableReply(request, QStringLiteral("FastShell answered HTTP %1.").arg(status));
            }
        }
        if (!sent) answer = unreachableReply(request, offline);
        if (!answer.isEmpty()) {
            std::fwrite(answer.constData(), 1, std::size_t(answer.size()), stdout);
            std::fputc('\n', stdout);
            std::fflush(stdout);
        }
    }
    return 0;
}

}  // namespace mcp
