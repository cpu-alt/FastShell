// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include "terminal/Screen.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "terminal/CharWidth.h"

namespace {

// DEC Special Graphics (ESC ( 0), used by tmux, mc, dialog... for borders.
char32_t lineDrawing(char32_t ch) {
    static constexpr char32_t table[] = {
        U'◆', U'▒', U'␉', U'␌', U'␍', U'␊', U'°', U'±', U'␤', U'␋', U'┘', U'┐', U'┌', U'└', U'┼', U'⎺',
        U'⎻', U'─', U'⎼', U'⎽', U'├', U'┤', U'┴', U'┬', U'│', U'≤', U'≥', U'π', U'≠', U'£', U'·',
    };
    return (ch >= 0x60 && ch <= 0x7E) ? table[ch - 0x60] : ch;
}

}  // namespace

Screen::Screen(int rows, int columns, std::size_t historyLines)
    : m_parser(*this)
    , m_rows(std::max(1, rows))
    , m_columns(std::max(1, columns))
    , m_history(historyLines) {
    reset();
}

void Screen::reset() {
    m_cursor = {};
    m_savedMain = {};
    m_savedAlt = {};
    m_autoWrap = true;
    m_insertMode = false;
    m_newLineMode = false;
    m_appCursorKeys = false;
    m_appKeypad = false;
    m_cursorVisible = true;
    m_bracketedPaste = false;
    m_sgrMouse = false;
    m_focusReporting = false;
    m_mouseMode = MouseMode::Off;
    m_cursorShape = CursorShape::Block;
    m_lines = &m_main;
    m_main.assign(m_rows, blankLine());
    m_alt.assign(m_rows, blankLine());
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    resetTabs();
    m_parser.reset();
    touch();
}

void Screen::resetTabs() {
    m_tabs.assign(m_columns, false);
    for (int c = 8; c < m_columns; c += 8) {
        m_tabs[c] = true;
    }
}

Cell Screen::blank() const {
    // Background color erase: blanks take the current background.
    Cell cell;
    cell.bg = m_cursor.pen.bg;
    return cell;
}

Line Screen::blankLine() const {
    Line line;
    line.cells.assign(m_columns, blank());
    return line;
}

void Screen::fitLine(Line &line) const {
    if (int(line.cells.size()) != m_columns) {
        line.cells.resize(m_columns, Cell{});
        if (!line.cells.empty() && (line.cells.back().attrs & AttrWide)) {
            line.cells.back() = Cell{};  // half of a wide char cannot stay
        }
    }
}

void Screen::feed(const char *data, std::size_t length) {
    m_now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    m_parser.feed(data, length);
}

// --- absolute line addressing ----------------------------------------------

std::int64_t Screen::firstLine() const {
    return m_history.minIndex();
}

std::int64_t Screen::screenTopLine() const {
    return m_history.currentIndex() + 1;
}

const Line *Screen::line(std::int64_t absolute) const {
    const std::int64_t top = screenTopLine();
    if (absolute >= top) {
        const std::int64_t row = absolute - top;
        return row < m_rows ? &(*m_lines)[std::size_t(row)] : nullptr;
    }
    return (!altScreen() && m_history.contains(absolute)) ? &m_history.at(absolute) : nullptr;
}

std::u32string Screen::text(std::int64_t fromLine, int fromColumn, std::int64_t toLine, int toColumn) const {
    std::u32string out;
    for (std::int64_t n = fromLine; n <= toLine; ++n) {
        const Line *l = line(n);
        if (!l) {
            continue;
        }
        const int first = (n == fromLine) ? fromColumn : 0;
        const int last = (n == toLine) ? std::min<int>(toColumn, int(l->cells.size()) - 1) : int(l->cells.size()) - 1;
        std::u32string row;
        for (int c = first; c <= last; ++c) {
            const Cell &cell = l->cells[c];
            if (!(cell.attrs & AttrWideTrail)) {
                row.push_back(cell.ch ? cell.ch : U' ');
            }
        }
        const bool joined = l->wrapped && n != toLine;
        if (!joined) {
            while (!row.empty() && row.back() == U' ') {
                row.pop_back();
            }
        }
        out += row;
        if (!joined && n != toLine) {
            out.push_back(U'\n');
        }
    }
    return out;
}

// --- resize ------------------------------------------------------------------

void Screen::resize(int rows, int columns) {
    rows = std::max(1, rows);
    columns = std::max(1, columns);
    if (rows == m_rows && columns == m_columns) {
        return;
    }

    m_columns = columns;
    for (auto *lines : {&m_main, &m_alt}) {
        for (Line &l : *lines) {
            fitLine(l);
        }
    }

    const bool onAlt = altScreen();
    Cursor &mainCursor = onAlt ? m_savedMain : m_cursor;

    // Main screen: keep the cursor line visible, trade lines with history.
    if (rows < m_rows) {
        int excess = m_rows - rows;
        // Drop blank lines below the cursor first.
        while (excess > 0 && int(m_main.size()) - 1 > mainCursor.row) {
            m_main.pop_back();
            --excess;
        }
        for (; excess > 0; --excess) {
            m_history.append(std::move(m_main.front()));
            m_main.erase(m_main.begin());
            mainCursor.row = std::max(0, mainCursor.row - 1);
        }
    } else {
        for (int extra = rows - m_rows; extra > 0; --extra) {
            if (!m_history.empty()) {
                Line l = m_history.popBack();
                fitLine(l);
                m_main.insert(m_main.begin(), std::move(l));
                ++mainCursor.row;
            } else {
                m_main.push_back(blankLine());
            }
        }
    }

    m_alt.resize(rows, blankLine());
    m_rows = rows;
    for (Line &l : m_alt) {
        fitLine(l);
    }

    for (Cursor *c : {&m_cursor, &m_savedMain, &m_savedAlt}) {
        c->row = std::clamp(c->row, 0, m_rows - 1);
        c->column = std::clamp(c->column, 0, m_columns - 1);
        c->pendingWrap = false;
    }
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    resetTabs();
    touch();
}

// --- printing ----------------------------------------------------------------

void Screen::print(const char32_t *text, std::size_t length) {
    const bool simple = !m_insertMode && !(m_cursor.shiftOut ? m_cursor.lineDrawingG1 : m_cursor.lineDrawingG0);
    for (std::size_t i = 0; i < length; ++i) {
        const char32_t ch = text[i];
        // Fast path for the common case: narrow character, no pending wrap,
        // not touching a wide character. Same result as putChar().
        if (simple && ch >= 0x20 && ch < 0x300 && !m_cursor.pendingWrap) {
            Line &line = (*m_lines)[m_cursor.row];
            Cell &cell = line.cells[m_cursor.column];
            if (!(cell.attrs & (AttrWide | AttrWideTrail))) {
                if (!line.time) line.time = m_now;
                cell = m_cursor.pen;
                cell.ch = ch;
                cell.attrs &= ~(AttrWide | AttrWideTrail);
                m_lastPrinted = ch;
                if (++m_cursor.column >= m_columns) {
                    m_cursor.column = m_columns - 1;
                    m_cursor.pendingWrap = true;
                }
                continue;
            }
        }
        putChar(ch);
    }
    touch();
}

void Screen::putChar(char32_t ch) {
    const bool g1 = m_cursor.shiftOut;
    if ((g1 ? m_cursor.lineDrawingG1 : m_cursor.lineDrawingG0) && ch < 0x80) {
        ch = lineDrawing(ch);
    }

    const int width = charWidth(ch);
    if (width == 0) {
        return;  // combining marks are not composed yet
    }

    if (m_cursor.pendingWrap) {
        if (m_autoWrap) {
            (*m_lines)[m_cursor.row].wrapped = true;
            carriageReturn();
            index();
        }
        m_cursor.pendingWrap = false;
    }

    if (width == 2 && m_cursor.column == m_columns - 1) {
        if (!m_autoWrap || m_columns < 2) {
            return;
        }
        eraseCells(m_cursor.row, m_cursor.column, m_cursor.column);
        (*m_lines)[m_cursor.row].wrapped = true;
        carriageReturn();
        index();
    }

    if (m_insertMode) {
        insertChars(width);
    }

    Line &line = (*m_lines)[m_cursor.row];
    Cell &cell = line.cells[m_cursor.column];
    if (!line.time) {
        line.time = m_now;
    }

    // Overwriting half of an existing wide character clears its other half.
    if ((cell.attrs & AttrWideTrail) && m_cursor.column > 0) {
        line.cells[m_cursor.column - 1] = blank();
    }

    cell = m_cursor.pen;
    cell.ch = ch;
    cell.attrs &= ~(AttrWide | AttrWideTrail);
    if (width == 2) {
        cell.attrs |= AttrWide;
        Cell &trail = line.cells[m_cursor.column + 1];
        trail = m_cursor.pen;
        trail.ch = 0;
        trail.attrs = (trail.attrs & ~AttrWide) | AttrWideTrail;
    } else if (m_cursor.column + 1 < m_columns && (line.cells[m_cursor.column + 1].attrs & AttrWideTrail)) {
        line.cells[m_cursor.column + 1] = blank();
    }
    m_lastPrinted = ch;

    m_cursor.column += width;
    if (m_cursor.column >= m_columns) {
        m_cursor.column = m_columns - 1;
        m_cursor.pendingWrap = true;
    }
}

// --- cursor & scrolling --------------------------------------------------------

void Screen::carriageReturn() {
    m_cursor.column = 0;
    m_cursor.pendingWrap = false;
}

void Screen::lineFeed() {
    index();
    if (m_newLineMode) {
        carriageReturn();
    }
}

void Screen::index() {
    m_cursor.pendingWrap = false;
    if (m_cursor.row == m_scrollBottom) {
        scrollUp(m_scrollTop, m_scrollBottom, 1);
    } else if (m_cursor.row < m_rows - 1) {
        ++m_cursor.row;
    }
}

void Screen::reverseIndex() {
    m_cursor.pendingWrap = false;
    if (m_cursor.row == m_scrollTop) {
        scrollDown(m_scrollTop, m_scrollBottom, 1);
    } else if (m_cursor.row > 0) {
        --m_cursor.row;
    }
}

void Screen::scrollUp(int top, int bottom, int count) {
    count = std::min(count, bottom - top + 1);
    if (count <= 0) {
        return;
    }
    auto &lines = *m_lines;
    const bool keepHistory = !altScreen() && top == 0;
    const Cell b = blank();
    for (int i = 0; i < count; ++i) {
        Line &line = lines[top + i];
        if (keepHistory) {
            m_history.append(std::move(line));
        }
        line.cells.assign(m_columns, b);
        line.wrapped = false;
        line.time = 0;
    }
    // Moves Line objects (cheap vector swaps), never copies cell data.
    std::rotate(lines.begin() + top, lines.begin() + top + count, lines.begin() + bottom + 1);
    touch();
}

void Screen::scrollDown(int top, int bottom, int count) {
    count = std::min(count, bottom - top + 1);
    if (count <= 0) {
        return;
    }
    auto &lines = *m_lines;
    std::rotate(lines.begin() + top, lines.begin() + bottom + 1 - count, lines.begin() + bottom + 1);
    for (int i = 0; i < count; ++i) {
        lines[top + i] = blankLine();
    }
    touch();
}

void Screen::moveTo(int row, int column) {
    const int minRow = m_cursor.originMode ? m_scrollTop : 0;
    const int maxRow = m_cursor.originMode ? m_scrollBottom : m_rows - 1;
    if (m_cursor.originMode) {
        row += m_scrollTop;
    }
    m_cursor.row = std::clamp(row, minRow, maxRow);
    m_cursor.column = std::clamp(column, 0, m_columns - 1);
    m_cursor.pendingWrap = false;
}

void Screen::saveCursor() {
    (altScreen() ? m_savedAlt : m_savedMain) = m_cursor;
}

void Screen::restoreCursor() {
    m_cursor = altScreen() ? m_savedAlt : m_savedMain;
    m_cursor.row = std::clamp(m_cursor.row, 0, m_rows - 1);
    m_cursor.column = std::clamp(m_cursor.column, 0, m_columns - 1);
}

void Screen::switchScreen(bool alternate, bool save, bool clear) {
    if (alternate == altScreen()) {
        return;
    }
    if (alternate) {
        if (save) {
            m_savedMain = m_cursor;
        }
        m_lines = &m_alt;
        if (clear) {
            m_alt.assign(m_rows, blankLine());
        }
    } else {
        if (clear) {
            m_alt.assign(m_rows, blankLine());
        }
        m_lines = &m_main;
        if (save) {
            m_cursor = m_savedMain;
        }
    }
    m_cursor.row = std::clamp(m_cursor.row, 0, m_rows - 1);
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    touch();
}

// --- erasing / editing -----------------------------------------------------------

void Screen::eraseCells(int row, int from, int to) {
    auto &cells = (*m_lines)[row].cells;
    from = std::max(0, from);
    to = std::min(to, m_columns - 1);
    const Cell b = blank();
    for (int c = from; c <= to; ++c) {
        cells[c] = b;
    }
    // Do not leave half of a wide character behind.
    if (from > 0 && (cells[from - 1].attrs & AttrWide)) {
        cells[from - 1] = b;
    }
    if (to + 1 < m_columns && (cells[to + 1].attrs & AttrWideTrail)) {
        cells[to + 1] = b;
    }
    touch();
}

void Screen::eraseInDisplay(int mode) {
    switch (mode) {
    case 0:
        eraseCells(m_cursor.row, m_cursor.column, m_columns - 1);
        for (int r = m_cursor.row + 1; r < m_rows; ++r) {
            (*m_lines)[r] = blankLine();
        }
        break;
    case 1:
        for (int r = 0; r < m_cursor.row; ++r) {
            (*m_lines)[r] = blankLine();
        }
        eraseCells(m_cursor.row, 0, m_cursor.column);
        break;
    case 2:
        for (Line &l : *m_lines) {
            l = blankLine();
        }
        break;
    case 3:
        m_history.clear();
        break;
    }
    touch();
}

void Screen::eraseInLine(int mode) {
    switch (mode) {
    case 0: eraseCells(m_cursor.row, m_cursor.column, m_columns - 1); break;
    case 1: eraseCells(m_cursor.row, 0, m_cursor.column); break;
    case 2: eraseCells(m_cursor.row, 0, m_columns - 1); break;
    }
    if (mode != 1) {
        (*m_lines)[m_cursor.row].wrapped = false;
    }
}

void Screen::insertChars(int count) {
    auto &cells = (*m_lines)[m_cursor.row].cells;
    count = std::min(count, m_columns - m_cursor.column);
    std::move_backward(cells.begin() + m_cursor.column, cells.end() - count, cells.end());
    std::fill(cells.begin() + m_cursor.column, cells.begin() + m_cursor.column + count, blank());
    m_cursor.pendingWrap = false;
    touch();
}

void Screen::deleteChars(int count) {
    auto &cells = (*m_lines)[m_cursor.row].cells;
    count = std::min(count, m_columns - m_cursor.column);
    std::move(cells.begin() + m_cursor.column + count, cells.end(), cells.begin() + m_cursor.column);
    std::fill(cells.end() - count, cells.end(), blank());
    m_cursor.pendingWrap = false;
    touch();
}

void Screen::insertLines(int count) {
    if (m_cursor.row >= m_scrollTop && m_cursor.row <= m_scrollBottom) {
        scrollDown(m_cursor.row, m_scrollBottom, count);
        carriageReturn();
    }
}

void Screen::deleteLines(int count) {
    if (m_cursor.row >= m_scrollTop && m_cursor.row <= m_scrollBottom) {
        // Deleted lines never go to history, even at the top of the screen.
        auto &lines = *m_lines;
        const int bottom = m_scrollBottom;
        count = std::min(count, bottom - m_cursor.row + 1);
        std::rotate(lines.begin() + m_cursor.row, lines.begin() + m_cursor.row + count, lines.begin() + bottom + 1);
        for (int r = bottom - count + 1; r <= bottom; ++r) {
            lines[r] = blankLine();
        }
        carriageReturn();
        touch();
    }
}

// --- control characters & escape sequences ---------------------------------------

void Screen::execute(std::uint8_t control) {
    switch (control) {
    case 0x07:
        if (onBell) onBell();
        break;
    case 0x08:  // BS
        if (m_cursor.pendingWrap) {
            m_cursor.pendingWrap = false;
        } else if (m_cursor.column > 0) {
            --m_cursor.column;
        }
        break;
    case 0x09: {  // HT
        int c = m_cursor.column + 1;
        while (c < m_columns - 1 && !m_tabs[c]) {
            ++c;
        }
        m_cursor.column = std::min(c, m_columns - 1);
        break;
    }
    case 0x0A: case 0x0B: case 0x0C: lineFeed(); break;
    case 0x0D: carriageReturn(); break;
    case 0x0E: m_cursor.shiftOut = true; break;   // SO -> G1
    case 0x0F: m_cursor.shiftOut = false; break;  // SI -> G0
    default: break;
    }
    touch();
}

void Screen::escDispatch(std::string_view intermediates, char finalByte) {
    if (intermediates.empty()) {
        switch (finalByte) {
        case '7': saveCursor(); break;
        case '8': restoreCursor(); break;
        case 'D': index(); break;
        case 'E': carriageReturn(); index(); break;
        case 'M': reverseIndex(); break;
        case 'H': m_tabs[m_cursor.column] = true; break;
        case 'c': m_history.clear(); reset(); break;
        case '=': m_appKeypad = true; break;
        case '>': m_appKeypad = false; break;
        default: break;
        }
    } else if (intermediates == "(" || intermediates == ")") {
        const bool special = finalByte == '0';
        (intermediates[0] == '(' ? m_cursor.lineDrawingG0 : m_cursor.lineDrawingG1) = special;
    } else if (intermediates == "#" && finalByte == '8') {
        // DECALN: fill the screen with 'E' (alignment test).
        for (Line &l : *m_lines) {
            for (Cell &c : l.cells) {
                c = Cell{};
                c.ch = U'E';
            }
        }
    }
    touch();
}

void Screen::setMode(const VtParams &params, bool isPrivate, bool enable) {
    for (int i = 0; i < std::max(1, params.count); ++i) {
        const int mode = params.raw(i);
        if (!isPrivate) {
            if (mode == 4) m_insertMode = enable;
            else if (mode == 20) m_newLineMode = enable;
            continue;
        }
        switch (mode) {
        case 1: m_appCursorKeys = enable; break;
        case 6:
            m_cursor.originMode = enable;
            moveTo(0, 0);
            break;
        case 7: m_autoWrap = enable; break;
        case 25: m_cursorVisible = enable; break;
        case 47: switchScreen(enable, false, false); break;
        case 1047: switchScreen(enable, false, true); break;
        case 1048: enable ? saveCursor() : restoreCursor(); break;
        case 1049: switchScreen(enable, true, true); break;
        case 9: case 1000: m_mouseMode = enable ? MouseMode::Click : MouseMode::Off; break;
        case 1002: m_mouseMode = enable ? MouseMode::Drag : MouseMode::Off; break;
        case 1003: m_mouseMode = enable ? MouseMode::Motion : MouseMode::Off; break;
        case 1004: m_focusReporting = enable; break;
        case 1006: m_sgrMouse = enable; break;
        case 2004: m_bracketedPaste = enable; break;
        default: break;
        }
    }
}

void Screen::selectGraphicRendition(const VtParams &params) {
    Cell &pen = m_cursor.pen;
    const int count = std::max(1, params.count);
    for (int i = 0; i < count; ++i) {
        const int p = params.raw(i);
        switch (p) {
        case 0: pen = Cell{}; break;
        case 1: pen.attrs |= AttrBold; break;
        case 2: pen.attrs |= AttrDim; break;
        case 3: pen.attrs |= AttrItalic; break;
        case 4: pen.attrs |= AttrUnderline; break;
        case 5: case 6: pen.attrs |= AttrBlink; break;
        case 7: pen.attrs |= AttrInverse; break;
        case 8: pen.attrs |= AttrHidden; break;
        case 9: pen.attrs |= AttrStrike; break;
        case 21: pen.attrs |= AttrUnderline; break;
        case 22: pen.attrs &= ~(AttrBold | AttrDim); break;
        case 23: pen.attrs &= ~AttrItalic; break;
        case 24: pen.attrs &= ~AttrUnderline; break;
        case 25: pen.attrs &= ~AttrBlink; break;
        case 27: pen.attrs &= ~AttrInverse; break;
        case 28: pen.attrs &= ~AttrHidden; break;
        case 29: pen.attrs &= ~AttrStrike; break;
        case 39: pen.fg = color::Default; break;
        case 49: pen.bg = color::Default; break;
        case 38:
        case 48: {
            std::uint32_t value = color::Default;
            if (params.raw(i + 1) == 5 && i + 2 < params.count) {
                value = color::indexed(params.raw(i + 2));
                i += 2;
            } else if (params.raw(i + 1) == 2 && i + 4 < params.count) {
                value = color::rgb(params.raw(i + 2), params.raw(i + 3), params.raw(i + 4));
                i += 4;
            } else {
                i = count;  // malformed: ignore the rest
                break;
            }
            (p == 38 ? pen.fg : pen.bg) = value;
            break;
        }
        default:
            if (p >= 30 && p <= 37) pen.fg = color::indexed(p - 30);
            else if (p >= 40 && p <= 47) pen.bg = color::indexed(p - 40);
            else if (p >= 90 && p <= 97) pen.fg = color::indexed(p - 90 + 8);
            else if (p >= 100 && p <= 107) pen.bg = color::indexed(p - 100 + 8);
            break;
        }
    }
}

void Screen::csiDispatch(const VtParams &params, std::string_view intermediates, char finalByte) {
    const bool isPrivate = !intermediates.empty() && intermediates[0] == '?';
    const bool isSecondary = !intermediates.empty() && intermediates[0] == '>';
    const int n = params.get(0, 1);

    if (intermediates == " " && finalByte == 'q') {  // DECSCUSR
        const int style = params.raw(0);
        m_cursorShape = style <= 2 ? CursorShape::Block : style <= 4 ? CursorShape::Underline : CursorShape::Bar;
        touch();
        return;
    }
    if (intermediates == "!" && finalByte == 'p') {  // DECSTR soft reset
        m_cursor.pen = Cell{};
        m_cursor.originMode = false;
        m_insertMode = false;
        m_autoWrap = true;
        m_cursorVisible = true;
        m_appCursorKeys = false;
        m_scrollTop = 0;
        m_scrollBottom = m_rows - 1;
        touch();
        return;
    }

    switch (finalByte) {
    case 'A': moveTo(m_cursor.row - n - (m_cursor.originMode ? m_scrollTop : 0), m_cursor.column); break;
    case 'B': case 'e': moveTo(m_cursor.row + n - (m_cursor.originMode ? m_scrollTop : 0), m_cursor.column); break;
    case 'C': case 'a': moveTo(m_cursor.row - (m_cursor.originMode ? m_scrollTop : 0), m_cursor.column + n); break;
    case 'D': moveTo(m_cursor.row - (m_cursor.originMode ? m_scrollTop : 0), m_cursor.column - n); break;
    case 'E': moveTo(m_cursor.row + n - (m_cursor.originMode ? m_scrollTop : 0), 0); break;
    case 'F': moveTo(m_cursor.row - n - (m_cursor.originMode ? m_scrollTop : 0), 0); break;
    case 'G': case '`': moveTo(m_cursor.row - (m_cursor.originMode ? m_scrollTop : 0), n - 1); break;
    case 'H': case 'f': moveTo(params.get(0, 1) - 1, params.get(1, 1) - 1); break;
    case 'd': moveTo(n - 1, m_cursor.column); break;
    case 'J': eraseInDisplay(params.raw(0)); break;
    case 'K': eraseInLine(params.raw(0)); break;
    case 'L': insertLines(n); break;
    case 'M': deleteLines(n); break;
    case '@': insertChars(n); break;
    case 'P': deleteChars(n); break;
    case 'X': eraseCells(m_cursor.row, m_cursor.column, m_cursor.column + n - 1); break;
    case 'S': if (!isPrivate) scrollUp(m_scrollTop, m_scrollBottom, n); break;
    case 'T': if (!isPrivate) scrollDown(m_scrollTop, m_scrollBottom, n); break;
    case 'b':
        for (int i = 0; i < std::min(n, 65535); ++i) {
            putChar(m_lastPrinted);
        }
        break;
    case 'I':
        for (int i = 0; i < n; ++i) execute(0x09);
        break;
    case 'Z':
        for (int i = 0; i < n && m_cursor.column > 0; ++i) {
            do {
                --m_cursor.column;
            } while (m_cursor.column > 0 && !m_tabs[m_cursor.column]);
        }
        break;
    case 'g':
        if (params.raw(0) == 0) m_tabs[m_cursor.column] = false;
        else if (params.raw(0) == 3) std::fill(m_tabs.begin(), m_tabs.end(), false);
        break;
    case 'h': setMode(params, isPrivate, true); break;
    case 'l': setMode(params, isPrivate, false); break;
    case 'm': if (intermediates.empty()) selectGraphicRendition(params); break;
    case 'r':
        if (!isPrivate) {
            const int top = params.get(0, 1) - 1;
            const int bottom = params.get(1, m_rows) - 1;
            if (top < bottom && bottom < m_rows) {
                m_scrollTop = top;
                m_scrollBottom = bottom;
                moveTo(0, 0);
            }
        }
        break;
    case 's': if (intermediates.empty()) saveCursor(); break;
    case 'u': if (intermediates.empty()) restoreCursor(); break;
    case 'n':
        if (onReply && params.raw(0) == 5) {
            onReply("\x1b[0n");
        } else if (onReply && params.raw(0) == 6) {
            const int row = m_cursor.row + 1 - (m_cursor.originMode ? m_scrollTop : 0);
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "\x1b[%d;%dR", row, m_cursor.column + 1);
            onReply(buffer);
        }
        break;
    case 'c':
        if (onReply && params.raw(0) == 0) {
            // VT220 with ANSI colors; secondary DA identifies as xterm-like.
            onReply(isSecondary ? "\x1b[>1;10;0c" : "\x1b[?62;22c");
        }
        break;
    default:
        break;
    }
    touch();
}

void Screen::oscDispatch(std::string_view data) {
    const auto separator = data.find(';');
    if (separator == std::string_view::npos) {
        return;
    }
    const std::string_view command = data.substr(0, separator);
    if ((command == "0" || command == "2") && onTitle) {
        onTitle(std::string(data.substr(separator + 1)));
    }
}
