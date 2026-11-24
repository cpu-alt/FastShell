// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Claude Code bridge: JSON-RPC tools, password rules, HTTP checks.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "app/CloudTunnel.h"
#include "app/McpProtocol.h"
#include "app/McpStdioBridge.h"

namespace {

QCoreApplication &app() {
    static int argc = 1;
    static char name[] = "mcp-tests";
    static char *argv[] = {name, nullptr};
    static QCoreApplication *instance = QCoreApplication::instance() ? nullptr : new QCoreApplication(argc, argv);
    return instance ? *instance : *QCoreApplication::instance();
}

// A fake terminal: what is sent is echoed, and "show version" prints a line.
class FakeHost : public mcp::Host {
public:
    QStringList screen{QStringLiteral("Switch>")};
    std::uint64_t revision = 1;
    mcp::Access access = mcp::Access::Full;
    bool approveAnswer = true;
    int approvals = 0;
    QByteArray sent;

    QList<mcp::SessionInfo> sessions() const override {
        return {{7, QStringLiteral("COM4"), QStringLiteral("serial"), true, access, {}}};
    }
    bool text(int id, std::int64_t from, int maxLines, mcp::Text &out) const override {
        if (id != 7) return false;
        out.cursorLine = screen.size() - 1;
        out.revision = revision;
        std::int64_t first = from < 0 ? out.cursorLine - maxLines + 1 : from;
        first = std::max<std::int64_t>({first, out.cursorLine - maxLines + 1, 0});
        out.first = first;
        out.lines = screen.mid(int(first));
        return true;
    }
    bool send(int id, const QByteArray &data) override {
        if (id != 7) return false;
        sent += data;
        if (data.endsWith('\r')) {
            screen.last() += QString::fromUtf8(data.chopped(1));
            if (data == "show version\r") screen << QStringLiteral("KT-NOS 1.2");
            if (data == "show run\r") screen << QStringLiteral("!") << QStringLiteral("!") << QStringLiteral("vlan 254") << QString() << QString();
            if (data == "bad\r") screen << QStringLiteral("% Invalid input detected");
            screen << QStringLiteral("Switch>");
            ++revision;
        } else if (data.endsWith('?')) {  // context help, then the partial command again
            const QString partial = QString::fromUtf8(data.chopped(1));
            screen.last() += partial;
            screen << QStringLiteral("  edge   Edge port") << QStringLiteral("Switch>") + partial;
            ++revision;
        } else if (data == "\x15") {
            screen.last() = QStringLiteral("Switch>");
            ++revision;
        }
        return true;
    }
    void approve(int, const QString &, std::function<void(bool)> done) override {
        ++approvals;
        done(approveAnswer);
    }
};

QJsonObject call(mcp::Protocol &protocol, const QString &tool, const QJsonObject &args) {
    QJsonObject result;
    bool done = false;
    protocol.handle({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                     {QStringLiteral("id"), 1},
                     {QStringLiteral("method"), QStringLiteral("tools/call")},
                     {QStringLiteral("params"), QJsonObject{{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), args}}}},
                    [&](const QJsonObject &r) {
                        result = r.value(QStringLiteral("result")).toObject();
                        done = true;
                    });
    const qint64 end = QDateTime::currentMSecsSinceEpoch() + 5000;
    while (!done && QDateTime::currentMSecsSinceEpoch() < end) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_TRUE(done);
    return result;
}

QString textOf(const QJsonObject &result) {
    return result.value(QStringLiteral("content")).toArray().at(0).toObject().value(QStringLiteral("text")).toString();
}

bool isError(const QJsonObject &result) {
    return result.value(QStringLiteral("isError")).toBool();
}

}  // namespace

TEST(Mcp, PasswordPrompts) {
    EXPECT_TRUE(mcp::isPasswordPrompt(QStringLiteral("Password:")));
    EXPECT_TRUE(mcp::isPasswordPrompt(QStringLiteral("admin@10.0.0.1's password: ")));
    EXPECT_TRUE(mcp::isPasswordPrompt(QStringLiteral("Enter passphrase for key 'id':")));
    EXPECT_TRUE(mcp::isPasswordPrompt(QStringLiteral("Mot de passe :")));
    EXPECT_TRUE(mcp::isPasswordPrompt(QStringLiteral("Enter new password again:")));
    EXPECT_FALSE(mcp::isPasswordPrompt(QStringLiteral("Switch(config)#enable secret 5 x")));
    EXPECT_FALSE(mcp::isPasswordPrompt(QStringLiteral("Switch#")));
    EXPECT_FALSE(mcp::isPasswordPrompt(QStringLiteral("Username:")));
}

TEST(Mcp, SecretsAreMasked) {
    EXPECT_EQ(mcp::maskSecrets(QStringLiteral("username admin password 7 0822455D0A16")),
              QStringLiteral("username admin password 7 ********"));
    EXPECT_EQ(mcp::maskSecrets(QStringLiteral("enable secret 5 $1$abc$def")), QStringLiteral("enable secret 5 ********"));
    EXPECT_EQ(mcp::maskSecrets(QStringLiteral("snmp-server community public ro")),
              QStringLiteral("snmp-server community ********"));
    EXPECT_EQ(mcp::maskSecrets(QStringLiteral("Password: hunter2")), QStringLiteral("Password: ********"));
    EXPECT_EQ(mcp::maskSecrets(QStringLiteral("username \"admin\" secret sha256 TWpFeU16Sm1NamszWVRVM1lUVmh=")),
              QStringLiteral("username \"admin\" secret ********"));
    EXPECT_EQ(mcp::maskSecrets(QStringLiteral("radius host 10.1.0.28 auth-port 1812 key 9PTm8xa0== priority 1")),
              QStringLiteral("radius host 10.1.0.28 auth-port 1812 key ********"));
    EXPECT_FALSE(mcp::containsSecret(QStringLiteral("crypto key generate rsa")));
    EXPECT_EQ(mcp::maskSecrets(QStringLiteral("interface eth-0-1")), QStringLiteral("interface eth-0-1"));
    EXPECT_TRUE(mcp::containsSecret(QStringLiteral("username nathan password Secret123")));
    EXPECT_FALSE(mcp::containsSecret(QStringLiteral("show running-config")));
}

TEST(Mcp, NamedKeys) {
    bool ok = false;
    EXPECT_EQ(mcp::namedKey(QStringLiteral("ctrl+c"), &ok), QByteArray("\x03"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(mcp::namedKey(QStringLiteral("Ctrl+Z"), &ok), QByteArray("\x1a"));
    EXPECT_EQ(mcp::namedKey(QStringLiteral("up"), &ok), QByteArray("\x1b[A"));
    EXPECT_EQ(mcp::namedKey(QStringLiteral("enter"), &ok), QByteArray("\r"));
    mcp::namedKey(QStringLiteral("?"), &ok);
    EXPECT_FALSE(ok);
}

TEST(Mcp, InitializeAndListTools) {
    app();
    FakeHost host;
    mcp::Protocol protocol(&host);
    QJsonObject init;
    protocol.handle({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                     {QStringLiteral("id"), 0},
                     {QStringLiteral("method"), QStringLiteral("initialize")},
                     {QStringLiteral("params"), QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")}}}},
                    [&](const QJsonObject &r) { init = r; });
    EXPECT_EQ(init[QStringLiteral("result")][QStringLiteral("protocolVersion")].toString(), QStringLiteral("2025-06-18"));

    bool notified = false;
    protocol.handle({{QStringLiteral("method"), QStringLiteral("notifications/initialized")}},
                    [&](const QJsonObject &r) { notified = r.isEmpty(); });
    EXPECT_TRUE(notified);

    QJsonObject tools;
    protocol.handle({{QStringLiteral("id"), 2}, {QStringLiteral("method"), QStringLiteral("tools/list")}},
                    [&](const QJsonObject &r) { tools = r; });
    EXPECT_EQ(tools[QStringLiteral("result")][QStringLiteral("tools")].toArray().size(), 14);
}

TEST(Mcp, ListReadAndSend) {
    app();
    FakeHost host;
    mcp::Protocol protocol(&host);
    protocol.settleMs = 50;
    protocol.pollMs = 10;

    EXPECT_TRUE(textOf(call(protocol, QStringLiteral("list_sessions"), {})).contains(QStringLiteral("COM4")));
    EXPECT_EQ(textOf(call(protocol, QStringLiteral("read_output"), {{QStringLiteral("session"), 7}})), QStringLiteral("Switch>"));

    const QJsonObject r = call(protocol, QStringLiteral("send_line"),
                               {{QStringLiteral("session"), 7}, {QStringLiteral("text"), QStringLiteral("show version")}});
    EXPECT_FALSE(isError(r));
    EXPECT_EQ(textOf(r), QStringLiteral("KT-NOS 1.2\nSwitch>"));

    EXPECT_FALSE(isError(call(protocol, QStringLiteral("send_keys"),
                              {{QStringLiteral("session"), 7},
                               {QStringLiteral("keys"), QJsonArray{QStringLiteral("show "), QStringLiteral("?")}},
                               {QStringLiteral("wait_seconds"), 0.1}})));
    EXPECT_TRUE(host.sent.endsWith("show ?"));
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("read_output"), {{QStringLiteral("session"), 99}})));
}

TEST(Mcp, AccessLevels) {
    app();
    FakeHost host;
    mcp::Protocol protocol(&host);
    protocol.settleMs = 20;
    protocol.pollMs = 5;
    const QJsonObject args{{QStringLiteral("session"), 7}, {QStringLiteral("text"), QStringLiteral("show clock")}};

    host.access = mcp::Access::ReadOnly;
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("send_line"), args)));
    EXPECT_TRUE(host.sent.isEmpty());

    host.access = mcp::Access::Ask;
    host.approveAnswer = false;
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("send_line"), args)));
    EXPECT_EQ(host.approvals, 1);
    EXPECT_TRUE(host.sent.isEmpty());
    host.approveAnswer = true;
    EXPECT_FALSE(isError(call(protocol, QStringLiteral("send_line"), args)));
    EXPECT_EQ(host.sent, QByteArray("show clock\r"));

    host.access = mcp::Access::Hidden;
    EXPECT_TRUE(textOf(call(protocol, QStringLiteral("list_sessions"), {})).contains(QStringLiteral("No terminal")));
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("read_output"), {{QStringLiteral("session"), 7}})));
}

TEST(Mcp, NeverTypesPasswords) {
    app();
    FakeHost host;
    mcp::Protocol protocol(&host);
    protocol.settleMs = 20;
    protocol.pollMs = 5;

    // A command carrying a secret, or a newline (two commands), is refused.
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("send_line"),
                             {{QStringLiteral("session"), 7}, {QStringLiteral("text"), QStringLiteral("username a password Foo1")}})));
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("send_line"),
                             {{QStringLiteral("session"), 7}, {QStringLiteral("text"), QStringLiteral("enable\rFoo")}})));
    EXPECT_TRUE(host.sent.isEmpty());

    // At a password prompt: nothing but the keys that leave it.
    host.screen << QStringLiteral("Password:");
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("send_line"),
                             {{QStringLiteral("session"), 7}, {QStringLiteral("text"), QStringLiteral("admin")}})));
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("send_keys"),
                             {{QStringLiteral("session"), 7}, {QStringLiteral("keys"), QJsonArray{QStringLiteral("abc")}}})));
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("send_keys"),
                             {{QStringLiteral("session"), 7}, {QStringLiteral("keys"), QJsonArray{QStringLiteral("enter")}}})));
    EXPECT_TRUE(host.sent.isEmpty());
    EXPECT_FALSE(isError(call(protocol, QStringLiteral("send_keys"),
                              {{QStringLiteral("session"), 7},
                               {QStringLiteral("keys"), QJsonArray{QStringLiteral("ctrl+c")}},
                               {QStringLiteral("wait_seconds"), 0.05}})));
    EXPECT_EQ(host.sent, QByteArray("\x03"));
    EXPECT_TRUE(textOf(call(protocol, QStringLiteral("read_output"), {{QStringLiteral("session"), 7}}))
                    .contains(QStringLiteral("wait_for_user")));

    // What Claude reads is masked.
    host.screen = QStringList{QStringLiteral("username admin password 7 0822455D0A16"), QStringLiteral("Switch#")};
    const QString out = textOf(call(protocol, QStringLiteral("read_output"), {{QStringLiteral("session"), 7}}));
    EXPECT_FALSE(out.contains(QStringLiteral("0822455D0A16")));
}

TEST(Mcp, HttpParsing) {
    QByteArray buffer = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:47821\r\nContent-Length: 2\r\n\r\n{}GET";
    mcp::HttpRequest request;
    ASSERT_EQ(mcp::parseHttp(buffer, request), mcp::Parse::Complete);
    EXPECT_EQ(request.method, QByteArray("POST"));
    EXPECT_EQ(request.body, QByteArray("{}"));
    EXPECT_EQ(buffer, QByteArray("GET"));
    EXPECT_EQ(mcp::parseHttp(buffer, request), mcp::Parse::Incomplete);

    QByteArray big = "POST /mcp HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n";
    EXPECT_EQ(mcp::parseHttp(big, request), mcp::Parse::Bad);
    QByteArray junk = "hello\r\n\r\n";
    EXPECT_EQ(mcp::parseHttp(junk, request), mcp::Parse::Bad);
}

TEST(Mcp, HttpChecks) {
    const QByteArray token = mcp::newToken();
    EXPECT_EQ(token.size(), 64);
    EXPECT_NE(token, mcp::newToken());
    mcp::HttpRequest r;
    r.method = "POST";
    r.path = "/mcp";
    r.headers.insert("host", "127.0.0.1:47821");
    r.headers.insert("authorization", "Bearer " + token);
    EXPECT_EQ(mcp::checkHttp(r, token, 47821), 0);

    mcp::HttpRequest wrongToken = r;
    wrongToken.headers.insert("authorization", "Bearer " + mcp::newToken());
    EXPECT_EQ(mcp::checkHttp(wrongToken, token, 47821), 401);
    mcp::HttpRequest noToken = r;
    noToken.headers.remove("authorization");
    EXPECT_EQ(mcp::checkHttp(noToken, token, 47821), 401);
    EXPECT_EQ(mcp::checkHttp(r, QByteArray(), 47821), 401);

    mcp::HttpRequest browser = r;  // a web page trying to reach the server
    browser.headers.insert("origin", "https://evil.example");
    EXPECT_EQ(mcp::checkHttp(browser, token, 47821), 403);
    mcp::HttpRequest rebinding = r;
    rebinding.headers.insert("host", "evil.example:47821");
    EXPECT_EQ(mcp::checkHttp(rebinding, token, 47821), 403);
    mcp::HttpRequest otherPath = r;
    otherPath.path = "/";
    EXPECT_EQ(mcp::checkHttp(otherPath, token, 47821), 404);
}

TEST(Mcp, HttpChunkedBody) {
    QByteArray buffer = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:47821\r\nTransfer-Encoding: chunked\r\n\r\n"
                        "4\r\n{\"a\"\r\n3;x=y\r\n:1}\r\n0\r\n\r\nNEXT";
    mcp::HttpRequest request;
    ASSERT_EQ(mcp::parseHttp(buffer, request), mcp::Parse::Complete);
    EXPECT_EQ(request.body, QByteArray("{\"a\":1}"));
    EXPECT_EQ(buffer, QByteArray("NEXT"));

    QByteArray partial = "POST /mcp HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n4\r\n{\"a";
    EXPECT_EQ(mcp::parseHttp(partial, request), mcp::Parse::Incomplete);
    QByteArray badSize = "POST /mcp HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n";
    EXPECT_EQ(mcp::parseHttp(badSize, request), mcp::Parse::Bad);
    QByteArray huge = "POST /mcp HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFF\r\n";
    EXPECT_EQ(mcp::parseHttp(huge, request), mcp::Parse::Bad);
    QByteArray gzip = "POST /mcp HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n";
    EXPECT_EQ(mcp::parseHttp(gzip, request), mcp::Parse::Bad);
}

TEST(Mcp, HttpRemoteAccess) {
    const QByteArray token = mcp::newToken();
    const QByteArray secret = mcp::newToken();
    mcp::HttpRequest r;
    r.method = "POST";
    r.path = "/mcp/" + secret;
    r.headers.insert("host", "127.0.0.1:47821");  // set by the tunnel
    EXPECT_EQ(mcp::checkHttp(r, token, 47821, secret), 0);
    EXPECT_EQ(mcp::checkHttp(r, token, 47821), 404);  // cloud access off
    mcp::HttpRequest wrong = r;
    wrong.path = "/mcp/" + mcp::newToken();
    EXPECT_EQ(mcp::checkHttp(wrong, token, 47821, secret), 404);
    wrong.path = "/mcp/";
    EXPECT_EQ(mcp::checkHttp(wrong, token, 47821, secret), 404);
    mcp::HttpRequest claude = r;
    claude.headers.insert("origin", "https://claude.ai");
    EXPECT_EQ(mcp::checkHttp(claude, token, 47821, secret), 0);
    mcp::HttpRequest page = r;
    page.headers.insert("origin", "https://evil.example");
    EXPECT_EQ(mcp::checkHttp(page, token, 47821, secret), 403);
    mcp::HttpRequest rebinding = r;
    rebinding.headers.insert("host", "abc.trycloudflare.com");
    EXPECT_EQ(mcp::checkHttp(rebinding, token, 47821, secret), 403);
    // The local endpoint still needs the token, even with cloud access on.
    mcp::HttpRequest local = r;
    local.path = "/mcp";
    EXPECT_EQ(mcp::checkHttp(local, token, 47821, secret), 401);
}

TEST(Mcp, StdioBridgeResponses) {
    QByteArray buffer = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}HTTP/1.1";
    int status = 0;
    QByteArray body;
    bool bad = false;
    ASSERT_TRUE(mcp::parseHttpResponse(buffer, status, body, bad));
    EXPECT_EQ(status, 200);
    EXPECT_EQ(body, QByteArray("{}"));
    EXPECT_EQ(buffer, QByteArray("HTTP/1.1"));
    EXPECT_FALSE(mcp::parseHttpResponse(buffer, status, body, bad));
    EXPECT_FALSE(bad);

    QByteArray accepted = "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n";
    ASSERT_TRUE(mcp::parseHttpResponse(accepted, status, body, bad));
    EXPECT_EQ(status, 202);
    EXPECT_TRUE(body.isEmpty());
    QByteArray junk = "hello\r\n\r\n";
    EXPECT_FALSE(mcp::parseHttpResponse(junk, status, body, bad));
    EXPECT_TRUE(bad);

    const QByteArray error = mcp::unreachableReply(R"({"jsonrpc":"2.0","id":7,"method":"tools/list"})", QStringLiteral("off"));
    const QJsonObject o = QJsonDocument::fromJson(error).object();
    EXPECT_EQ(o.value(QStringLiteral("id")).toInt(), 7);
    EXPECT_EQ(o.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(), QStringLiteral("off"));
    EXPECT_TRUE(mcp::unreachableReply(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", QStringLiteral("off")).isEmpty());
}

TEST(Mcp, TunnelAddress) {
    const QString log = QStringLiteral(
        "2026-11-24T13:40:00Z INF |  Your quick Tunnel has been created! Visit it at (it may take some time to be reachable):  |\n"
        "2026-11-24T13:40:00Z INF |  https://quiet-river-fox-12.trycloudflare.com                                     |\n");
    EXPECT_EQ(CloudTunnel::parseUrl(log), QStringLiteral("https://quiet-river-fox-12.trycloudflare.com"));
    EXPECT_TRUE(CloudTunnel::parseUrl(QStringLiteral("failed to request https://api.trycloudflare.com/tunnel")).isEmpty());
    EXPECT_TRUE(CloudTunnel::parseUrl(QStringLiteral("https://a.trycloudflare.com.evil.example")).isEmpty());
    EXPECT_TRUE(CloudTunnel::parseUrl(QStringLiteral("https://evil.example/x.trycloudflare.com")).isEmpty());
}

TEST(Mcp, Prompts) {
    EXPECT_TRUE(mcp::isCommandPrompt(QStringLiteral("C38-SW-01#")));
    EXPECT_TRUE(mcp::isCommandPrompt(QStringLiteral("C38-SW-01(config-if-range-mgi1-6)#")));
    EXPECT_TRUE(mcp::isCommandPrompt(QStringLiteral("Switch>")));
    EXPECT_TRUE(mcp::isCommandPrompt(QStringLiteral("nathan@srv:~$ ")));
    EXPECT_TRUE(mcp::isCommandPrompt(QStringLiteral(R"(PS C:\Users\WitzN>)")));
    EXPECT_FALSE(mcp::isCommandPrompt(QStringLiteral("Password:")));
    EXPECT_FALSE(mcp::isCommandPrompt(QStringLiteral("  vlan 254")));
    EXPECT_TRUE(mcp::isErrorLine(QStringLiteral("% Invalid input detected at '^' marker.")));
    EXPECT_TRUE(mcp::isErrorLine(QStringLiteral("Unknown command")));
    EXPECT_FALSE(mcp::isErrorLine(QStringLiteral("errdisable recovery")));
    EXPECT_FALSE(mcp::isErrorLine(QStringLiteral("Success")));
}

TEST(Mcp, CompactOutput) {
    const QStringList lines{QStringLiteral("SW# show run"), QStringLiteral("!"), QStringLiteral("!"),
                            QStringLiteral("hostname SW"), QString(), QString(), QStringLiteral("snmp community public ro"),
                            QStringLiteral("interface mgi1   "), QStringLiteral("SW#")};
    mcp::Format f;
    f.command = QStringLiteral("show run");
    EXPECT_EQ(mcp::formatOutput(lines, f),
              QStringLiteral("hostname SW\n\nsnmp community ********\ninterface mgi1\nSW#"));
    f.filter = QStringLiteral("^interface");
    EXPECT_EQ(mcp::formatOutput(lines, f), QStringLiteral("interface mgi1\nSW#"));

    QStringList many;
    for (int i = 0; i < 1000; ++i) many << QStringLiteral("line %1 with some text").arg(i);
    mcp::Format small;
    small.maxChars = 2000;
    const QString capped = mcp::formatOutput(many, small);
    EXPECT_LE(capped.size(), 2200);
    EXPECT_TRUE(capped.contains(QStringLiteral("lines cut")));
    EXPECT_TRUE(capped.startsWith(QStringLiteral("line 0 ")));
    EXPECT_TRUE(capped.endsWith(QStringLiteral("line 999 with some text")));
}

TEST(Mcp, BatchHelpAndFollow) {
    app();
    FakeHost host;
    mcp::Protocol protocol(&host);
    protocol.settleMs = 50;
    protocol.promptSettleMs = 10;
    protocol.pollMs = 5;

    // A batch is one confirmation and stops at the first error.
    host.access = mcp::Access::Ask;
    QJsonObject r = call(protocol, QStringLiteral("run_commands"),
                         {{QStringLiteral("session"), 7},
                          {QStringLiteral("commands"), QJsonArray{QStringLiteral("show version"), QStringLiteral("bad"),
                                                                  QStringLiteral("show run")}}});
    EXPECT_EQ(host.approvals, 1);
    EXPECT_TRUE(textOf(r).contains(QStringLiteral("> show version\nKT-NOS 1.2")));
    EXPECT_TRUE(textOf(r).contains(QStringLiteral("Stopped at this error: 1 command")));
    EXPECT_FALSE(host.sent.contains("show run"));

    // Nothing new since that call.
    EXPECT_TRUE(textOf(call(protocol, QStringLiteral("read_output"), {{QStringLiteral("session"), 7}, {QStringLiteral("new_only"), true}}))
                    .startsWith(QStringLiteral("[Nothing new.]")));

    // Help: no confirmation, the line is cleared afterwards.
    r = call(protocol, QStringLiteral("help"), {{QStringLiteral("session"), 7}, {QStringLiteral("text"), QStringLiteral("spanning-tree ")}});
    EXPECT_EQ(host.approvals, 1);
    EXPECT_EQ(textOf(r), QStringLiteral("  edge   Edge port"));
    EXPECT_EQ(host.screen.last(), QStringLiteral("Switch>"));

    // A secret in a batch: nothing is sent.
    host.access = mcp::Access::Full;
    const QByteArray before = host.sent;
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("run_commands"),
                             {{QStringLiteral("session"), 7},
                              {QStringLiteral("commands"), QJsonArray{QStringLiteral("configure"),
                                                                      QStringLiteral("username admin secret Foo")}}})));
    EXPECT_EQ(host.sent, before);
}

TEST(Mcp, WaitForUser) {
    app();
    FakeHost host;
    mcp::Protocol protocol(&host);
    protocol.settleMs = 50;
    protocol.promptSettleMs = 10;
    protocol.pollMs = 5;
    host.screen << QStringLiteral("Password:");
    QTimer::singleShot(100, [&host] {  // the user types the password
        host.screen << QStringLiteral("Switch#");
        ++host.revision;
    });
    const QJsonObject r = call(protocol, QStringLiteral("wait_for_user"),
                               {{QStringLiteral("session"), 7}, {QStringLiteral("timeout_seconds"), 3}});
    EXPECT_FALSE(isError(r));
    EXPECT_FALSE(textOf(r).contains(QStringLiteral("not answered")));
    EXPECT_TRUE(textOf(r).endsWith(QStringLiteral("Switch#")));
}

TEST(Mcp, FileServerTool) {
    app();
    struct Host : FakeHost {
        QString action;
        void fileServer(const QString &a, const QJsonObject &, std::function<void(const QString &, bool)> done) override {
            action = a;
            done(QStringLiteral("TFTP on 0.0.0.0 port 69"), false);
        }
    } host;
    mcp::Protocol protocol(&host);
    QJsonObject r = call(protocol, QStringLiteral("file_server"), {{QStringLiteral("action"), QStringLiteral("status")}});
    EXPECT_FALSE(isError(r));
    EXPECT_EQ(host.action, QStringLiteral("status"));
    EXPECT_EQ(textOf(r), QStringLiteral("TFTP on 0.0.0.0 port 69"));
    host.action.clear();
    r = call(protocol, QStringLiteral("file_server"), {{QStringLiteral("action"), QStringLiteral("delete")}});
    EXPECT_TRUE(isError(r));
    EXPECT_TRUE(host.action.isEmpty());  // unknown actions never reach FastShell

    FakeHost plain;  // a host without a file server
    mcp::Protocol other(&plain);
    EXPECT_TRUE(isError(call(other, QStringLiteral("file_server"), {{QStringLiteral("action"), QStringLiteral("start")}})));
}

namespace {

// Several fake switches: each echoes and answers "show version" with its name.
class FleetHost : public mcp::Host {
public:
    struct Pane {
        QString title;
        mcp::Access access = mcp::Access::Full;
        QStringList screen{QStringLiteral("SW>")};
        std::uint64_t revision = 1;
    };
    QMap<int, Pane> panes;
    int approvals = 0;
    QString approvedText;
    QString notesText;
    QMap<int, QString> vaults;  // pane -> vault account Claude may use
    int vaultLogins = 0;
    QStringList saved{QStringLiteral("core-1"), QStringLiteral("core-2")};
    int opened = 0;

    QList<mcp::SessionInfo> sessions() const override {
        QList<mcp::SessionInfo> list;
        for (auto it = panes.cbegin(); it != panes.cend(); ++it) {
            list << mcp::SessionInfo{it.key(), it->title, QStringLiteral("ssh"), true, it->access, vaults.value(it.key())};
        }
        return list;
    }
    bool text(int id, std::int64_t from, int maxLines, mcp::Text &out) const override {
        if (!panes.contains(id)) return false;
        const Pane &p = panes[id];
        out.cursorLine = p.screen.size() - 1;
        out.revision = p.revision;
        std::int64_t first = from < 0 ? out.cursorLine - maxLines + 1 : from;
        first = std::max<std::int64_t>({first, out.cursorLine - maxLines + 1, 0});
        out.first = first;
        out.lines = p.screen.mid(int(first));
        return true;
    }
    bool send(int id, const QByteArray &data) override {
        if (!panes.contains(id)) return false;
        Pane &p = panes[id];
        if (data.endsWith('\r')) {
            p.screen.last() += QString::fromUtf8(data.chopped(1));
            if (data == "show version\r") p.screen << QStringLiteral("Model of ") + p.title;
            if (data == "bad\r") p.screen << QStringLiteral("% Invalid input detected");
            p.screen << QStringLiteral("SW>");
            ++p.revision;
        }
        return true;
    }
    void approve(int, const QString &, std::function<void(bool)> done) override { done(false); }
    void approveMany(const QList<int> &, const QString &what, std::function<void(bool)> done) override {
        ++approvals;
        approvedText = what;
        done(true);
    }
    void vaultLogin(int, std::function<void(const QString &, bool)> done) override {
        ++vaultLogins;
        done(QStringLiteral("typed"), false);
    }
    QString notes() const override { return notesText; }
    bool setNotes(const QString &text) override {
        notesText = text;
        return true;
    }
    void openSaved(const QString &name, std::function<void(int, const QString &)> done) override {
        if (!saved.contains(name)) return done(-1, QStringLiteral("unknown"));
        ++opened;
        done(100 + opened, QString());
    }
};

}  // namespace

TEST(Mcp, RunOnSeveralSessions) {
    app();
    FleetHost host;
    host.panes[1].title = QStringLiteral("access-1");
    host.panes[2].title = QStringLiteral("access-2");
    host.panes[2].access = mcp::Access::Ask;
    host.panes[3].title = QStringLiteral("access-3");
    mcp::Protocol protocol(&host);
    protocol.settleMs = 100;
    protocol.promptSettleMs = 20;
    protocol.pollMs = 10;

    const QJsonObject r = call(protocol, QStringLiteral("run_on_sessions"),
                               {{QStringLiteral("sessions"), QJsonArray{1, 2, 3}},
                                {QStringLiteral("commands"), QJsonArray{QStringLiteral("show version")}}});
    ASSERT_FALSE(isError(r)) << textOf(r).toStdString();
    const QString out = textOf(r);
    EXPECT_TRUE(out.contains(QStringLiteral("=== 1 \"access-1\" ===")));
    EXPECT_TRUE(out.contains(QStringLiteral("Model of access-1")));
    EXPECT_TRUE(out.contains(QStringLiteral("Model of access-2")));
    EXPECT_TRUE(out.contains(QStringLiteral("Model of access-3")));
    EXPECT_LT(out.indexOf(QStringLiteral("access-1")), out.indexOf(QStringLiteral("access-3")));  // in order
    EXPECT_EQ(host.approvals, 1);  // one confirmation, only for the "ask" pane
    EXPECT_TRUE(host.approvedText.contains(QStringLiteral("access-2")));
    EXPECT_FALSE(host.approvedText.contains(QStringLiteral("access-1")));

    host.panes[3].access = mcp::Access::ReadOnly;
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("run_on_sessions"),
                             {{QStringLiteral("sessions"), QJsonArray{1, 3}},
                              {QStringLiteral("commands"), QJsonArray{QStringLiteral("show version")}}})));
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("run_on_sessions"),
                             {{QStringLiteral("sessions"), QJsonArray{1, 2}},
                              {QStringLiteral("commands"), QJsonArray{QStringLiteral("username a password x")}}})));
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("run_on_sessions"),
                             {{QStringLiteral("sessions"), QJsonArray{1, 99}},
                              {QStringLiteral("commands"), QJsonArray{QStringLiteral("show version")}}})));
}

TEST(Mcp, OpenSeveralSessions) {
    app();
    FleetHost host;
    mcp::Protocol protocol(&host);
    const QJsonObject r = call(protocol, QStringLiteral("open_sessions"),
                               {{QStringLiteral("names"), QJsonArray{QStringLiteral("core-1"), QStringLiteral("nope"),
                                                                     QStringLiteral("core-2")}}});
    ASSERT_FALSE(isError(r));
    EXPECT_TRUE(textOf(r).contains(QStringLiteral("\"core-1\": session 101")));
    EXPECT_TRUE(textOf(r).contains(QStringLiteral("\"nope\": not opened")));
    EXPECT_TRUE(textOf(r).contains(QStringLiteral("\"core-2\": session 102")));
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("open_sessions"), {{QStringLiteral("names"), QJsonArray{}}})));
}

TEST(Mcp, Notes) {
    app();
    FleetHost host;
    mcp::Protocol protocol(&host);
    EXPECT_TRUE(textOf(call(protocol, QStringLiteral("notes"), {{QStringLiteral("action"), QStringLiteral("read")}}))
                    .contains(QStringLiteral("No notes")));
    EXPECT_FALSE(isError(call(protocol, QStringLiteral("notes"),
                              {{QStringLiteral("action"), QStringLiteral("add")},
                               {QStringLiteral("text"), QStringLiteral("core-1 is the root bridge of VLAN 10")}})));
    EXPECT_FALSE(isError(call(protocol, QStringLiteral("notes"),
                              {{QStringLiteral("action"), QStringLiteral("add")},
                               {QStringLiteral("text"), QStringLiteral("access switches run KT-NOS")}})));
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("notes"),
                             {{QStringLiteral("action"), QStringLiteral("add")},
                              {QStringLiteral("text"), QStringLiteral("enable secret 5 $1$abc")}})));
    const QString read = textOf(call(protocol, QStringLiteral("notes"), {{QStringLiteral("action"), QStringLiteral("read")}}));
    EXPECT_TRUE(read.contains(QStringLiteral("1. ")));
    EXPECT_TRUE(read.contains(QStringLiteral("2. ")));
    EXPECT_TRUE(read.contains(QStringLiteral("root bridge")));
    EXPECT_FALSE(isError(call(protocol, QStringLiteral("notes"),
                              {{QStringLiteral("action"), QStringLiteral("remove")}, {QStringLiteral("line"), 1}})));
    EXPECT_FALSE(host.notesText.contains(QStringLiteral("root bridge")));
    EXPECT_TRUE(host.notesText.contains(QStringLiteral("KT-NOS")));
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("notes"),
                             {{QStringLiteral("action"), QStringLiteral("remove")}, {QStringLiteral("line"), 5}})));
}

TEST(Mcp, VaultLogin) {
    app();
    FleetHost host;
    host.panes[1].title = QStringLiteral("core-1");
    host.panes[1].screen = QStringList{QStringLiteral("login as:")};
    host.panes[2].title = QStringLiteral("other");
    host.vaults[1] = QStringLiteral("core admin");
    mcp::Protocol protocol(&host);
    EXPECT_TRUE(textOf(call(protocol, QStringLiteral("list_sessions"), {})).contains(QStringLiteral("vault=\"core admin\"")));
    EXPECT_FALSE(isError(call(protocol, QStringLiteral("vault_login"), {{QStringLiteral("session"), 1}})));
    EXPECT_EQ(host.vaultLogins, 1);
    // No account Claude may use: the user logs in.
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("vault_login"), {{QStringLiteral("session"), 2}})));
    host.panes[1].access = mcp::Access::ReadOnly;
    EXPECT_TRUE(isError(call(protocol, QStringLiteral("vault_login"), {{QStringLiteral("session"), 1}})));
    EXPECT_EQ(host.vaultLogins, 1);
}
