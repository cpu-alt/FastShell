// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include "app/TerminalView.h"

#include <QApplication>
#include <QClipboard>
#include <QDrag>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScreen>
#include <algorithm>

#include "app/KeyMapper.h"
#include "widgets/ScrollBar.h"

namespace {

constexpr qsizetype ParseSliceBytes = 1 << 20;  // keep the UI responsive on floods
constexpr int WheelLines = 3;

bool isWordChar(char32_t ch) {
    if (ch == U' ' || ch == 0) {
        return false;
    }
    static constexpr std::u32string_view delimiters = U"\t \"'`()[]{}<>|,;:";
    return delimiters.find(ch) == std::u32string_view::npos;
}

QString toQString(const std::u32string &text) {
    return QString::fromUcs4(text.data(), qsizetype(text.size()));
}

QFont defaultFont() {
    const QStringList preferred = {QStringLiteral("Cascadia Mono"), QStringLiteral("JetBrains Mono"),
                                   QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"),
                                   QStringLiteral("Menlo")};
    const QStringList families = QFontDatabase::families();
    for (const QString &name : preferred) {
        if (families.contains(name)) {
            QFont font(name, 11);
            font.setStyleHint(QFont::Monospace);
            return font;
        }
    }
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    return font;
}

}  // namespace

TerminalView::TerminalView(BackendFactory factory, QWidget *parent)
    : QWidget(parent)
    , m_factory(std::move(factory))
    , m_screen(std::make_unique<Screen>(24, 80, 20000))
    , m_scrollBar(new ScrollBar(Qt::Vertical, this)) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    setAttribute(Qt::WA_OpaquePaintEvent);  // we paint every pixel ourselves
    // Drops (panes, sessions from the tree) are handled by the TabPage around.
    setMouseTracking(true);
    setCursor(Qt::IBeamCursor);

    m_screen->onReply = [this](const std::string &reply) { send(QByteArray::fromStdString(reply)); };
    m_screen->onTitle = [this](const std::string &title) {
        m_title = QString::fromStdString(title);
        Q_EMIT titleChanged(m_title);
    };
    m_screen->onBell = [] { QApplication::beep(); };

    // Never repaint more often than the screen refreshes.
    const qreal hz = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->refreshRate() : 60.0;
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(std::max(4, int(1000.0 / std::max<qreal>(30.0, hz))));
    m_renderTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_renderTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));

    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(60);
    connect(&m_resizeTimer, &QTimer::timeout, this, [this] {
        if (m_backend) m_backend->resize(m_screen->rows(), m_screen->columns());
    });

    m_scrollBar->setCursor(Qt::ArrowCursor);
    connect(m_scrollBar, &ScrollBar::valueChanged, this, [this](qint64 value, qint64) {
        m_scrollOffset = m_screen->historySize() - value;
        scheduleRender();
    });

    setTerminalFont(defaultFont());
}

TerminalView::~TerminalView() = default;

// --- backend ---------------------------------------------------------------------

void TerminalView::start() {
    if (m_backend) {
        m_backend->deleteLater();
    }
    m_receivedData = false;
    m_finished = false;
    m_backend = m_factory(this);
    connect(m_backend, &Backend::readyRead, this, &TerminalView::onReadyRead);
    connect(m_backend, &Backend::errorOccurred, this, [this](const QString &e) { onBackendFinished(e, true); });
    connect(m_backend, &Backend::finished, this, [this](const QString &r) { onBackendFinished(r, false); });
    m_title = m_backend->displayName();
    Q_EMIT titleChanged(m_title);
    m_backend->start(m_screen->rows(), m_screen->columns());
}

void TerminalView::userSend(const QByteArray &data) {
    if (m_broadcast) {
        Q_EMIT broadcastInput(data);  // the tab sends it to every pane
    } else {
        send(data);
    }
}

void TerminalView::writeInput(const QByteArray &data) {
    if (m_finished) return;
    scrollToBottom();
    send(data);
}

void TerminalView::send(const QByteArray &data) {
    if (m_backend && !m_finished && !data.isEmpty()) {
        m_backend->write(data);
    }
}

void TerminalView::message(const QString &text, const char *sgr) {
    const QByteArray line = QByteArray("\r\n\x1b[") + sgr + "m" + text.toUtf8() + "\x1b[0m\r\n";
    m_screen->feed(line.constData(), std::size_t(line.size()));
    updateScrollBar();
    scheduleRender();
}

void TerminalView::onBackendFinished(const QString &reason, bool isError) {
    if (m_finished) {
        return;
    }
    processPending();
    m_finished = true;
    message(reason, isError ? "1;31" : "90");
    message(tr("Press Enter to restart the session."), "90");
}

void TerminalView::onReadyRead() {
    if (!m_backend) {
        return;
    }
    if (!m_receivedData) {
        m_receivedData = true;
        Q_EMIT connected();
    }
    const bool idle = m_pending.isEmpty();
    m_pending += m_backend->readAll();
    if (idle) {
        processPending();
    }
}

void TerminalView::processPending() {
    if (m_pending.isEmpty()) {
        return;
    }
    const std::int64_t viewTop = m_screen->screenTopLine() - m_scrollOffset;
    const qsizetype length = std::min(m_pending.size(), ParseSliceBytes);
    m_screen->feed(m_pending.constData(), std::size_t(length));
    m_pending.remove(0, length);

    // If the user scrolled back, keep the same text in view while output continues.
    if (m_scrollOffset > 0) {
        m_scrollOffset = std::clamp<std::int64_t>(m_screen->screenTopLine() - viewTop, 0, m_screen->historySize());
    }
    updateScrollBar();
    scheduleRender();

    if (!m_pending.isEmpty()) {
        QTimer::singleShot(0, this, &TerminalView::processPending);  // let input and painting run
    }
}

// --- geometry & scrolling -----------------------------------------------------------

void TerminalView::setTerminalFont(const QFont &font) {
    m_font = font;
    m_font.setStyleHint(QFont::Monospace);
    m_font.setKerning(false);
    m_font.setHintingPreference(QFont::PreferFullHinting);
    m_boldFont = m_font;
    m_boldFont.setBold(true);
    m_italicFont = m_font;
    m_italicFont.setItalic(true);

    const QFontMetricsF metrics(m_font);
    m_cellWidth = metrics.horizontalAdvance(QLatin1Char('M'));
    m_cellHeight = std::ceil(metrics.height() * 1.1);  // a little line spacing
    m_ascent = metrics.ascent() + (m_cellHeight - metrics.height()) / 2;
    updateGutter();
}

void TerminalView::updateGutter() {
    int chars = 0;
    if (m_showTimestamps) chars += 10;               // [HH:mm:ss]
    if (m_showLineNumbers) chars += chars ? 7 : 6;   // line number
    m_gutterWidth = chars ? (chars + 1.5) * m_cellWidth : 0;
    updateGeometryFromFont();
    update();
}

void TerminalView::setShowTimestamps(bool show) {
    m_showTimestamps = show;
    updateGutter();
}

void TerminalView::setShowLineNumbers(bool show) {
    m_showLineNumbers = show;
    updateGutter();
}

void TerminalView::sendText(const QString &text) {
    if (m_finished) return;
    scrollToBottom();
    clearSelection();
    QByteArray bytes = text.toUtf8();
    bytes.replace('\n', '\r');
    if (bytes.endsWith('\r')) recordCommand();
    userSend(bytes);
}

void TerminalView::scrollToLine(std::int64_t line) {
    const std::int64_t history = m_screen->altScreen() ? 0 : m_screen->historySize();
    // Put the line about a third of the way down the view.
    const std::int64_t wantedTop = line - m_screen->rows() / 3;
    m_scrollOffset = std::clamp<std::int64_t>(m_screen->screenTopLine() - wantedTop, 0, history);
    m_highlightLine = line;
    updateScrollBar();
    scheduleRender();
    QTimer::singleShot(1500, this, [this, line] {
        if (m_highlightLine == line) {
            m_highlightLine = -1;
            update();
        }
    });
}

// Remembers the command being submitted: the text of the cursor line, minus
// the shell prompt (cut after the last "$ ", "> ", "# ", "% " or "❯ ").
void TerminalView::recordCommand() {
    if (m_screen->altScreen()) return;
    // The command may start on an earlier line if it wrapped.
    std::int64_t start = m_screen->screenTopLine() + m_screen->cursorRow();
    while (start > m_screen->firstLine()) {
        const Line *prev = m_screen->line(start - 1);
        if (!prev || !prev->wrapped) break;
        --start;
    }
    // Read it a moment later: the shell may not have echoed the last keys yet
    // (fast typing, paste, quick commands).
    QTimer::singleShot(200, this, [this, start] { captureCommand(start); });
}

void TerminalView::captureCommand(std::int64_t line) {
    QString text;
    for (std::int64_t l = line;; ++l) {
        const Line *current = m_screen->line(l);
        if (!current) break;
        text += toQString(m_screen->text(l, 0, l, m_screen->columns() - 1));
        if (!current->wrapped) break;
    }
    qsizetype cut = -1;
    for (const QString &marker : {QStringLiteral("$ "), QStringLiteral("> "), QStringLiteral("# "),
                                  QStringLiteral("% "), QStringLiteral("❯ ")}) {
        const qsizetype at = text.indexOf(marker);
        if (at >= 0 && (cut < 0 || at < cut)) cut = at + marker.size();
    }
    if (cut < 0) {  // cmd.exe prompt has no space: "C:\Users>dir"
        static const QRegularExpression cmdPrompt(QStringLiteral(R"(^[A-Za-z]:\\[^>]*>)"));
        if (const auto m = cmdPrompt.match(text); m.hasMatch()) cut = m.capturedLength();
    }
    if (cut < 0 && QStringLiteral(">$#%").contains(text.trimmed().right(1))) {
        return;  // only a prompt, nothing typed
    }
    QString command = (cut >= 0 ? text.mid(cut) : text).trimmed();
    if (command.isEmpty()) return;
    if (!m_commands.isEmpty() && m_commands.last().line == line) {
        m_commands.last().text = command;  // same prompt line re-submitted
    } else {
        m_commands.append({line, command, QDateTime::currentDateTime()});
    }
    // Forget commands whose lines left the history.
    while (!m_commands.isEmpty() && m_commands.first().line < m_screen->firstLine()) {
        m_commands.removeFirst();
    }
    Q_EMIT commandsChanged();
}

void TerminalView::zoom(int delta) {
    QFont font = m_font;
    font.setPointSizeF(std::clamp(font.pointSizeF() + delta, 6.0, 48.0));
    setTerminalFont(font);
}

void TerminalView::resizeEvent(QResizeEvent *) {
    updateGeometryFromFont();
}

void TerminalView::updateGeometryFromFont() {
    const int barWidth = m_scrollBar->sizeHint().width();
    m_scrollBar->setGeometry(width() - barWidth, headerHeight(), barWidth, height() - headerHeight());

    const int columns = int((width() - barWidth - 2 * m_padding - m_gutterWidth) / m_cellWidth);
    const int rows = int((height() - 2 * m_padding - headerHeight()) / m_cellHeight);
    // Layout changes (splitting a pane, restoring a window) pass through
    // tiny transient sizes. Applying them would push the text into history
    // and make ConPty discard what it shows, so they are ignored.
    if (!isVisible() || columns < 10 || rows < 3) {
        return;
    }
    if (columns != m_screen->columns() || rows != m_screen->rows()) {
        m_screen->resize(rows, columns);
        clearSelection();
        updateScrollBar();
        // The shell gets the final size once the layout has settled.
        m_resizeTimer.start();
    }
}

void TerminalView::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    updateGeometryFromFont();
}

void TerminalView::updateScrollBar() {
    const std::int64_t history = m_screen->altScreen() ? 0 : m_screen->historySize();
    if (m_screen->altScreen()) {
        m_scrollOffset = 0;
    }
    m_scrollOffset = std::clamp<std::int64_t>(m_scrollOffset, 0, history);
    const QSignalBlocker blocker(m_scrollBar);
    m_scrollBar->setRange(0, history);
    m_scrollBar->setPage(m_screen->rows());
    m_scrollBar->setValue(history - m_scrollOffset);
    m_lastHistorySize = history;
}

void TerminalView::scrollBy(int lines) {
    const std::int64_t history = m_screen->altScreen() ? 0 : m_screen->historySize();
    m_scrollOffset = std::clamp<std::int64_t>(m_scrollOffset + lines, 0, history);
    updateScrollBar();
    scheduleRender();
}

void TerminalView::scrollToBottom() {
    if (m_scrollOffset != 0) {
        m_scrollOffset = 0;
        updateScrollBar();
        scheduleRender();
    }
}

void TerminalView::scheduleRender() {
    if (!m_renderTimer.isActive()) {
        m_renderTimer.start();
    }
}

// --- painting --------------------------------------------------------------------------

bool TerminalView::isSelected(std::int64_t line, int column) const {
    if (!m_hasSelection) {
        return false;
    }
    const auto [from, to] = std::minmax(m_selectionAnchor, m_selectionHead);
    const Position p{line, column};
    return !(p < from) && !(to < p);
}

void TerminalView::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), m_theme.background);

    const int rows = m_screen->rows();
    const int columns = m_screen->columns();
    const std::int64_t top = m_screen->screenTopLine() - m_scrollOffset;
    const bool focused = hasFocus();

    QString run;
    run.reserve(columns);

    // Gutter: timestamp and line number, command lines highlighted.
    const QColor gutterText(0x5C, 0x63, 0x70);
    const QColor commandBand(0x26, 0x2A, 0x33);
    const QColor accent(0x61, 0xAF, 0xEF);
    auto isCommandLine = [this](std::int64_t line) {
        auto it = std::lower_bound(m_commands.begin(), m_commands.end(), line,
                                   [](const Command &c, std::int64_t l) { return c.line < l; });
        return it != m_commands.end() && it->line == line;
    };
    if (m_gutterWidth > 0) {
        painter.fillRect(QRectF(0, 0, m_padding + m_gutterWidth - 6, height()), QColor(0x17, 0x19, 0x1E));
    }

    for (int r = 0; r < rows; ++r) {
        const Line *line = m_screen->line(top + r);
        if (!line) {
            continue;
        }
        const qreal y = textTop() + r * m_cellHeight;

        const bool command = !m_screen->altScreen() && isCommandLine(top + r);
        if (command || top + r == m_highlightLine) {
            painter.fillRect(QRectF(textLeft() - 4, y, width(), m_cellHeight),
                             top + r == m_highlightLine ? m_theme.selection : commandBand);
        }
        if (m_gutterWidth > 0) {
            QString gutter;
            if (m_showTimestamps) {
                gutter = line->time ? QDateTime::fromSecsSinceEpoch(line->time).toString(QStringLiteral("[HH:mm:ss]"))
                                    : QStringLiteral("          ");
            }
            if (m_showLineNumbers) {
                if (!gutter.isEmpty()) gutter += QLatin1Char(' ');
                gutter += QString::number(top + r + 1).rightJustified(6);
            }
            painter.setFont(m_font);
            painter.setPen(command ? accent : gutterText);
            painter.drawText(QPointF(m_padding, y + m_ascent), gutter);
        }
        const int count = std::min<int>(columns, int(line->cells.size()));

        // Resolve the colors of one cell, honoring inverse and selection.
        auto colorsOf = [&](int c, QColor &fg, QColor &bg) {
            const Cell &cell = line->cells[c];
            const bool bold = cell.attrs & AttrBold;
            fg = m_theme.resolve(cell.fg, true, bold);
            bg = m_theme.resolve(cell.bg, false, false);
            if (cell.attrs & AttrInverse) {
                std::swap(fg, bg);
            }
            if (isSelected(top + r, c)) {
                bg = m_theme.selection;
            }
            if (cell.attrs & AttrDim) {
                fg.setAlphaF(0.6f);
            }
        };

        // Pass 1: backgrounds, merged into runs of equal color.
        for (int c = 0; c < count;) {
            QColor fg, bg;
            colorsOf(c, fg, bg);
            int end = c + 1;
            for (; end < count; ++end) {
                QColor fg2, bg2;
                colorsOf(end, fg2, bg2);
                if (bg2 != bg) {
                    break;
                }
            }
            if (bg != m_theme.background) {
                painter.fillRect(QRectF(textLeft() + c * m_cellWidth, y, (end - c) * m_cellWidth, m_cellHeight), bg);
            }
            c = end;
        }

        // Pass 2: text. ASCII cells with the same style are drawn as one
        // string; other glyphs are placed one by one so a fallback font with
        // a different advance cannot shift the grid.
        int runStart = 0;
        QColor runColor;
        std::uint16_t runAttrs = 0;
        auto flush = [&](int endColumn) {
            if (run.isEmpty()) {
                return;
            }
            const QFont &font = (runAttrs & AttrBold) ? m_boldFont : (runAttrs & AttrItalic) ? m_italicFont : m_font;
            painter.setFont(font);
            painter.setPen(runColor);
            const qreal x = textLeft() + runStart * m_cellWidth;
            painter.drawText(QPointF(x, y + m_ascent), run);
            if (runAttrs & (AttrUnderline | AttrStrike)) {
                const qreal width = (endColumn - runStart) * m_cellWidth;
                if (runAttrs & AttrUnderline) {
                    painter.fillRect(QRectF(x, y + m_ascent + 1, width, 1), runColor);
                }
                if (runAttrs & AttrStrike) {
                    painter.fillRect(QRectF(x, y + m_cellHeight / 2, width, 1), runColor);
                }
            }
            run.clear();
        };

        for (int c = 0; c < count; ++c) {
            const Cell &cell = line->cells[c];
            if (cell.attrs & AttrWideTrail) {
                continue;
            }
            QColor fg, bg;
            colorsOf(c, fg, bg);
            const std::uint16_t attrs = cell.attrs & (AttrBold | AttrItalic | AttrUnderline | AttrStrike);
            const bool visible = !(cell.attrs & AttrHidden);
            const char32_t ch = visible ? cell.ch : U' ';
            const bool ascii = ch < 0x80;

            if (!run.isEmpty() && (!ascii || fg != runColor || attrs != runAttrs)) {
                flush(c);
            }
            if (ascii) {
                if (run.isEmpty()) {
                    runStart = c;
                    runColor = fg;
                    runAttrs = attrs;
                }
                run.append(QChar(char16_t(ch)));
            } else {
                runStart = c;
                runColor = fg;
                runAttrs = attrs;
                run = QString::fromUcs4(&ch, 1);
                flush(c + ((cell.attrs & AttrWide) ? 2 : 1));
            }
        }
        flush(count);
    }

    // Cursor (only when the bottom of the buffer is in view).
    if (m_scrollOffset == 0 && m_screen->cursorVisible() && !m_finished) {
        const qreal x = textLeft() + m_screen->cursorColumn() * m_cellWidth;
        const qreal y = textTop() + m_screen->cursorRow() * m_cellHeight;
        const Cell &cell = m_screen->screenLine(m_screen->cursorRow()).cells[m_screen->cursorColumn()];
        const qreal w = m_cellWidth * ((cell.attrs & AttrWide) ? 2 : 1);
        if (!focused) {
            painter.setPen(m_theme.cursor);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(x + 0.5, y + 0.5, w - 1, m_cellHeight - 1));
        } else {
            switch (m_screen->cursorShape()) {
            case Screen::CursorShape::Block:
                painter.fillRect(QRectF(x, y, w, m_cellHeight), m_theme.cursor);
                if (cell.ch > U' ') {
                    painter.setFont(m_font);
                    painter.setPen(m_theme.background);
                    painter.drawText(QPointF(x, y + m_ascent), QString::fromUcs4(&cell.ch, 1));
                }
                break;
            case Screen::CursorShape::Underline:
                painter.fillRect(QRectF(x, y + m_cellHeight - 2, w, 2), m_theme.cursor);
                break;
            case Screen::CursorShape::Bar:
                painter.fillRect(QRectF(x, y, 2, m_cellHeight), m_theme.cursor);
                break;
            }
        }
    }
    if (m_broadcast) {
        // Orange frame: what you type goes to every pane of the tab.
        painter.setPen(QPen(QColor(0xFF, 0x9F, 0x1C), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(1, 1, width() - m_scrollBar->width() - 2, height() - 2));
    } else if (m_activeFrame && !m_headerVisible) {
        painter.fillRect(QRectF(0, 0, width() - m_scrollBar->width(), 2), m_theme.cursor);
    }
    if (m_headerVisible) {
        paintHeader(painter);
    }
    m_paintedRevision = m_screen->revision();
}

// --- keyboard --------------------------------------------------------------------------

bool TerminalView::focusNextPrevChild(bool) {
    return false;  // Tab belongs to the shell
}

void TerminalView::keyPressEvent(QKeyEvent *event) {
    const auto mods = event->modifiers();
    const bool shiftOnly = (mods & ~Qt::KeypadModifier) == Qt::ShiftModifier;

    if (shiftOnly) {
        switch (event->key()) {
        case Qt::Key_PageUp: scrollBy(m_screen->rows() - 1); return;
        case Qt::Key_PageDown: scrollBy(-(m_screen->rows() - 1)); return;
        case Qt::Key_Home: scrollBy(int(m_screen->historySize())); return;
        case Qt::Key_End: scrollToBottom(); return;
        case Qt::Key_Insert: paste(); return;
        default: break;
        }
    }

    if (m_finished) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            message(QString(), "0");
            start();
        }
        return;
    }

    const KeyModes modes{m_screen->applicationCursorKeys(), m_screen->applicationKeypad(), m_screen->newLineMode(),
                         m_backspaceCtrlH};
    const QByteArray bytes = mapKey(*event, modes);
    if (!bytes.isEmpty()) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            recordCommand();
        }
        clearSelection();
        scrollToBottom();
        userSend(bytes);
    }
}

void TerminalView::inputMethodEvent(QInputMethodEvent *event) {
    if (!event->commitString().isEmpty()) {
        scrollToBottom();
        userSend(event->commitString().toUtf8());
    }
    event->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImCursorRectangle) {
        return QRectF(textLeft() + m_screen->cursorColumn() * m_cellWidth, textTop() + m_screen->cursorRow() * m_cellHeight,
                      m_cellWidth, m_cellHeight).toRect();
    }
    return QWidget::inputMethodQuery(query);
}

void TerminalView::focusInEvent(QFocusEvent *event) {
    if (m_screen->focusReporting()) {
        send("\x1b[I");
    }
    update();
    QWidget::focusInEvent(event);
}

void TerminalView::focusOutEvent(QFocusEvent *event) {
    if (m_screen->focusReporting()) {
        send("\x1b[O");
    }
    update();
    QWidget::focusOutEvent(event);
}

// --- clipboard ---------------------------------------------------------------------------

void TerminalView::copySelection() {
    if (!m_hasSelection) {
        return;
    }
    const auto [from, to] = std::minmax(m_selectionAnchor, m_selectionHead);
    const QString text = toQString(m_screen->text(from.line, from.column, to.line, to.column));
    QGuiApplication::clipboard()->setText(text);
    if (QGuiApplication::clipboard()->supportsSelection()) {
        QGuiApplication::clipboard()->setText(text, QClipboard::Selection);
    }
}

void TerminalView::paste() {
    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
        return;
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    QByteArray bytes = text.toUtf8();
    if (m_screen->bracketedPaste()) {
        bytes.replace("\x1b[201~", "");  // a pasted end marker must not escape the paste
        bytes = "\x1b[200~" + bytes + "\x1b[201~";
    }
    scrollToBottom();
    userSend(bytes);
}

// --- mouse ---------------------------------------------------------------------------------

TerminalView::Position TerminalView::positionAt(const QPointF &point) const {
    const int row = std::clamp(int((point.y() - textTop()) / m_cellHeight), 0, m_screen->rows() - 1);
    const int column = std::clamp(int((point.x() - textLeft()) / m_cellWidth), 0, m_screen->columns() - 1);
    return {m_screen->screenTopLine() - m_scrollOffset + row, column};
}

void TerminalView::clearSelection() {
    if (m_hasSelection) {
        m_hasSelection = false;
        scheduleRender();
    }
}

bool TerminalView::reportMouse(QMouseEvent *event, int button, bool press, bool motion) {
    const auto mode = m_screen->mouseMode();
    if (mode == Screen::MouseMode::Off || (event->modifiers() & Qt::ShiftModifier) || m_scrollOffset != 0) {
        return false;  // Shift forces local selection, like xterm
    }
    if (motion && (mode == Screen::MouseMode::Click || (mode == Screen::MouseMode::Drag && button < 0))) {
        return true;
    }
    const int row = std::clamp(int((event->position().y() - textTop()) / m_cellHeight), 0, m_screen->rows() - 1) + 1;
    const int col = std::clamp(int((event->position().x() - textLeft()) / m_cellWidth), 0, m_screen->columns() - 1) + 1;

    int code = button < 0 ? 3 : button;
    if (motion) code += 32;
    if (event->modifiers() & Qt::ShiftModifier) code += 4;
    if (event->modifiers() & Qt::AltModifier) code += 8;
    if (event->modifiers() & Qt::ControlModifier) code += 16;

    QByteArray seq;
    if (m_screen->sgrMouse()) {
        seq = "\x1b[<" + QByteArray::number(code) + ';' + QByteArray::number(col) + ';' + QByteArray::number(row)
            + (press || motion ? 'M' : 'm');
    } else {
        if (!press && !motion) code = 3;  // X10 has no button id on release
        if (col > 223 || row > 223) return true;
        seq = "\x1b[M";
        seq += char(32 + code);
        seq += char(32 + col);
        seq += char(32 + row);
    }
    send(seq);
    return true;
}

static int buttonCode(Qt::MouseButton button) {
    switch (button) {
    case Qt::LeftButton: return 0;
    case Qt::MiddleButton: return 1;
    case Qt::RightButton: return 2;
    default: return -1;
    }
}

void TerminalView::mousePressEvent(QMouseEvent *event) {
    setFocus();
    if (m_headerVisible && event->position().y() < headerHeight()) {
        if (event->button() == Qt::LeftButton) {
            if (m_closeRect.contains(event->position().toPoint())) Q_EMIT closeRequested();
            else if (m_zoomRect.contains(event->position().toPoint())) Q_EMIT zoomRequested();
            else m_headerPress = event->position().toPoint();  // may become a drag of the pane
        }
        return;
    }
    m_lastMouseButton = buttonCode(event->button());
    if (reportMouse(event, m_lastMouseButton, true, false)) {
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_selectionAnchor = m_selectionHead = positionAt(event->position());
        m_selecting = true;
        m_hasSelection = false;
        scheduleRender();
    } else if (event->button() == Qt::RightButton) {
        // Right click: copy when something is selected, paste otherwise.
        if (m_hasSelection) {
            copySelection();
            clearSelection();
        } else {
            paste();
        }
    } else if (event->button() == Qt::MiddleButton && QGuiApplication::clipboard()->supportsSelection()) {
        userSend(QGuiApplication::clipboard()->text(QClipboard::Selection).toUtf8());
    }
}

void TerminalView::mouseMoveEvent(QMouseEvent *event) {
    if (m_headerPress.x() >= 0) {
        // Title bar held: far enough, and the pane goes where it is dropped.
        if (!(event->buttons() & Qt::LeftButton)) {
            m_headerPress = QPoint(-1, -1);
        } else {
            if ((event->position().toPoint() - m_headerPress).manhattanLength() >= QApplication::startDragDistance()) {
                startPaneDrag();
            }
            return;
        }
    }
    if (m_headerVisible) {
        const bool onHeader = event->position().y() < headerHeight();
        setCursor(onHeader ? Qt::ArrowCursor : Qt::IBeamCursor);
        if (onHeader) {
            update(0, 0, width(), headerHeight());  // button hover
            const QPoint pos = event->position().toPoint();
            setToolTip(m_closeRect.contains(pos)  ? tr("Close this pane")
                       : m_zoomRect.contains(pos) ? (m_zoomed ? tr("Restore") : tr("Enlarge this pane"))
                                                  : tr("Drag to move this pane"));
            setCursor(m_closeRect.contains(pos) || m_zoomRect.contains(pos) ? Qt::ArrowCursor : Qt::OpenHandCursor);
            if (!(event->buttons() & Qt::LeftButton)) return;
        } else if (!toolTip().isEmpty()) {
            setToolTip(QString());
        }
    }
    const int held = (event->buttons() & Qt::LeftButton) ? 0 : (event->buttons() & Qt::MiddleButton) ? 1
                   : (event->buttons() & Qt::RightButton) ? 2 : -1;
    if (reportMouse(event, held, false, true)) {
        return;
    }
    if (m_selecting) {
        // Auto-scroll when dragging above or below the view.
        if (event->position().y() < 0) scrollBy(1);
        else if (event->position().y() > height()) scrollBy(-1);
        const Position head = positionAt(event->position());
        if (!(head == m_selectionHead)) {
            m_selectionHead = head;
            m_hasSelection = !(m_selectionHead == m_selectionAnchor);
            scheduleRender();
        }
    }
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_headerPress.x() >= 0) {
        m_headerPress = QPoint(-1, -1);
        return;
    }
    if (reportMouse(event, buttonCode(event->button()), false, false)) {
        return;
    }
    if (event->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        if (m_hasSelection && QGuiApplication::clipboard()->supportsSelection()) {
            const auto [from, to] = std::minmax(m_selectionAnchor, m_selectionHead);
            QGuiApplication::clipboard()->setText(toQString(m_screen->text(from.line, from.column, to.line, to.column)),
                                                  QClipboard::Selection);
        }
    }
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (m_headerVisible && event->position().y() < headerHeight()) {
        if (!m_closeRect.contains(event->position().toPoint())) Q_EMIT zoomRequested();
        return;
    }
    if (m_screen->mouseMode() != Screen::MouseMode::Off && !(event->modifiers() & Qt::ShiftModifier)) {
        mousePressEvent(event);
        return;
    }
    const Position p = positionAt(event->position());
    const Line *line = m_screen->line(p.line);
    if (!line || !isWordChar(line->cells[p.column].ch)) {
        return;
    }
    int from = p.column;
    int to = p.column;
    while (from > 0 && isWordChar(line->cells[from - 1].ch)) --from;
    while (to + 1 < int(line->cells.size()) && isWordChar(line->cells[to + 1].ch)) ++to;
    m_selectionAnchor = {p.line, from};
    m_selectionHead = {p.line, to};
    m_hasSelection = true;
    m_selecting = false;
    scheduleRender();
}

void TerminalView::wheelEvent(QWheelEvent *event) {
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0) {
        return;
    }
    if (m_screen->mouseMode() != Screen::MouseMode::Off && !(event->modifiers() & Qt::ShiftModifier)) {
        const int row = std::clamp(int((event->position().y() - textTop()) / m_cellHeight), 0, m_screen->rows() - 1) + 1;
        const int col = std::clamp(int((event->position().x() - textLeft()) / m_cellWidth), 0, m_screen->columns() - 1) + 1;
        const int code = steps > 0 ? 64 : 65;
        for (int i = 0; i < std::abs(steps); ++i) {
            if (m_screen->sgrMouse()) {
                send("\x1b[<" + QByteArray::number(code) + ';' + QByteArray::number(col) + ';' + QByteArray::number(row) + 'M');
            } else if (col <= 223 && row <= 223) {
                send(QByteArray("\x1b[M") + char(32 + code) + char(32 + col) + char(32 + row));
            }
        }
    } else if (m_screen->altScreen()) {
        // Full-screen apps without mouse support (less, man): send arrows.
        const QByteArray arrow = steps > 0 ? (m_screen->applicationCursorKeys() ? "\x1bOA" : "\x1b[A")
                                           : (m_screen->applicationCursorKeys() ? "\x1bOB" : "\x1b[B");
        for (int i = 0; i < std::abs(steps) * WheelLines; ++i) {
            send(arrow);
        }
    } else {
        scrollBy(steps * WheelLines);
    }
    event->accept();
}

// --- pane header & drop zones --------------------------------------------------------

int TerminalView::headerHeight() const {
    if (!m_headerVisible) return 0;
    return std::max(24, QFontMetrics(QApplication::font()).height() + 10);
}

void TerminalView::setPaneInfo(const QString &title, const QColor &color) {
    m_paneTitle = title;
    m_paneColor = color;
    update();
}

void TerminalView::setHeaderVisible(bool visible) {
    if (m_headerVisible == visible) return;
    m_headerVisible = visible;
    updateGeometryFromFont();
    update();
}

void TerminalView::setZoomed(bool zoomed) {
    m_zoomed = zoomed;
    update();
}

void TerminalView::paintHeader(QPainter &p) {
    const int h = headerHeight();
    const QRect bar(0, 0, width(), h);
    const QColor accent = m_paneColor.isValid() ? m_paneColor : m_theme.cursor;
    p.fillRect(bar, m_activeFrame ? QColor(0x1E, 0x25, 0x30) : QColor(0x17, 0x1A, 0x20));
    if (m_activeFrame) {
        p.fillRect(QRect(0, h - 2, width(), 2), m_theme.cursor);
    } else {
        p.fillRect(QRect(0, h - 1, width(), 1), QColor(0x24, 0x28, 0x30));
    }

    QFont font = QApplication::font();
    font.setBold(m_activeFrame);
    p.setFont(font);
    const QFontMetrics fm(font);
    p.setRenderHint(QPainter::Antialiasing);

    // Buttons on the right: enlarge / restore, then close.
    const int button = h - 8;
    m_closeRect = QRect(width() - button - 6, 4, button, button);
    m_zoomRect = QRect(m_closeRect.left() - button - 4, 4, button, button);
    const QPoint mouse = mapFromGlobal(QCursor::pos());
    p.setPen(Qt::NoPen);
    if (m_zoomRect.contains(mouse)) {
        p.setBrush(QColor(0xFF, 0xFF, 0xFF, 26));
        p.drawRoundedRect(QRectF(m_zoomRect), 5, 5);
    }
    const bool closeHover = m_closeRect.contains(mouse);
    if (closeHover) {
        p.setBrush(QColor(0xE5, 0x48, 0x4D));
        p.drawRoundedRect(QRectF(m_closeRect), 5, 5);
    }
    QPen icon(QColor(0xAB, 0xB2, 0xBF), 1.4);
    p.setPen(icon);
    p.setBrush(Qt::NoBrush);
    const QRectF z = QRectF(m_zoomRect).adjusted(5, 5, -5, -5);
    if (m_zoomed) {  // two overlapping squares = restore
        p.drawRoundedRect(z.adjusted(2, 0, 0, -2), 1.5, 1.5);
        p.drawRoundedRect(z.adjusted(0, 2, -2, 0), 1.5, 1.5);
    } else {
        p.drawRoundedRect(z, 1.5, 1.5);
    }
    if (closeHover) p.setPen(QPen(Qt::white, 1.4));
    const QRectF c = QRectF(m_closeRect).adjusted(5.5, 5.5, -5.5, -5.5);
    p.drawLine(c.topLeft(), c.bottomRight());
    p.drawLine(c.topRight(), c.bottomLeft());

    // Grip (the bar can be dragged), colored dot, session name (+ state).
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x5C, 0x63, 0x70));
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 2; ++col) {
            p.drawEllipse(QPointF(8 + col * 4, h / 2.0 - 4 + row * 4), 1.1, 1.1);
        }
    }
    p.setBrush(accent);
    p.drawEllipse(QRectF(19, h / 2.0 - 4, 8, 8));
    QString title = m_paneTitle.isEmpty() ? m_title : m_paneTitle;
    if (m_finished) title += tr("  (disconnected)");
    if (m_zoomed) title += tr("  (enlarged)");
    p.setPen(m_activeFrame ? QColor(0xE6, 0xE9, 0xEF) : QColor(0x8A, 0x91, 0x9E));
    const int textWidth = m_zoomRect.left() - 34 - 8;
    p.drawText(QRect(34, 0, textWidth, h), Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(title, Qt::ElideRight, textWidth));
    p.setRenderHint(QPainter::Antialiasing, false);
}

// --- dragging the pane ------------------------------------------------------------

namespace {
QPointer<TerminalView> g_dragSource;
}

TerminalView *TerminalView::dragSource() {
    return g_dragSource;
}

void TerminalView::startPaneDrag() {
    // A small translucent picture of the pane follows the mouse.
    const QPixmap shot = grab();
    const qreal scale = std::min({1.0, 240.0 / std::max(1, shot.width()), 150.0 / std::max(1, shot.height())});
    const QSize size = (QSizeF(shot.size()) * scale).toSize();
    QPixmap thumb(size);
    thumb.fill(Qt::transparent);
    {
        QPainter p(&thumb);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), 8, 8);
        p.setClipPath(clip);
        p.setOpacity(0.85);
        p.drawPixmap(thumb.rect(), shot);
        p.setOpacity(1);
        p.setClipping(false);
        p.setPen(QPen(m_theme.cursor, 2));
        p.drawRoundedRect(QRectF(1, 1, size.width() - 2, size.height() - 2), 8, 8);
    }

    auto *mime = new QMimeData;
    mime->setData(QLatin1String(PaneMimeType), QByteArray("1"));
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(thumb);
    drag->setHotSpot(QPoint(size.width() / 2, std::min(16, size.height() / 2)));
    g_dragSource = this;
    drag->exec(Qt::MoveAction);
    g_dragSource = nullptr;
    m_headerPress = QPoint(-1, -1);
}
