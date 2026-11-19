// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Claude Code bridge: a small MCP server (Model Context Protocol, JSON-RPC
// over HTTP on 127.0.0.1) that lets Claude list the open terminals, read
// their output, run commands and open saved sessions, within the limits the
// user sets per pane.
//
// This file holds everything that does not need the GUI, so it is tested:
// the JSON-RPC tools, the HTTP parsing and checks, the password rules.
//
// Made to spend few tokens: commands return only their own output, compacted
// (no "!" filler lines, no blank runs, no echoed command), optionally
// filtered by a regular expression and capped; a batch of commands is one
// call; a command ends as soon as the device prompt comes back.
//
// Rules that never change:
// - text after "password", "secret", "community", "key"... is masked in what
//   Claude reads, and a command that would type such a value is refused;
// - nothing is sent while the device shows a password prompt (only Ctrl+C,
//   Ctrl+D, Ctrl+Z or Esc, to leave it): the user types passwords, and
//   wait_for_user lets Claude wait for that without polling;
// - nothing is logged.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>

class QObject;
class QJsonValue;

namespace mcp {

// What Claude may do with one pane.
enum class Access { Hidden, ReadOnly, Ask, Full };
QString accessName(Access access);

struct SessionInfo {
    int id = 0;
    QString title;
    QString kind;  // "serial", "ssh", "telnet", "local"
    bool running = false;
    Access access = Access::Ask;
};

struct SavedSession {
    QString name;
    QString folder;
    QString kind;
    QString target;  // host, COM port... (no user, no password)
};

// Lines [first, first + lines.size()) of a terminal; the last one holds the cursor.
struct Text {
    std::int64_t first = 0;
    std::int64_t cursorLine = 0;
    std::uint64_t revision = 0;
    QStringList lines;
};

// The line ends with a password / passphrase / PIN prompt.
bool isPasswordPrompt(const QString &line);
// The line looks like a device or shell prompt: "Switch#", "SW(config-if)#",
// "user@host:~$", "PS C:\>", "[admin@fw] >".
bool isCommandPrompt(const QString &line);
// An error printed by the device: "% Invalid input", "Unknown command"...
bool isErrorLine(const QString &line);
// Replaces secret values ("password 7 0822..", "secret 5 $1$..", "community
// public", "key 7 xx", "Password: xyz") by ********.
QString maskSecrets(const QString &line);
bool containsSecret(const QString &text);
// "ctrl+c", "enter", "tab", "up"... -> bytes; ok = false if unknown.
QByteArray namedKey(const QString &name, bool *ok);

struct Format {
    QString command;  // echoed command line to drop
    QString filter;   // regular expression, lines kept
    int maxChars = 16000;
};
// Masks, compacts, filters and caps terminal lines for Claude.
QString formatOutput(const QStringList &lines, const Format &format);

class Host {
public:
    virtual ~Host() = default;
    virtual QList<SessionInfo> sessions() const = 0;
    // from < 0: the last maxLines lines. Otherwise from that line, at most
    // maxLines (the last ones). False if the session does not exist.
    virtual bool text(int id, std::int64_t from, int maxLines, Text &out) const = 0;
    virtual bool send(int id, const QByteArray &data) = 0;
    // Access::Ask: asks the user; done(true) to send.
    virtual void approve(int id, const QString &what, std::function<void(bool)> done) = 0;
    virtual void activity() {}  // a tool was called (status bar indicator)
    // The user has to do something in this pane (type a password).
    virtual void attention(int /*id*/, const QString & /*message*/) {}
    // Saved sessions Claude may open (empty if the user does not allow it).
    virtual QList<SavedSession> savedSessions() const { return {}; }
    // Opens a saved session in a new tab (after asking the user when needed).
    // done(id, error): id > 0 on success.
    virtual void openSaved(const QString &name, std::function<void(int, const QString &)> done) {
        done(-1, QStringLiteral("Opening sessions is not available."));
    }
};

class Protocol {
public:
    using Reply = std::function<void(const QJsonObject &)>;
    using Done = std::function<void(const QJsonObject &)>;  // tool result
    explicit Protocol(Host *host);
    ~Protocol();

    // One JSON-RPC message. reply is called once, maybe later (commands wait
    // for the output); with an empty object for a notification.
    void handle(const QJsonObject &message, const Reply &reply);

    int settleMs = 700;       // output quiet this long = finished (no prompt seen)
    int promptSettleMs = 150; // ...when the device prompt is back
    int pollMs = 50;

    struct Wait;  // one command waiting for its output (McpProtocol.cpp)

private:
    void callTool(const QString &name, const QJsonObject &args, const Done &done);
    // Sends data, then calls done with the new lines once the output settles.
    void sendAndWait(int session, const QByteArray &data, int waitMs, bool untilPromptGone,
                     const std::function<void(const QString &error, const QStringList &lines, bool finished)> &done);
    void poll(const std::shared_ptr<Wait> &wait);
    void runCommands(int session, QStringList commands, int index, int waitMs, bool stopOnError, Format format,
                     QString result, const Done &done);
    bool atPasswordPrompt(int session) const;

    Host *m_host;
    std::unique_ptr<QObject> m_context;  // timers die with the protocol
    QHash<int, std::int64_t> m_seen;      // per session: first line Claude has not read
};

// --- HTTP -------------------------------------------------------------------

struct HttpRequest {
    QByteArray method;
    QByteArray path;
    QHash<QByteArray, QByteArray> headers;  // names in lower case
    QByteArray body;
};
enum class Parse { Incomplete, Complete, Bad };
// Takes one request from the front of buffer when it is complete.
Parse parseHttp(QByteArray &buffer, HttpRequest &out);
// 0 if the request may go on, else the HTTP status to answer: 404 (other
// path), 403 (browser Origin or Host not local: DNS rebinding), 401 (token).
int checkHttp(const HttpRequest &request, const QByteArray &token, quint16 port);
QByteArray httpResponse(int status, const QByteArray &body, const QByteArray &contentType = "application/json");

QByteArray newToken();  // 64 hex characters

}  // namespace mcp
