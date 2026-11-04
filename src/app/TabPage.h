// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Content of one tab: one or more terminals arranged in nested splitters.
// Splitting a pane in the same direction as its splitter adds a sibling;
// in the other direction it nests a new splitter in place of the pane.
//
// The page is also the drop target for drag and drop: panes dragged by their
// title bar (or by their tab) and sessions dragged from the session tree.
// A snap preview shows where they will land before the mouse is released.
#pragma once

#include <QPointer>
#include <QWidget>

class DropPreview;
class QSplitter;
#include "app/TerminalView.h"

class TabPage : public QWidget {
    Q_OBJECT

public:
    explicit TabPage(TerminalView *first, QWidget *parent = nullptr);
    // Several sessions at once, laid out in a near-square grid.
    explicit TabPage(const QList<TerminalView *> &views, QWidget *parent = nullptr);

    TerminalView *activeView() const;
    QList<TerminalView *> views() const;
    int count() const { return int(views().size()); }

    // Puts newView next to target: right of it (Horizontal) or below it (Vertical).
    // before = true puts the new pane left of / above the target.
    void split(TerminalView *target, TerminalView *newView, Qt::Orientation orientation, bool before = false);
    // Puts newView on one side of target, or on one side of the whole tab
    // when target is null.
    void place(TerminalView *newView, TerminalView *target, Qt::Edge edge);
    // Exchanges the places of two panes of this tab.
    void swap(TerminalView *a, TerminalView *b);
    // Takes a pane out of the tab without deleting it (it is moving
    // elsewhere). Emits empty() when the last one goes.
    void release(TerminalView *view);
    // Shows one pane alone (or everything again).
    void toggleZoom(TerminalView *view);
    // Removes and deletes a pane. Emits empty() when the last one goes.
    void closeView(TerminalView *view);
    // Moves the focus to the closest pane in a direction (arrow key).
    void focusNeighbor(Qt::Key direction);
    // What is typed in one pane goes to all panes (MultiExec).
    void setBroadcast(bool on);
    bool broadcast() const { return m_broadcast; }

Q_SIGNALS:
    void activeViewChanged(TerminalView *view);
    void empty();
    void panesChanged();  // a pane was added or closed
    // Sessions dropped from the session tree. target is null for a side of
    // the whole tab.
    void sessionsDropped(TerminalView *target, const QStringList &sessionIds, Qt::Edge edge);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    // Where a drag would land if released now.
    struct DropTarget {
        QPointer<TerminalView> view;  // null: a side of the whole tab
        int edge = -1;                // Qt::Edge, -1 = nowhere
        bool swap = false;            // center of another pane of this tab
        QRect zone;                   // preview, in page coordinates
        QString label;
    };
    DropTarget dropTargetAt(const QPoint &pos, TerminalView *dragged) const;
    void detach(TerminalView *view);  // removes from the splitters, keeps it alive
    void collapse(QSplitter *parent);
    void focusLater(TerminalView *view);
    void adopt(TerminalView *view);
    void fanOut(const QByteArray &data);
    void setActive(TerminalView *view);
    void updateFrames();
    static void equalize(QSplitter *splitter);

    QSplitter *m_root;
    DropPreview *m_preview;
    DropTarget m_drop;
    QPointer<TerminalView> m_active;
    bool m_broadcast = false;
    QPointer<TerminalView> m_zoomed;
    int m_lastCount = 0;
};
