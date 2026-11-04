// Copyright 2026 FastShell contributors. Apache-2.0.
#include <gtest/gtest.h>

#include <string>

#include "terminal/CharWidth.h"
#include "terminal/Screen.h"

namespace {

void feed(Screen &s, std::string_view text) {
    s.feed(text.data(), text.size());
}

std::string row(const Screen &s, int r) {
    std::u32string text = s.text(s.screenTopLine() + r, 0, s.screenTopLine() + r, s.columns() - 1);
    std::string out;
    for (char32_t c : text) {
        out += c < 0x80 ? char(c) : '?';
    }
    return out;
}

}  // namespace

TEST(CharWidth, Basics) {
    EXPECT_EQ(charWidth(U'a'), 1);
    EXPECT_EQ(charWidth(U'é'), 1);
    EXPECT_EQ(charWidth(U'́'), 0);  // combining acute
    EXPECT_EQ(charWidth(U'中'), 2);
    EXPECT_EQ(charWidth(U'😀'), 2);
}

TEST(Screen, PrintsAndWraps) {
    Screen s(3, 5);
    feed(s, "hello world");
    EXPECT_EQ(row(s, 0), "hello");
    EXPECT_EQ(row(s, 1), " worl");
    EXPECT_EQ(row(s, 2), "d");
    EXPECT_TRUE(s.screenLine(0).wrapped);
    // Soft-wrapped lines are joined when copied.
    EXPECT_EQ(s.text(s.screenTopLine(), 0, s.screenTopLine() + 2, 4), U"hello world");
}

TEST(Screen, PendingWrapDoesNotScrollEarly) {
    Screen s(2, 3);
    feed(s, "abc");
    EXPECT_EQ(s.cursorRow(), 0);
    EXPECT_EQ(s.cursorColumn(), 2);
    feed(s, "\r\n");
    EXPECT_EQ(s.cursorRow(), 1);
}

TEST(Screen, ScrollbackKeepsLines) {
    Screen s(2, 10, 100);
    feed(s, "one\r\ntwo\r\nthree\r\nfour");
    EXPECT_EQ(s.historySize(), 2);
    EXPECT_EQ(row(s, 0), "three");
    const Line *first = s.line(s.firstLine());
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->cells[0].ch, U'o');
}

TEST(Screen, CursorMovementAndErase) {
    Screen s(5, 10);
    feed(s, "\x1b[3;4HX");
    EXPECT_EQ(row(s, 2), "   X");
    feed(s, "\x1b[2J\x1b[H");
    EXPECT_EQ(row(s, 2), "");
    EXPECT_EQ(s.cursorRow(), 0);
    feed(s, "abcdef\x1b[3D\x1b[K");
    EXPECT_EQ(row(s, 0), "abc");
    feed(s, "\x1b[1G\x1b[2P");
    EXPECT_EQ(row(s, 0), "c");
    feed(s, "\x1b[2@");
    EXPECT_EQ(row(s, 0), "  c");
}

TEST(Screen, SgrColors) {
    Screen s(2, 10);
    feed(s, "\x1b[1;31mA\x1b[38;5;200mB\x1b[38;2;1;2;3;48;5;4mC\x1b[0mD");
    const Line &l = s.screenLine(0);
    EXPECT_EQ(l.cells[0].fg, color::indexed(1));
    EXPECT_TRUE(l.cells[0].attrs & AttrBold);
    EXPECT_EQ(l.cells[1].fg, color::indexed(200));
    EXPECT_EQ(l.cells[2].fg, color::rgb(1, 2, 3));
    EXPECT_EQ(l.cells[2].bg, color::indexed(4));
    EXPECT_EQ(l.cells[3].fg, color::Default);
    EXPECT_EQ(l.cells[3].attrs, 0);
}

TEST(Screen, Utf8AndWideCharacters) {
    Screen s(2, 6);
    feed(s, "\xc3\xa9\xe4\xb8\xad|");  // é 中 |
    const Line &l = s.screenLine(0);
    EXPECT_EQ(l.cells[0].ch, U'é');
    EXPECT_EQ(l.cells[1].ch, U'中');
    EXPECT_TRUE(l.cells[1].attrs & AttrWide);
    EXPECT_TRUE(l.cells[2].attrs & AttrWideTrail);
    EXPECT_EQ(l.cells[3].ch, U'|');
    EXPECT_EQ(s.cursorColumn(), 4);
}

TEST(Screen, Utf8SplitAcrossFeeds) {
    Screen s(1, 4);
    feed(s, "\xe4\xb8");
    feed(s, "\xad");
    EXPECT_EQ(s.screenLine(0).cells[0].ch, U'中');
}

TEST(Screen, AlternateScreenRestoresMain) {
    Screen s(3, 10);
    feed(s, "shell$ ");
    feed(s, "\x1b[?1049h\x1b[Hvim");
    EXPECT_TRUE(s.altScreen());
    EXPECT_EQ(row(s, 0), "vim");
    feed(s, "\x1b[?1049l");
    EXPECT_FALSE(s.altScreen());
    EXPECT_EQ(row(s, 0), "shell$");
    EXPECT_EQ(s.cursorColumn(), 7);
}

TEST(Screen, ScrollRegion) {
    Screen s(4, 5);
    feed(s, "A\r\nB\r\nC\r\nD");
    feed(s, "\x1b[2;3r\x1b[3;1H\n");  // scroll lines 2-3 only
    EXPECT_EQ(row(s, 0), "A");
    EXPECT_EQ(row(s, 1), "C");
    EXPECT_EQ(row(s, 2), "");
    EXPECT_EQ(row(s, 3), "D");
    EXPECT_EQ(s.historySize(), 0);
}

TEST(Screen, RepliesToStatusReports) {
    Screen s(5, 10);
    std::string reply;
    s.onReply = [&](const std::string &r) { reply += r; };
    feed(s, "\x1b[2;3H\x1b[6n");
    EXPECT_EQ(reply, "\x1b[2;3R");
    reply.clear();
    feed(s, "\x1b[c");
    EXPECT_EQ(reply, "\x1b[?62;22c");
}

TEST(Screen, TitleAndModes) {
    Screen s(2, 10);
    std::string title;
    s.onTitle = [&](const std::string &t) { title = t; };
    feed(s, "\x1b]0;my title\x07\x1b]2;other\x1b\\");
    EXPECT_EQ(title, "other");
    feed(s, "\x1b[?1h\x1b[?2004h\x1b[?1000h\x1b[?1006h\x1b[?25l");
    EXPECT_TRUE(s.applicationCursorKeys());
    EXPECT_TRUE(s.bracketedPaste());
    EXPECT_EQ(s.mouseMode(), Screen::MouseMode::Click);
    EXPECT_TRUE(s.sgrMouse());
    EXPECT_FALSE(s.cursorVisible());
}

TEST(Screen, LineDrawingCharset) {
    Screen s(1, 5);
    feed(s, "\x1b(0lqk\x1b(Bx");
    const Line &l = s.screenLine(0);
    EXPECT_EQ(l.cells[0].ch, U'┌');
    EXPECT_EQ(l.cells[1].ch, U'─');
    EXPECT_EQ(l.cells[2].ch, U'┐');
    EXPECT_EQ(l.cells[3].ch, U'x');
}

TEST(Screen, ResizeKeepsCursorLineAndUsesHistory) {
    Screen s(4, 10, 100);
    feed(s, "1\r\n2\r\n3\r\n4");
    s.resize(2, 10);
    EXPECT_EQ(row(s, 1), "4");
    EXPECT_EQ(s.cursorRow(), 1);
    EXPECT_EQ(s.historySize(), 2);
    s.resize(4, 10);  // lines come back from history
    EXPECT_EQ(row(s, 0), "1");
    EXPECT_EQ(s.cursorRow(), 3);
    EXPECT_EQ(s.historySize(), 0);
}

TEST(Screen, MalformedInputIsHarmless) {
    Screen s(3, 10);
    feed(s, "\x1b[99999999999;99999999999H\x1b[38;5m\x1b[?\x1b]unterminated");
    feed(s, "\xff\xfe\x80ok");
    EXPECT_LT(s.cursorRow(), 3);
    EXPECT_LT(s.cursorColumn(), 10);
}
