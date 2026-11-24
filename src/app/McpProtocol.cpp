// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/McpProtocol.h"

#include <algorithm>
#include <iterator>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>

#ifdef FASTSHELL_VERSION
#define FASTSHELL_MCP_VERSION_TEXT FASTSHELL_VERSION
#else
#define FASTSHELL_MCP_VERSION_TEXT "dev"
#endif

namespace mcp {

namespace {

constexpr int MaxReadLines = 2000;
constexpr int MaxLineLength = 1024;
constexpr int MaxCommands = 50;

const QRegularExpression &passwordPattern() {
    // "Password:", "admin@10.0.0.1's password:", "Enter passphrase for key...:",
    // "Mot de passe :", "PIN:", "Enable secret?"...
    static const QRegularExpression re(
        QStringLiteral(R"((pass(word|wd|phrase|code)?|mot de passe|secret|\bpin\b|passcode|verification code|otp)[^:\n]{0,40}[:?]\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &secretPattern() {
    // keyword [type digit] value...: the rest of the line is masked (the value
    // may follow a type word: "secret sha256 xxx", "key 7 xxx").
    static const QRegularExpression re(
        QStringLiteral(R"(\b(password|passwd|passphrase|secret|community|key-string|pre-shared-key|psk|mot de passe|key(?=\s+(?!generate\b|chain\b|zeroize\b)))(\s*[:=]\s*|\s+)((?:\d{1,2}\s+)?)(\S.*))"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QByteArray LeaveKeys[] = {"\x03", "\x04", "\x1a", "\x1b"};  // Ctrl+C/D/Z, Esc
bool isLeaveKey(const QByteArray &data) {
    return std::find(std::begin(LeaveKeys), std::end(LeaveKeys), data) != std::end(LeaveKeys);
}

QJsonObject toolText(const QString &text, bool error = false) {
    QJsonObject result{{QStringLiteral("content"),
                        QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}}}}};
    if (error) result.insert(QStringLiteral("isError"), true);
    return result;
}
QJsonObject toolError(const QString &text) {
    return toolText(text, true);
}

QJsonObject response(const QJsonValue &id, const QJsonObject &result) {
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}};
}

QJsonObject errorResponse(const QJsonValue &id, int code, const QString &message) {
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

QJsonObject schema(const QJsonObject &properties, const QStringList &required) {
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), properties},
            {QStringLiteral("required"), QJsonArray::fromStringList(required)}};
}

QJsonObject prop(const char *type, const QString &description) {
    return {{QStringLiteral("type"), QLatin1String(type)}, {QStringLiteral("description"), description}};
}

QJsonObject arrayOfStrings(const QString &description) {
    QJsonObject p = prop("array", description);
    p.insert(QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
    return p;
}

QJsonArray toolList() {
    const QJsonObject session = prop("integer", QStringLiteral("Session id from list_sessions."));
    const QJsonObject wait = prop("number", QStringLiteral("Max seconds per command (default 10, max 120). A command ends "
                                                           "as soon as the prompt is back."));
    const QJsonObject filter = prop("string", QStringLiteral("Optional regex (case-insensitive): keep only matching lines, "
                                                             "like '| include'. Saves tokens on long outputs."));
    auto tool = [](const char *name, const QString &description, const QJsonObject &input) {
        return QJsonObject{{QStringLiteral("name"), QLatin1String(name)},
                           {QStringLiteral("description"), description},
                           {QStringLiteral("inputSchema"), input}};
    };
    return {
        tool("list_sessions",
             QStringLiteral("Open FastShell terminals: id, type, title, Claude access (read_only / ask = the user "
                            "confirms each send / full), and the current prompt line."),
             schema({}, {})),
        tool("read_output",
             QStringLiteral("Lines of a terminal up to the cursor. new_only=true returns only what Claude has not "
                            "seen yet (cheapest way to follow a console)."),
             schema({{QStringLiteral("session"), session},
                     {QStringLiteral("lines"), prop("integer", QStringLiteral("Last N lines (default 40, max 2000)."))},
                     {QStringLiteral("new_only"), prop("boolean", QStringLiteral("Only lines not read before."))},
                     {QStringLiteral("filter"), filter}},
                    {QStringLiteral("session")})),
        tool("run_commands",
             QStringLiteral("Runs commands one after the other (each + Enter) and returns each output, compacted. "
                            "Use it for show commands and whole configuration blocks: one call, one confirmation. "
                            "Stops at the first device error unless stop_on_error=false. --More-- pages are "
                            "skipped automatically. Never put passwords or secrets in commands."),
             schema({{QStringLiteral("session"), session},
                     {QStringLiteral("commands"), arrayOfStrings(QStringLiteral("Command lines, without Enter."))},
                     {QStringLiteral("stop_on_error"), prop("boolean", QStringLiteral("Default true."))},
                     {QStringLiteral("wait_seconds"), wait},
                     {QStringLiteral("filter"), filter}},
                    {QStringLiteral("session"), QStringLiteral("commands")})),
        tool("send_line",
             QStringLiteral("Runs one command (+ Enter) and returns its output. Same as run_commands with one command."),
             schema({{QStringLiteral("session"), session},
                     {QStringLiteral("text"), prop("string", QStringLiteral("The command, one line."))},
                     {QStringLiteral("wait_seconds"), wait},
                     {QStringLiteral("filter"), filter}},
                    {QStringLiteral("session"), QStringLiteral("text")})),
        tool("help",
             QStringLiteral("Context help of the device: types the text followed by '?', returns the help, then "
                            "clears the line (Ctrl+U). Example: text='spanning-tree ' lists the keywords after "
                            "spanning-tree. Use it to learn an unknown CLI syntax before configuring."),
             schema({{QStringLiteral("session"), session},
                     {QStringLiteral("text"), prop("string", QStringLiteral("Partial command; '' for the commands of the current mode."))}},
                    {QStringLiteral("session"), QStringLiteral("text")})),
        tool("send_keys",
             QStringLiteral("Keys without Enter. Named keys: enter, tab, space, esc, backspace, delete, up, down, left, "
                            "right, home, end, pageup, pagedown, ctrl+a ... ctrl+z. Other strings are typed as text."),
             schema({{QStringLiteral("session"), session},
                     {QStringLiteral("keys"), arrayOfStrings(QStringLiteral("Keys in order."))},
                     {QStringLiteral("wait_seconds"), prop("number", QStringLiteral("Default 1."))}},
                    {QStringLiteral("session"), QStringLiteral("keys")})),
        tool("wait_for_user",
             QStringLiteral("When the device asks for a password (or anything only the user may type): shows the "
                            "message in FastShell and waits until the user has answered, then returns the new "
                            "output. Use it instead of polling read_output."),
             schema({{QStringLiteral("session"), session},
                     {QStringLiteral("message"), prop("string", QStringLiteral("What the user should do, short."))},
                     {QStringLiteral("timeout_seconds"), prop("number", QStringLiteral("Default 180, max 900."))}},
                    {QStringLiteral("session")})),
        tool("list_saved_sessions",
             QStringLiteral("Saved FastShell sessions Claude may open (name, folder, type, target)."),
             schema({{QStringLiteral("filter"), prop("string", QStringLiteral("Optional text to search in names, "
                                                                              "folders and targets."))}},
                    {})),
        tool("open_session",
             QStringLiteral("Opens a saved session in a new FastShell tab (the user may be asked first) and returns "
                            "its id. FastShell logs in with the saved credentials; Claude never sees them."),
             schema({{QStringLiteral("name"), prop("string", QStringLiteral("Exact saved session name."))}},
                    {QStringLiteral("name")})),
        tool("file_server",
             QStringLiteral("FastShell's built-in TFTP (and optional HTTP) server, to send a file such as a firmware "
                            "from this PC to a device: start it, then run the copy command on the device (e.g. "
                            "'copy tftp://<PC address>/<file> flash:<name>'), then action=wait to follow the transfer. "
                            "action=status: folder, PC addresses, files, last transfers. start: the user confirms; "
                            "folder is optional (default: the one set in FastShell). stop: refused while a transfer "
                            "runs. wait: returns when the running transfers end."),
             schema({{QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                            {QStringLiteral("enum"), QJsonArray{QStringLiteral("status"), QStringLiteral("start"),
                                                                                                QStringLiteral("stop"), QStringLiteral("wait")}}}},
                     {QStringLiteral("folder"), prop("string", QStringLiteral("start: folder of this PC to share (read only)."))},
                     {QStringLiteral("timeout_seconds"), prop("number", QStringLiteral("wait: default 600, max 1800."))}},
                    {QStringLiteral("action")})),
    };
}

bool hasControl(const QString &text) {
    for (QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F || c.category() == QChar::Other_Format) return true;
    }
    return false;
}

const QString &instructions() {
    static const QString text = QStringLiteral(
        "FastShell is the user's terminal (serial consoles, SSH, Telnet). Workflow: list_sessions, then read_output "
        "(new_only=true to follow a console cheaply). Run show and configuration commands with run_commands, many "
        "in one call (it stops at the first error). When the CLI syntax is unknown (FS KT-NOS, FSOS, Cisco...), use "
        "help with a partial command instead of guessing. Use filter to keep long outputs short. Verify a change "
        "by reading the configuration back, and save it only when the user wants. Passwords are never shown and "
        "never typed by Claude: write the exact commands with <placeholders> for the user, and when the device "
        "asks for a password call wait_for_user. Ask the user before disruptive changes (reload, erase, shutting "
        "the uplink or management interface). To send a file (firmware) from the PC to a device, use file_server.");
    return text;
}

QString kindTarget(const SavedSession &s) {
    return s.target.isEmpty() ? s.kind : s.kind + QLatin1Char(' ') + s.target;
}

}  // namespace

QString accessName(Access access) {
    switch (access) {
    case Access::Hidden: return QStringLiteral("hidden");
    case Access::ReadOnly: return QStringLiteral("read_only");
    case Access::Ask: return QStringLiteral("ask");
    case Access::Full: return QStringLiteral("full");
    }
    return {};
}

bool isPasswordPrompt(const QString &line) {
    return passwordPattern().match(line).hasMatch();
}

bool isCommandPrompt(const QString &line) {
    static const QRegularExpression re(QStringLiteral(R"(^\S.{0,118}[#>$\]]\s?$)"));
    return re.match(line).hasMatch() && !isPasswordPrompt(line);
}

bool isErrorLine(const QString &line) {
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*(%\s*(invalid|incomplete|ambiguous|unknown|unrecognized|error|bad)|unknown command|)"
                       R"(invalid (input|command|parameter|argument)|incomplete command|ambiguous command|error[: ]|)"
                       R"(syntax error|command (rejected|not found|failed)|unrecognized command)|is not recognized as|)"
                       R"(n'est pas reconnu|command not found$|commandnotfoundexception)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(line).hasMatch();
}

QString maskSecrets(const QString &line) {
    QString result = line;
    QList<QRegularExpressionMatch> matches;
    for (auto it = secretPattern().globalMatch(line); it.hasNext();) matches << it.next();
    for (auto m = matches.crbegin(); m != matches.crend(); ++m) {  // from the end: offsets stay valid
        result.replace(m->capturedStart(4), m->capturedLength(4), QStringLiteral("********"));
    }
    return result;
}

bool containsSecret(const QString &text) {
    return maskSecrets(text) != text;
}

QByteArray namedKey(const QString &name, bool *ok) {
    *ok = true;
    const QString n = name.trimmed().toLower();
    static const QHash<QString, QByteArray> keys{
        {QStringLiteral("enter"), "\r"},        {QStringLiteral("return"), "\r"},     {QStringLiteral("tab"), "\t"},
        {QStringLiteral("space"), " "},         {QStringLiteral("esc"), "\x1b"},      {QStringLiteral("escape"), "\x1b"},
        {QStringLiteral("backspace"), "\x7f"},  {QStringLiteral("delete"), "\x1b[3~"}, {QStringLiteral("up"), "\x1b[A"},
        {QStringLiteral("down"), "\x1b[B"},     {QStringLiteral("right"), "\x1b[C"},  {QStringLiteral("left"), "\x1b[D"},
        {QStringLiteral("home"), "\x1b[H"},     {QStringLiteral("end"), "\x1b[F"},    {QStringLiteral("pageup"), "\x1b[5~"},
        {QStringLiteral("pagedown"), "\x1b[6~"}};
    if (auto it = keys.constFind(n); it != keys.cend()) return *it;
    static const QRegularExpression ctrl(QStringLiteral(R"(^(ctrl|control|c)[+-]([a-z\[\]\\^_])$)"));
    if (const auto m = ctrl.match(n); m.hasMatch()) {
        const char c = m.captured(2).at(0).toLatin1();
        return QByteArray(1, char(c >= 'a' && c <= 'z' ? c - 'a' + 1 : (c - 0x40) & 0x1F));
    }
    *ok = false;
    return {};
}

QString formatOutput(const QStringList &input, const Format &format) {
    QStringList lines;
    lines.reserve(input.size());
    const QString command = format.command.trimmed();
    bool first = true;
    for (const QString &raw : input) {
        QString line = maskSecrets(raw);
        while (!line.isEmpty() && line.back().isSpace()) line.chop(1);
        if (first && !command.isEmpty() && line.endsWith(command)) {  // the echoed command
            first = false;
            continue;
        }
        if (!line.isEmpty()) first = false;
        const QString t = line.trimmed();
        if (t == QLatin1String("!") || t == QLatin1String("#")) continue;  // config filler
        if (t.isEmpty() && (lines.isEmpty() || lines.last().isEmpty())) continue;
        lines << (t.isEmpty() ? QString() : line);
    }
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();

    if (!format.filter.isEmpty()) {
        const QRegularExpression re(format.filter, QRegularExpression::CaseInsensitiveOption);
        QStringList kept;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            if (re.match(lines.at(i)).hasMatch() || i == lines.size() - 1) kept << lines.at(i);  // + the prompt
        }
        lines = kept;
    }

    qsizetype total = 0;
    for (const QString &l : lines) total += l.size() + 1;
    if (total <= format.maxChars) return lines.join(QLatin1Char('\n'));
    // Too long: the start and the end, with a note in the middle.
    QStringList head, tail;
    qsizetype size = 0;
    qsizetype i = 0;
    for (; i < lines.size() && size + lines.at(i).size() < format.maxChars * 6 / 10; ++i) {
        size += lines.at(i).size() + 1;
        head << lines.at(i);
    }
    qsizetype j = lines.size() - 1;
    for (; j > i && size + lines.at(j).size() < format.maxChars; --j) {
        size += lines.at(j).size() + 1;
        tail.prepend(lines.at(j));
    }
    head << QStringLiteral("[... %1 lines cut: use filter, or read_output with more lines ...]").arg(j - i + 1);
    return (head + tail).join(QLatin1Char('\n'));
}

// --- protocol -----------------------------------------------------------------

struct Protocol::Wait {
    int session = 0;
    std::int64_t start = 0;
    std::uint64_t revision = 0;
    std::uint64_t lastRevision = 0;
    qint64 started = 0;
    qint64 lastChange = 0;
    int waitMs = 0;
    bool untilPromptGone = false;
    std::function<void(const QString &, const QStringList &, bool)> done;
};

Protocol::Protocol(Host *host) : m_host(host), m_context(std::make_unique<QObject>()) {}
Protocol::~Protocol() = default;

void Protocol::handle(const QJsonObject &message, const Reply &reply) {
    const QString method = message.value(QStringLiteral("method")).toString();
    if (!message.contains(QStringLiteral("id"))) {  // notification
        reply({});
        return;
    }
    const QJsonValue id = message.value(QStringLiteral("id"));
    const QJsonObject params = message.value(QStringLiteral("params")).toObject();
    if (method == QLatin1String("initialize")) {
        const QString asked = params.value(QStringLiteral("protocolVersion")).toString();
        static const QStringList known{QStringLiteral("2025-11-25"), QStringLiteral("2025-06-18"), QStringLiteral("2025-03-26"),
                                       QStringLiteral("2024-11-05")};
        reply(response(id, {{QStringLiteral("protocolVersion"), known.contains(asked) ? asked : known.at(1)},
                            {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}},
                            {QStringLiteral("serverInfo"),
                             QJsonObject{{QStringLiteral("name"), QStringLiteral("fastshell")},
                                         {QStringLiteral("version"), QStringLiteral(FASTSHELL_MCP_VERSION_TEXT)}}},
                            {QStringLiteral("instructions"), instructions()}}));
    } else if (method == QLatin1String("ping")) {
        reply(response(id, {}));
    } else if (method == QLatin1String("tools/list")) {
        reply(response(id, {{QStringLiteral("tools"), toolList()}}));
    } else if (method == QLatin1String("tools/call")) {
        m_host->activity();
        callTool(params.value(QStringLiteral("name")).toString(), params.value(QStringLiteral("arguments")).toObject(),
                 [reply, id](const QJsonObject &result) { reply(response(id, result)); });
    } else {
        reply(errorResponse(id, -32601, QStringLiteral("Method not found")));
    }
}

bool Protocol::atPasswordPrompt(int session) const {
    Text t;
    return m_host->text(session, -1, 1, t) && !t.lines.isEmpty() && isPasswordPrompt(t.lines.last());
}

void Protocol::callTool(const QString &name, const QJsonObject &args, const Done &done) {
    // --- tools without a session ---
    if (name == QLatin1String("list_sessions")) {
        QStringList out;
        for (const SessionInfo &s : m_host->sessions()) {
            if (s.access == Access::Hidden) continue;
            Text t;
            QString prompt;
            if (m_host->text(s.id, -1, 1, t) && !t.lines.isEmpty()) prompt = maskSecrets(t.lines.last()).trimmed();
            QString line = QStringLiteral("%1 %2 \"%3\" access=%4").arg(s.id).arg(s.kind, s.title, accessName(s.access));
            if (!s.running) line += QStringLiteral(" DISCONNECTED");
            if (!prompt.isEmpty()) line += QStringLiteral(" prompt=\"%1\"").arg(prompt.left(80));
            if (isPasswordPrompt(prompt)) line += QStringLiteral(" WAITING_FOR_PASSWORD");
            out << line;
        }
        done(toolText(out.isEmpty() ? QStringLiteral("No terminal is shared with Claude in FastShell.")
                                    : QStringLiteral("id type \"title\" access prompt\n") + out.join(QLatin1Char('\n'))));
        return;
    }
    if (name == QLatin1String("list_saved_sessions")) {
        const QString filter = args.value(QStringLiteral("filter")).toString().trimmed();
        QStringList out;
        for (const SavedSession &s : m_host->savedSessions()) {
            const QString line = QStringLiteral("\"%1\" %2%3").arg(s.name, kindTarget(s),
                                                                   s.folder.isEmpty() ? QString() : QStringLiteral(" [") + s.folder + QLatin1Char(']'));
            if (filter.isEmpty() || line.contains(filter, Qt::CaseInsensitive)) out << line;
        }
        done(toolText(out.isEmpty() ? QStringLiteral("No saved session found (or opening sessions is not allowed in FastShell).")
                                    : out.join(QLatin1Char('\n'))));
        return;
    }
    if (name == QLatin1String("open_session")) {
        const QString target = args.value(QStringLiteral("name")).toString();
        m_host->openSaved(target, [done](int id, const QString &error) {
            if (id <= 0) done(toolError(error));
            else done(toolText(QStringLiteral("Opened: session %1. Use read_output with new_only=true to follow the login.").arg(id)));
        });
        return;
    }

    if (name == QLatin1String("file_server")) {
        const QString action = args.value(QStringLiteral("action")).toString();
        static const QStringList actions{QStringLiteral("status"), QStringLiteral("start"), QStringLiteral("stop"), QStringLiteral("wait")};
        if (!actions.contains(action)) {
            done(toolError(QStringLiteral("action must be status, start, stop or wait.")));
            return;
        }
        m_host->fileServer(action, args, [done](const QString &text, bool error) { done(error ? toolError(text) : toolText(text)); });
        return;
    }

    // --- tools on a session ---
    const int session = args.value(QStringLiteral("session")).toInt(-1);
    const QList<SessionInfo> all = m_host->sessions();
    const auto info = std::find_if(all.cbegin(), all.cend(), [session](const SessionInfo &s) { return s.id == session; });
    if (info == all.cend() || info->access == Access::Hidden) {
        done(toolError(QStringLiteral("Unknown session %1: call list_sessions.").arg(session)));
        return;
    }
    Format format;
    format.filter = args.value(QStringLiteral("filter")).toString();
    if (!format.filter.isEmpty() && !QRegularExpression(format.filter).isValid()) {
        done(toolError(QStringLiteral("Invalid filter regex.")));
        return;
    }

    if (name == QLatin1String("read_output")) {
        const int count = std::clamp(args.value(QStringLiteral("lines")).toInt(40), 1, MaxReadLines);
        std::int64_t from = -1;
        if (args.value(QStringLiteral("new_only")).toBool() && m_seen.contains(session)) from = m_seen.value(session);
        Text t;
        if (!m_host->text(session, from, from < 0 ? count : MaxReadLines, t)) {
            done(toolError(QStringLiteral("This session is closed.")));
            return;
        }
        if (from >= 0 && from > t.cursorLine) m_host->text(session, -1, count, t);  // screen cleared
        m_seen.insert(session, t.cursorLine);
        QString out = formatOutput(t.lines, format);
        if (from >= 0 && t.lines.size() <= 1) out = QStringLiteral("[Nothing new.] ") + out;
        if (!t.lines.isEmpty() && isPasswordPrompt(t.lines.last())) {
            out += QStringLiteral("\n[Password prompt: call wait_for_user.]");
        }
        done(toolText(out));
        return;
    }

    const bool writes = name == QLatin1String("send_line") || name == QLatin1String("run_commands") ||
                        name == QLatin1String("send_keys") || name == QLatin1String("help") ||
                        name == QLatin1String("wait_for_user");
    if (!writes) {
        done(toolError(QStringLiteral("Unknown tool %1.").arg(name)));
        return;
    }
    if (!info->running) {
        done(toolError(QStringLiteral("This session is disconnected.")));
        return;
    }

    if (name == QLatin1String("wait_for_user")) {
        const int waitMs = int(std::clamp(args.value(QStringLiteral("timeout_seconds")).toDouble(180), 1.0, 900.0) * 1000);
        QString message = args.value(QStringLiteral("message")).toString().left(200);
        if (message.isEmpty()) message = QStringLiteral("Claude is waiting for you to type in this console.");
        m_host->attention(session, message);
        sendAndWait(session, {}, waitMs, true, [done, format](const QString &error, const QStringList &lines, bool finished) {
            if (!error.isEmpty()) return done(toolError(error));
            QString out = formatOutput(lines, format);
            if (!finished) out = QStringLiteral("[The user has not answered yet.]\n") + out;
            done(toolText(out));
        });
        return;
    }

    if (info->access == Access::ReadOnly) {
        done(toolError(QStringLiteral("This session is read only for Claude (the user can change it: right click on "
                                      "the tab > Claude access).")));
        return;
    }
    const bool password = atPasswordPrompt(session);
    const double defaultWait = name == QLatin1String("send_keys") ? 1 : 10;
    const int waitMs = int(std::clamp(args.value(QStringLiteral("wait_seconds")).toDouble(defaultWait), 0.0, 120.0) * 1000);
    auto checkLine = [&](const QString &text) -> QString {
        if (text.size() > MaxLineLength) return QStringLiteral("Command too long.");
        if (hasControl(text)) return QStringLiteral("One line per command, without control characters.");
        if (containsSecret(text)) {
            return QStringLiteral("\"%1\" contains a password or a secret: give the user the exact command with a "
                                  "<placeholder> to type.").arg(maskSecrets(text).left(80));
        }
        return {};
    };
    const QString passwordError = QStringLiteral("The device is asking for a password: call wait_for_user.");

    if (name == QLatin1String("help")) {
        const QString text = args.value(QStringLiteral("text")).toString();
        if (const QString e = checkLine(text); !e.isEmpty()) return done(toolError(e));
        if (password) return done(toolError(passwordError));
        // Harmless (no Enter): not confirmed in "ask" mode.
        sendAndWait(session, text.toUtf8() + '?', std::max(waitMs, 3000), false,
                    [this, session, done](const QString &error, const QStringList &lines, bool) {
                        if (!error.isEmpty()) return done(toolError(error));
                        QStringList help = lines;
                        if (help.size() > 1) help.removeLast();  // the prompt with the partial command again
                        if (!help.isEmpty()) help.removeFirst();  // the typed line
                        m_host->send(session, QByteArrayLiteral("\x15"));  // Ctrl+U: clear the line
                        Text t;
                        if (m_host->text(session, -1, 1, t)) m_seen.insert(session, t.cursorLine + 1);
                        done(toolText(formatOutput(help, {})));
                    });
        return;
    }

    if (name == QLatin1String("send_keys")) {
        const QJsonArray keys = args.value(QStringLiteral("keys")).toArray();
        if (keys.isEmpty() || keys.size() > 64) return done(toolError(QStringLiteral("Give between 1 and 64 keys.")));
        QByteArray data;
        QStringList parts;
        for (const QJsonValue &v : keys) {
            const QString key = v.toString();
            bool named = false;
            const QByteArray bytes = namedKey(key, &named);
            if (named) {
                if (password && !isLeaveKey(bytes)) {
                    return done(toolError(QStringLiteral("Password prompt: only ctrl+c, ctrl+d, ctrl+z or esc. Call wait_for_user.")));
                }
                data += bytes;
                parts << QLatin1Char('<') + key.trimmed().toLower() + QLatin1Char('>');
            } else {
                if (key.isEmpty()) return done(toolError(QStringLiteral("Empty key.")));
                if (const QString e = checkLine(key); !e.isEmpty()) return done(toolError(e));
                if (password) return done(toolError(passwordError));
                data += key.toUtf8();
                parts << key;
            }
        }
        auto go = [this, session, data, waitMs, done](bool ok) {
            if (!ok) return done(toolError(QStringLiteral("The user refused.")));
            sendAndWait(session, data, waitMs, false, [done](const QString &error, const QStringList &lines, bool) {
                if (!error.isEmpty()) return done(toolError(error));
                done(toolText(formatOutput(lines, {})));
            });
        };
        if (info->access == Access::Ask) m_host->approve(session, parts.join(QLatin1Char(' ')), go);
        else go(true);
        return;
    }

    // send_line / run_commands
    QStringList commands;
    if (name == QLatin1String("send_line")) {
        commands << args.value(QStringLiteral("text")).toString();
    } else {
        for (const QJsonValue &v : args.value(QStringLiteral("commands")).toArray()) commands << v.toString();
    }
    if (commands.isEmpty() || commands.size() > MaxCommands) {
        return done(toolError(QStringLiteral("Give between 1 and %1 commands.").arg(MaxCommands)));
    }
    for (const QString &c : commands) {
        if (const QString e = checkLine(c); !e.isEmpty()) return done(toolError(e));
    }
    if (password) return done(toolError(passwordError));
    const bool stopOnError = args.value(QStringLiteral("stop_on_error")).toBool(true);
    auto go = [this, session, commands, waitMs, stopOnError, format, done](bool ok) {
        if (!ok) return done(toolError(QStringLiteral("The user refused.")));
        runCommands(session, commands, 0, waitMs, stopOnError, format, QString(), done);
    };
    if (info->access == Access::Ask) m_host->approve(session, commands.join(QLatin1Char('\n')), go);
    else go(true);
}

void Protocol::runCommands(int session, QStringList commands, int index, int waitMs, bool stopOnError, Format format,
                           QString result, const Done &done) {
    if (index >= commands.size()) return done(toolText(result));
    if (atPasswordPrompt(session)) {
        result += QStringLiteral("\n[Stopped: the device asks for a password. Call wait_for_user, then run the rest.]");
        return done(toolText(result));
    }
    const QString command = commands.at(index);
    sendAndWait(session, command.toUtf8() + '\r', waitMs, false,
                [=, this](const QString &error, const QStringList &lines, bool finished) mutable {
                    if (!error.isEmpty()) return done(toolError(result + error));
                    Format f = format;
                    f.command = command;
                    f.maxChars = std::max(2000, format.maxChars - int(result.size()));
                    QStringList own = lines;
                    if (index + 1 < commands.size() && own.size() > 1 && isCommandPrompt(own.last())) {
                        own.removeLast();  // the prompt shows once, at the end of the batch
                    }
                    const QString out = formatOutput(own, f);
                    if (commands.size() > 1) result += QStringLiteral("> ") + command + QLatin1Char('\n');
                    result += out;
                    if (!finished) result += QStringLiteral("\n[Still running after the wait: read_output new_only=true later.]");
                    bool failed = false;
                    for (const QString &l : lines) failed = failed || isErrorLine(l);
                    if (commands.size() > 1) result += QLatin1Char('\n');
                    if (failed && stopOnError && index + 1 < commands.size()) {
                        result += QStringLiteral("[Stopped at this error: %1 command(s) not sent.]").arg(commands.size() - index - 1);
                        return done(toolText(result));
                    }
                    if (!finished) return done(toolText(result));
                    runCommands(session, commands, index + 1, waitMs, stopOnError, format, result, done);
                });
}

void Protocol::sendAndWait(int session, const QByteArray &data, int waitMs, bool untilPromptGone,
                           const std::function<void(const QString &, const QStringList &, bool)> &done) {
    Text before;
    if (!m_host->text(session, -1, 1, before)) return done(QStringLiteral("This session is closed."), {}, false);
    // The device may have moved to a password prompt while the user was asked.
    if (!data.isEmpty() && !before.lines.isEmpty() && isPasswordPrompt(before.lines.last()) && !isLeaveKey(data)) {
        return done(QStringLiteral("The device is asking for a password: call wait_for_user."), {}, false);
    }
    if (!data.isEmpty() && !m_host->send(session, data)) return done(QStringLiteral("This session is closed."), {}, false);
    auto wait = std::make_shared<Wait>();
    wait->session = session;
    wait->start = untilPromptGone ? std::max<std::int64_t>(before.cursorLine, m_seen.value(session, before.cursorLine))
                                  : before.cursorLine;
    wait->revision = wait->lastRevision = before.revision;
    wait->started = wait->lastChange = QDateTime::currentMSecsSinceEpoch();
    wait->waitMs = waitMs;
    wait->untilPromptGone = untilPromptGone;
    wait->done = done;
    poll(wait);
}

void Protocol::poll(const std::shared_ptr<Wait> &w) {
    Text t;
    if (!m_host->text(w->session, -1, 1, t)) return w->done(QStringLiteral("This session is closed."), {}, false);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (t.revision != w->lastRevision) {
        w->lastRevision = t.revision;
        w->lastChange = now;
    }
    const QString cursor = t.lines.isEmpty() ? QString() : t.lines.last();
    const bool changed = w->lastRevision != w->revision;
    const qint64 quiet = now - w->lastChange;
    const bool prompt = isCommandPrompt(cursor) && (t.cursorLine > w->start || w->untilPromptGone);
    bool finished;
    if (w->untilPromptGone) {
        finished = (changed || !isPasswordPrompt(cursor)) && !isPasswordPrompt(cursor) &&
                   quiet >= (prompt ? promptSettleMs : settleMs);
    } else {
        finished = changed && (quiet >= settleMs || ((prompt || isPasswordPrompt(cursor)) && quiet >= promptSettleMs));
    }
    if (finished || now - w->started >= w->waitMs) {
        Text out;
        m_host->text(w->session, w->start, MaxReadLines, out);
        m_seen.insert(w->session, out.cursorLine);
        return w->done(QString(), out.lines, finished);
    }
    QTimer::singleShot(pollMs, m_context.get(), [this, w] { poll(w); });
}

// --- HTTP -------------------------------------------------------------------

Parse parseHttp(QByteArray &buffer, HttpRequest &out) {
    constexpr qsizetype MaxHeader = 16 * 1024;
    constexpr qsizetype MaxBody = 1024 * 1024;
    const qsizetype end = buffer.indexOf("\r\n\r\n");
    if (end < 0) return buffer.size() > MaxHeader ? Parse::Bad : Parse::Incomplete;
    if (end > MaxHeader) return Parse::Bad;
    const QList<QByteArray> lines = buffer.left(end).split('\n');
    const QList<QByteArray> first = lines.first().trimmed().split(' ');
    if (first.size() != 3 || !first.at(2).startsWith("HTTP/1.")) return Parse::Bad;
    HttpRequest request;
    request.method = first.at(0);
    request.path = first.at(1);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray &l = lines.at(i);
        const qsizetype colon = l.indexOf(':');
        if (colon <= 0) return Parse::Bad;
        request.headers.insert(l.left(colon).trimmed().toLower(), l.mid(colon + 1).trimmed());
    }
    if (request.headers.contains("transfer-encoding")) return Parse::Bad;  // chunked: not needed
    bool ok = true;
    const QByteArray lengthText = request.headers.value("content-length", "0");
    const qint64 length = lengthText.toLongLong(&ok);
    if (!ok || length < 0 || length > MaxBody) return Parse::Bad;
    if (buffer.size() < end + 4 + length) return Parse::Incomplete;
    request.body = buffer.mid(end + 4, length);
    buffer.remove(0, end + 4 + length);
    out = std::move(request);
    return Parse::Complete;
}

int checkHttp(const HttpRequest &request, const QByteArray &token, quint16 port) {
    QByteArray path = request.path;
    if (const qsizetype q = path.indexOf('?'); q >= 0) path.truncate(q);
    if (path != "/mcp") return 404;
    // A web page cannot reach the server: browsers always send Origin, and a
    // rebound DNS name shows in Host.
    if (request.headers.contains("origin")) return 403;
    const QByteArray host = request.headers.value("host");
    const QByteArray p = QByteArray::number(port);
    if (host != "127.0.0.1:" + p && host != "localhost:" + p) return 403;
    const QByteArray expected = "Bearer " + token;
    const QByteArray given = request.headers.value("authorization");
    if (token.isEmpty() || given.size() != expected.size()) return 401;
    unsigned char diff = 0;  // constant time
    for (qsizetype i = 0; i < given.size(); ++i) diff |= static_cast<unsigned char>(given.at(i) ^ expected.at(i));
    return diff == 0 ? 0 : 401;
}

QByteArray httpResponse(int status, const QByteArray &body, const QByteArray &contentType) {
    static const QHash<int, QByteArray> reasons{{200, "OK"},          {202, "Accepted"},  {400, "Bad Request"},
                                                {401, "Unauthorized"}, {403, "Forbidden"}, {404, "Not Found"},
                                                {405, "Method Not Allowed"}};
    QByteArray r = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasons.value(status, "Error") + "\r\n";
    if (!body.isEmpty()) r += "Content-Type: " + contentType + "\r\n";
    if (status == 405) r += "Allow: POST\r\n";
    r += "Content-Length: " + QByteArray::number(body.size()) + "\r\nCache-Control: no-store\r\n\r\n" + body;
    return r;
}

QByteArray newToken() {
    QByteArray bytes(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), bytes.size() / 4);
    return bytes.toHex();
}

}  // namespace mcp
