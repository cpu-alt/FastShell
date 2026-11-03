// Copyright 2026 WindTerm NG contributors. Apache-2.0.
//
// Content of one tab: one or more terminals arranged in nested splitters.
// Splitting a pane in the same direction as its splitter adds a sibling;
// in the other direction it nests a new splitter in place of the pane.
#pragma once

#include <QPointer>
#include <QWidget>

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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void adopt(TerminalView *view);
    void fanOut(const QByteArray &data);
    void setActive(TerminalView *view);
    void updateFrames();
    static void equalize(QSplitter *splitter);

    QSplitter *m_root;
    QPointer<TerminalView> m_active;
    bool m_broadcast = false;
    QPointer<TerminalView> m_zoomed;
    int m_lastCount = 0;
};
