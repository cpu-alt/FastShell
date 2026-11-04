// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Terminal screen model: the visible grid, the scrollback history, the
// cursor and all xterm modes. Has no GUI dependency, so it is fully testable.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/CircularBuffer.h"
#include "terminal/VtParser.h"

// Color encoding: 0 = default, 0x01'000000 | index (0-255), 0x02'RRGGBB.
namespace color {
constexpr std::uint32_t Default = 0;
constexpr std::uint32_t indexed(int index) { return 0x01000000u | std::uint32_t(index & 0xFF); }
constexpr std::uint32_t rgb(int r, int g, int b) {
    return 0x02000000u | (std::uint32_t(r & 0xFF) << 16) | (std::uint32_t(g & 0xFF) << 8) | std::uint32_t(b & 0xFF);
}
constexpr bool isIndexed(std::uint32_t c) { return (c >> 24) == 1; }
constexpr bool isRgb(std::uint32_t c) { return (c >> 24) == 2; }
}  // namespace color

enum CellAttribute : std::uint16_t {
    AttrBold = 1 << 0,
    AttrDim = 1 << 1,
    AttrItalic = 1 << 2,
    AttrUnderline = 1 << 3,
    AttrBlink = 1 << 4,
    AttrInverse = 1 << 5,
    AttrHidden = 1 << 6,
    AttrStrike = 1 << 7,
    AttrWide = 1 << 8,       // first half of a double-width character
    AttrWideTrail = 1 << 9,  // second half (no glyph of its own)
};

struct Cell {
    char32_t ch = U' ';
    std::uint32_t fg = color::Default;
    std::uint32_t bg = color::Default;
    std::uint16_t attrs = 0;

    bool sameStyle(const Cell &o) const { return fg == o.fg && bg == o.bg && (attrs & 0xFF) == (o.attrs & 0xFF); }
};

struct Line {
    std::vector<Cell> cells;
    bool wrapped = false;  // continues on the next line (soft wrap)
    std::int64_t time = 0;  // seconds since epoch when text was first written, 0 = empty
};

class Screen final : public VtHandler {
public:
    enum class MouseMode { Off, Click, Drag, Motion };
    enum class CursorShape { Block, Underline, Bar };

    Screen(int rows, int columns, std::size_t historyLines = 10000);

    void feed(const char *data, std::size_t length);
    void resize(int rows, int columns);
    void reset();

    int rows() const { return m_rows; }
    int columns() const { return m_columns; }
    int cursorRow() const { return m_cursor.row; }
    int cursorColumn() const { return m_cursor.column; }
    bool cursorVisible() const { return m_cursorVisible; }
    CursorShape cursorShape() const { return m_cursorShape; }
    bool altScreen() const { return m_lines == &m_alt; }

    // Lines are addressed by an absolute number that stays attached to the
    // same text while it scrolls into history.
    std::int64_t firstLine() const;          // oldest line still available
    std::int64_t screenTopLine() const;      // absolute number of screen row 0
    std::int64_t historySize() const { return screenTopLine() - firstLine(); }
    const Line *line(std::int64_t absolute) const;
    const Line &screenLine(int row) const { return (*m_lines)[row]; }

    // Text between two positions (inclusive, row-major), soft wraps joined.
    std::u32string text(std::int64_t fromLine, int fromColumn, std::int64_t toLine, int toColumn) const;

    // Bumped on every visible change; lets the view skip useless repaints.
    std::uint64_t revision() const { return m_revision; }

    // Modes the input side needs to know about.
    bool applicationCursorKeys() const { return m_appCursorKeys; }
    bool applicationKeypad() const { return m_appKeypad; }
    bool bracketedPaste() const { return m_bracketedPaste; }
    bool sgrMouse() const { return m_sgrMouse; }
    bool focusReporting() const { return m_focusReporting; }
    MouseMode mouseMode() const { return m_mouseMode; }
    bool newLineMode() const { return m_newLineMode; }

    std::function<void(const std::string &)> onReply;  // answers to DSR / DA
    std::function<void(const std::string &)> onTitle;
    std::function<void()> onBell;

    // VtHandler
    void print(const char32_t *text, std::size_t length) override;
    void execute(std::uint8_t control) override;
    void escDispatch(std::string_view intermediates, char finalByte) override;
    void csiDispatch(const VtParams &params, std::string_view intermediates, char finalByte) override;
    void oscDispatch(std::string_view data) override;

private:
    struct Cursor {
        int row = 0;
        int column = 0;
        Cell pen;
        bool pendingWrap = false;
        bool originMode = false;
        bool lineDrawingG0 = false;
        bool lineDrawingG1 = false;
        bool shiftOut = false;
    };

    Cell blank() const;
    Line blankLine() const;
    void touch() { ++m_revision; }

    void putChar(char32_t ch);
    void lineFeed();
    void index();
    void reverseIndex();
    void carriageReturn();
    void scrollUp(int top, int bottom, int count);
    void scrollDown(int top, int bottom, int count);
    void moveTo(int row, int column);
    void eraseCells(int row, int from, int to);
    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void insertChars(int count);
    void deleteChars(int count);
    void insertLines(int count);
    void deleteLines(int count);
    void setMode(const VtParams &params, bool isPrivate, bool enable);
    void selectGraphicRendition(const VtParams &params);
    void switchScreen(bool alternate, bool saveCursor, bool clear);
    void saveCursor();
    void restoreCursor();
    void resetTabs();
    void fitLine(Line &line) const;

    VtParser m_parser;
    int m_rows;
    int m_columns;

    std::vector<Line> m_main;
    std::vector<Line> m_alt;
    std::vector<Line> *m_lines = &m_main;
    CircularBuffer<Line> m_history;

    Cursor m_cursor;
    Cursor m_savedMain;
    Cursor m_savedAlt;
    int m_scrollTop = 0;
    int m_scrollBottom = 0;
    std::vector<bool> m_tabs;
    char32_t m_lastPrinted = U' ';

    bool m_autoWrap = true;
    bool m_insertMode = false;
    bool m_newLineMode = false;
    bool m_appCursorKeys = false;
    bool m_appKeypad = false;
    bool m_cursorVisible = true;
    bool m_bracketedPaste = false;
    bool m_sgrMouse = false;
    bool m_focusReporting = false;
    MouseMode m_mouseMode = MouseMode::Off;
    CursorShape m_cursorShape = CursorShape::Block;

    std::uint64_t m_revision = 0;
    std::int64_t m_now = 0;
};
