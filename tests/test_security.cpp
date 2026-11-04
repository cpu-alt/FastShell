// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Hostile input from the remote side: whatever a server sends, the terminal
// and the Telnet decoder must not crash, run away or answer with data that
// could be typed back into the shell.
#include <gtest/gtest.h>

#include <random>
#include <string>

#include "core/TelnetProtocol.h"
#include "terminal/Screen.h"

namespace {

void feed(Screen &s, std::string_view text) {
    s.feed(text.data(), text.size());
}

// Random bytes biased towards escape sequences, digits and separators.
std::string hostileChunk(std::mt19937 &rng) {
    static const std::string pieces[] = {
        "\x1b[", "\x1b]", "\x1bP", "\x1b(0", "\x1b#8", "\x07", "\x1b\\", ";", ":", "?", ">", "999999999", "65535",
        "-1", "0", "1", "\r\n", "\x08", "\t", "m", "H", "J", "K", "r", "S", "T", "L", "M", "@", "P", "X", "b",
        "I", "h", "l", "n", "c", "t", "q", "\xe4\xb8\xad", "\xf0\x9f\x98\x80", "\xc3", "\xff", "\x9b", "\x90",
        "\x1b[?1049h", "\x1b[?1049l", "\x1b[2J", "52;c;", "0;", "2;", "21", "\x1b[6n", "\x1b[c", "\x1b[>c",
    };
    std::uniform_int_distribution<int> count(1, 40);
    std::uniform_int_distribution<std::size_t> pick(0, std::size(pieces) - 1);
    std::uniform_int_distribution<int> byte(0, 255);
    std::string out;
    for (int i = count(rng); i > 0; --i) {
        if (rng() % 4 == 0) out += char(byte(rng));
        else out += pieces[pick(rng)];
    }
    return out;
}

}  // namespace

TEST(Security, TerminalSurvivesHostileOutput) {
    std::mt19937 rng(20261104);  // fixed seed: reproducible
    Screen screen(24, 80, 2000);
    std::size_t replies = 0;
    screen.onReply = [&](const std::string &reply) {
        ++replies;
        // Only fixed status answers, never text chosen by the server.
        ASSERT_FALSE(reply.empty());
        ASSERT_EQ(reply.front(), '\x1b');
        const char last = reply.back();
        ASSERT_TRUE(last == 'n' || last == 'R' || last == 'c') << reply;
        ASSERT_LE(reply.size(), 32u);
    };
    std::uniform_int_distribution<int> size(1, 300);
    for (int i = 0; i < 20000; ++i) {
        feed(screen, hostileChunk(rng));
        if (i % 500 == 0) screen.resize(size(rng) % 100 + 1, size(rng));
        ASSERT_GE(screen.cursorRow(), 0);
        ASSERT_LT(screen.cursorRow(), screen.rows());
        ASSERT_GE(screen.cursorColumn(), 0);
        ASSERT_LT(screen.cursorColumn(), screen.columns());
    }
    EXPECT_GT(replies, 0u);  // the generator did ask for reports
}

TEST(Security, TerminalNeverReportsItsTitleOrClipboard) {
    Screen screen(24, 80);
    std::string replies;
    screen.onReply = [&](const std::string &reply) { replies += reply; };
    // Set a title, then ask for it back (CSI 21 t) and for the clipboard (OSC 52):
    // a classic way to make the terminal "type" attacker text into the shell.
    feed(screen, "\x1b]2;rm -rf ~\r\x07");
    feed(screen, "\x1b[21t\x1b[20t\x1b]52;c;?\x07\x1b]52;c;cm0gLXJmIH4K\x07\x1bP$qm\x1b\\");
    EXPECT_EQ(replies.find("rm -rf"), std::string::npos);
    EXPECT_TRUE(replies.empty()) << replies;
}

TEST(Security, TerminalBoundsHugeRequests) {
    Screen screen(24, 80, 100);
    // Huge counts must be clamped, not looped over or allocated.
    feed(screen, "x\x1b[999999999b\x1b[999999999@\x1b[999999999L\x1b[999999999S\x1b[999999999I");
    feed(screen, "\x1b[999999999;999999999H\x1b[999999999X\x1b[999999999P\x1b[999999999T");
    EXPECT_LT(screen.cursorRow(), screen.rows());
    EXPECT_LT(screen.cursorColumn(), screen.columns());
    EXPECT_LE(screen.historySize(), 128);  // history ring: 100 rounded up to a power of two
    // A title longer than the limit is cut, never kept whole.
    std::size_t titleSize = 0;
    screen.onTitle = [&](const std::string &title) { titleSize = title.size(); };
    feed(screen, "\x1b]0;" + std::string(1 << 20, 'A') + "\x07");
    EXPECT_LE(titleSize, 64u * 1024u);
}

TEST(Security, TelnetSurvivesHostileServer) {
    std::mt19937 rng(42);
    QByteArray sent;
    telnet::Engine engine([&](const QByteArray &data) { sent += data; });
    engine.begin();
    std::uniform_int_distribution<int> byte(0, 255);
    qsizetype received = 0;
    for (int i = 0; i < 5000; ++i) {
        QByteArray chunk;
        for (int n = 0; n < 64; ++n) {
            // Lots of IAC so most bytes are protocol commands.
            chunk.append(rng() % 3 == 0 ? char(0xFF) : char(byte(rng)));
        }
        received += chunk.size();
        engine.receive(chunk);
    }
    // Answers stay proportionate: no reply storm, no endless negotiation loop.
    EXPECT_LT(sent.size(), received);
}

TEST(Security, TelnetCapsSubnegotiation) {
    QByteArray sent;
    telnet::Engine engine([&](const QByteArray &data) { sent += data; });
    QByteArray huge;
    huge.append(char(0xFF)).append(char(0xFA)).append(char(24));  // IAC SB TTYPE
    huge.append(QByteArray(10 * 1024 * 1024, 'x'));               // never ended
    const QByteArray data = engine.receive(huge);
    EXPECT_TRUE(data.isEmpty());
    // Back to normal data once the server ends it.
    const QByteArray after = engine.receive(QByteArray("\xff\xf0ok", 4));
    EXPECT_EQ(after, "ok");
}
