// Copyright 2026 FastShell contributors. Apache-2.0.
//
// "FastShell.exe --mcp-stdio": a small bridge for MCP clients that start a
// program and talk to it over stdin/stdout (the Claude desktop app). Each
// JSON-RPC line read on stdin is posted to the running FastShell's local
// server (127.0.0.1, with the key from the settings) and the answer is
// written back as one line. Nothing secret goes in the client's config.
#pragma once

#include <QByteArray>
#include <QString>

namespace mcp {

// Body of a complete HTTP response in 'buffer' (removed from it), and its
// status; false while incomplete. Sets 'bad' on a malformed response.
bool parseHttpResponse(QByteArray &buffer, int &status, QByteArray &body, bool &bad);

// JSON-RPC error answer for a request line when FastShell cannot be reached
// (empty for a notification, which gets no answer).
QByteArray unreachableReply(const QByteArray &requestLine, const QString &message);

int runStdioBridge(quint16 port, const QByteArray &token);

}  // namespace mcp
