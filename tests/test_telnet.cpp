// Copyright 2026 FastShell contributors. Apache-2.0.
#include <gtest/gtest.h>

#include "core/TelnetProtocol.h"

using namespace telnet;

namespace {
QByteArray bytes(std::initializer_list<int> values) {
    QByteArray out;
    for (int v : values) {
        out.append(char(v));
    }
    return out;
}
}  // namespace

TEST(Telnet, Names) {
    EXPECT_EQ(commandName(IAC), "IAC");
    EXPECT_EQ(commandName(WILL), "WILL");
    EXPECT_EQ(optionName(NAWS), "NAWS");
    EXPECT_EQ(optionName(200), "0xc8(Unknown Telnet Option)");
}

TEST(Telnet, StripsCommandsAndAnswersNegotiation) {
    QByteArray sent;
    Engine engine([&](const QByteArray &d) { sent += d; });

    const QByteArray data = engine.receive(bytes({'h', 'i', IAC, WILL, ECHO, IAC, DO, 99, '!'}));
    EXPECT_EQ(data, "hi!");
    EXPECT_TRUE(engine.remoteEcho());
    EXPECT_EQ(sent, bytes({IAC, DO, ECHO, IAC, WONT, 99}));

    // Repeating WILL ECHO must not trigger another answer (no loops).
    sent.clear();
    engine.receive(bytes({IAC, WILL, ECHO}));
    EXPECT_TRUE(sent.isEmpty());
}

TEST(Telnet, EscapedIacAndCrNul) {
    Engine engine([](const QByteArray &) {});
    EXPECT_EQ(engine.receive(bytes({'a', IAC, IAC, 'b', '\r', 0, 'c'})), bytes({'a', 255, 'b', '\r', 'c'}));
    EXPECT_EQ(engine.encode(bytes({'x', 255, '\r'})), bytes({'x', 255, 255, '\r', 0}));
    EXPECT_EQ(engine.encode("ls\r\n"), "ls\r\n");
}

TEST(Telnet, WindowSizeAndTerminalType) {
    QByteArray sent;
    Engine engine([&](const QByteArray &d) { sent += d; });
    engine.setWindowSize(120, 40);
    engine.begin();
    sent.clear();

    engine.receive(bytes({IAC, DO, NAWS}));
    EXPECT_EQ(sent, bytes({IAC, SB, NAWS, 0, 120, 0, 40, IAC, SE}));  // no WILL again: we offered it

    sent.clear();
    engine.receive(bytes({IAC, DO, TTYPE, IAC, SB, TTYPE, SEND, IAC, SE}));
    EXPECT_EQ(sent, bytes({IAC, SB, TTYPE, IS}) + "XTERM-256COLOR" + bytes({IAC, SE}));
}

TEST(Telnet, SplitAcrossPackets) {
    QByteArray sent;
    Engine engine([&](const QByteArray &d) { sent += d; });
    EXPECT_EQ(engine.receive(bytes({'a', IAC})), "a");
    EXPECT_EQ(engine.receive(bytes({WILL})), "");
    EXPECT_EQ(engine.receive(bytes({SGA, 'b'})), "b");
    EXPECT_TRUE(engine.remoteEnabled(SGA));
}
