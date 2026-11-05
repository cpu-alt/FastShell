// Copyright 2026 FastShell contributors. Apache-2.0.
//
// One terminal tab: owns the screen model and the backend, renders the grid
// and turns keyboard / mouse input into bytes for the backend.
#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QFont>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

#include "app/Theme.h"
#include "core/Backend.h"
#include "terminal/Screen.h"

class QPainter;
class ScrollBar;

// Drag-and-drop format for sessions dragged out of the session tree (ids, one per line).
inline constexpr char SessionMimeType[] = "application/x-emfhexa-sessions";
// Drag-and-drop format for a pane dragged by its title bar or its tab
// (the pane itself is TerminalView::dragSource()).
inline constexpr char PaneMimeType[] = "application/x-emfhexa-pane";

class TerminalView : public QWidget {
    Q_OBJECT

public:
    using BackendFactory = std::function<Backend *(QObject *parent)>;

    TerminalView(BackendFactory factory, QWidget *parent = nullptr);
    ~TerminalView() override;

    void start();
    void copySelection();
    void paste();
    void setTerminalFont(const QFont &font);
    QFont terminalFont() const { return m_font; }
    void zoom(int delta);
    QString title() const { return m_title; }
    Backend *backend() const { return m_backend; }
    bool isRunning() const { return m_backend && !m_finished; }

    struct Command {
        std::int64_t line = 0;
        QString text;
        QDateTime time;
    };
    const QList<Command> &commands() const { return m_commands; }
    // What a command printed (a whole "show running-config"...), from the line
    // after it to the next prompt, with pager prompts ("--More--") removed.
    // index -1 = the last command. Empty if there is none.
    QString commandOutput(int index = -1) const;
    // Index of the command whose output (or command line) shows at a point
    // of the widget, or -1.
    int commandAt(const QPointF &point) const;
    void scrollToLine(std::int64_t line);
    void sendText(const QString &text);  // as if typed
    QPoint cursorPosition() const { return {m_screen->cursorColumn() + 1, int(m_screen->cursorRow() + 1)}; }
    QSize terminalSize() const { return {m_screen->columns(), m_screen->rows()}; }

    void setShowTimestamps(bool show);
    void setShowLineNumbers(bool show);
    void setBackspaceSendsCtrlH(bool on) { m_backspaceCtrlH = on; }
    // Answers pager prompts (--More--) with a space, so long outputs come whole.
    void setAutoMore(bool on) { m_autoMore = on; }
    bool autoMore() const { return m_autoMore; }
    // Broadcast mode: typed input is emitted instead of sent (TabPage fans it out).
    void setBroadcast(bool on) {
        m_broadcast = on;
        update();
    }
    bool broadcast() const { return m_broadcast; }
    void writeInput(const QByteArray &data);  // sends input as if typed here

    // Title bar shown when the tab holds several panes.
    void setPaneInfo(const QString &title, const QColor &color);
    QString paneTitle() const { return m_paneTitle; }
    void setHeaderVisible(bool visible);
    void setZoomed(bool zoomed);
    int headerHeight() const;
    bool backspaceSendsCtrlH() const { return m_backspaceCtrlH; }
    // Starts dragging this pane (to split it next to another one). Returns
    // when the mouse is released.
    void startPaneDrag();
    // The pane being dragged, or null.
    static TerminalView *dragSource();
    // Accent line marking the focused pane when a tab is split.
    void setActiveFrame(bool active) {
        if (m_activeFrame != active) {
            m_activeFrame = active;
            update();
        }
    }

Q_SIGNALS:
    void titleChanged(const QString &title);
    void commandsChanged();
    void connected();
    void broadcastInput(const QByteArray &data);
    void closeRequested();
    void zoomRequested();
    // Right click on the title bar, or Shift+right click in the terminal
    // (a plain right click pastes). Global position.
    void menuRequested(const QPoint &globalPos, int commandIndex);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private:
    struct Position {
        std::int64_t line = 0;
        int column = 0;
        bool operator<(const Position &o) const { return line < o.line || (line == o.line && column < o.column); }
        bool operator==(const Position &o) const = default;
    };

    void onReadyRead();
    void processPending();
    void onBackendFinished(const QString &reason, bool isError);
    void send(const QByteArray &data);
    void userSend(const QByteArray &data);  // keyboard, paste: may be broadcast
    void scheduleRender();
    void updateGeometryFromFont();
    void updateScrollBar();
    void scrollToBottom();
    void scrollBy(int lines);
    Position positionAt(const QPointF &point) const;
    bool isSelected(std::int64_t line, int column) const;
    void clearSelection();
    bool reportMouse(QMouseEvent *event, int button, bool press, bool motion);
    void message(const QString &text, const char *sgr);
    void recordCommand();
    void captureCommand(std::int64_t line);
    void updateGutter();
    qreal textLeft() const { return m_padding + m_gutterWidth; }
    qreal textTop() const { return m_padding + headerHeight(); }
    void paintHeader(QPainter &painter);
    void paintEdge(QPainter &painter);

    BackendFactory m_factory;
    QPointer<Backend> m_backend;
    std::unique_ptr<Screen> m_screen;
    ScrollBar *m_scrollBar;
    Theme m_theme;

    QFont m_font;
    QFont m_boldFont;
    QFont m_italicFont;
    qreal m_cellWidth = 8;
    qreal m_cellHeight = 16;
    qreal m_ascent = 12;
    int m_padding = 4;

    QByteArray m_pending;      // received but not parsed yet (from m_pendingOffset)
    qsizetype m_pendingOffset = 0;  // parsed part of m_pending, dropped once all is parsed
    QTimer m_renderTimer;      // coalesces repaints to the display rate
    QTimer m_resizeTimer;      // debounces size changes sent to the backend
    std::uint64_t m_paintedRevision = ~0ull;
    std::int64_t m_scrollOffset = 0;  // lines scrolled back from the bottom
    std::int64_t m_lastHistorySize = 0;

    bool m_selecting = false;
    bool m_hasSelection = false;
    Position m_selectionAnchor;
    Position m_selectionHead;

    QString m_title;
    bool m_finished = false;
    int m_lastMouseButton = -1;

    QList<Command> m_commands;
    std::int64_t m_highlightLine = -1;
    bool m_showTimestamps = true;
    bool m_showLineNumbers = true;
    qreal m_gutterWidth = 0;
    bool m_receivedData = false;
    bool m_activeFrame = false;
    bool m_backspaceCtrlH = false;
    bool m_autoMore = true;
    std::int64_t m_pagerLine = -1;       // pager prompt already answered...
    std::uint64_t m_pagerRevision = 0;   // ...at this screen state
    void answerPager();
    bool m_broadcast = false;
    bool m_headerVisible = false;
    bool m_zoomed = false;
    QString m_paneTitle;
    QColor m_paneColor;
    QRect m_closeRect;
    QRect m_zoomRect;
    QPoint m_headerPress{-1, -1};  // where the title bar was pressed, to start a drag
};
