// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include "app/TabPage.h"

#include <QEvent>
#include <cmath>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include "app/TerminalView.h"

namespace {

QSplitter *newSplitter(Qt::Orientation orientation) {
    auto *splitter = new QSplitter(orientation);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(4);
    splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background: #2A2D35; }"));
    return splitter;
}

}  // namespace

TabPage::TabPage(TerminalView *first, QWidget *parent) : QWidget(parent), m_root(newSplitter(Qt::Horizontal)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_root);
    m_root->addWidget(first);
    adopt(first);
    m_active = first;
    setFocusProxy(first);
}

TabPage::TabPage(const QList<TerminalView *> &views, QWidget *parent)
    : QWidget(parent), m_root(newSplitter(Qt::Vertical)) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_root);
    const int count = int(views.size());
    const int columns = std::max(1, int(std::ceil(std::sqrt(double(count)))));
    for (int start = 0; start < count; start += columns) {
        QSplitter *row = nullptr;
        if (count > 1) {
            row = newSplitter(Qt::Horizontal);
            m_root->addWidget(row);
        }
        for (int i = start; i < std::min(count, start + columns); ++i) {
            (row ? row : m_root)->addWidget(views[i]);
            adopt(views[i]);
        }
    }
    // A single row needs no vertical level.
    if (m_root->count() == 1) {
        if (auto *only = qobject_cast<QSplitter *>(m_root->widget(0))) {
            m_root->setOrientation(Qt::Horizontal);
            while (only->count() > 0) m_root->addWidget(only->widget(0));
            only->deleteLater();
        }
    }
    if (!views.isEmpty()) {
        m_active = views.first();
        setFocusProxy(views.first());
    }
    updateFrames();
}

void TabPage::fanOut(const QByteArray &data) {
    for (TerminalView *v : views()) v->writeInput(data);
}

void TabPage::setBroadcast(bool on) {
    m_broadcast = on;
    for (TerminalView *v : views()) v->setBroadcast(on);
}

void TabPage::adopt(TerminalView *view) {
    view->installEventFilter(this);
    view->setBroadcast(m_broadcast);
    connect(view, &TerminalView::broadcastInput, this, &TabPage::fanOut);
    connect(view, &TerminalView::closeRequested, this, [this, view] { closeView(view); });
    connect(view, &TerminalView::zoomRequested, this, [this, view] { toggleZoom(view); });
}

bool TabPage::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::FocusIn) {
        if (auto *view = qobject_cast<TerminalView *>(watched)) {
            setActive(view);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TabPage::setActive(TerminalView *view) {
    if (m_active == view) return;
    m_active = view;
    setFocusProxy(view);
    updateFrames();
    Q_EMIT activeViewChanged(view);
}

TerminalView *TabPage::activeView() const {
    if (m_active) return m_active;
    const QList<TerminalView *> all = views();
    return all.isEmpty() ? nullptr : all.first();
}

QList<TerminalView *> TabPage::views() const {
    return m_root->findChildren<TerminalView *>();
}

void TabPage::equalize(QSplitter *splitter) {
    const int total = splitter->orientation() == Qt::Horizontal ? splitter->width() : splitter->height();
    QList<int> sizes(splitter->count(), std::max(1, total / std::max(1, splitter->count())));
    splitter->setSizes(sizes);
}

void TabPage::split(TerminalView *target, TerminalView *newView, Qt::Orientation orientation, bool before) {
    if (m_zoomed) toggleZoom(m_zoomed);  // show everything again first
    auto *parent = qobject_cast<QSplitter *>(target ? target->parentWidget() : nullptr);
    if (!parent) {
        parent = m_root;
        target = nullptr;
    }
    adopt(newView);

    if (parent->count() == 1 && parent == m_root) {
        parent->setOrientation(orientation);  // a lone pane can take any direction
    }
    if (parent->orientation() == orientation) {
        const int index = target ? parent->indexOf(target) + (before ? 0 : 1) : parent->count();
        parent->insertWidget(index, newView);
        equalize(parent);
    } else {
        // Replace the pane by a splitter holding the pane and the new one.
        const int index = parent->indexOf(target);
        const QList<int> sizes = parent->sizes();
        QSplitter *nested = newSplitter(orientation);
        parent->replaceWidget(index, nested);
        if (before) {
            nested->addWidget(newView);
            nested->addWidget(target);
        } else {
            nested->addWidget(target);
            nested->addWidget(newView);
        }
        parent->setSizes(sizes);
        QTimer::singleShot(0, nested, [nested] { equalize(nested); });
    }
    updateFrames();
    // Moving the old pane into a new splitter drops its focus, and the new
    // pane cannot take it before it is shown: focus it once laid out.
    QPointer<TerminalView> focusTarget(newView);
    QTimer::singleShot(0, this, [this, focusTarget] {
        if (focusTarget) {
            focusTarget->setFocus(Qt::OtherFocusReason);
            setActive(focusTarget);
        }
    });
}

void TabPage::closeView(TerminalView *view) {
    auto *parent = qobject_cast<QSplitter *>(view->parentWidget());
    if (!parent) return;
    if (m_zoomed) toggleZoom(m_zoomed);

    view->hide();
    view->setParent(nullptr);
    view->deleteLater();

    // A splitter left with one child is replaced by that child.
    if (parent != m_root && parent->count() == 1) {
        auto *grand = qobject_cast<QSplitter *>(parent->parentWidget());
        QWidget *only = parent->widget(0);
        if (grand) {
            const QList<int> sizes = grand->sizes();
            grand->replaceWidget(grand->indexOf(parent), only);
            grand->setSizes(sizes);
            parent->deleteLater();
        }
    }
    // Keep the root useful: if it holds a single splitter, flatten it.
    if (m_root->count() == 1) {
        if (auto *inner = qobject_cast<QSplitter *>(m_root->widget(0))) {
            m_root->setOrientation(inner->orientation());
            while (inner->count() > 0) {
                m_root->addWidget(inner->widget(0));
            }
            inner->deleteLater();
            equalize(m_root);
        }
    }

    const QList<TerminalView *> remaining = views();
    if (remaining.isEmpty()) {
        Q_EMIT empty();
        return;
    }
    if (m_active == view || !m_active) {
        m_active = nullptr;
        QPointer<TerminalView> next(remaining.first());
        setActive(next);
        QTimer::singleShot(0, this, [next] {
            if (next) next->setFocus(Qt::OtherFocusReason);
        });
    }
    updateFrames();
}

void TabPage::focusNeighbor(Qt::Key direction) {
    TerminalView *from = activeView();
    if (!from) return;
    const QRect origin(from->mapTo(this, QPoint(0, 0)), from->size());
    TerminalView *best = nullptr;
    int bestDistance = INT_MAX;
    for (TerminalView *candidate : views()) {
        if (candidate == from) continue;
        const QRect r(candidate->mapTo(this, QPoint(0, 0)), candidate->size());
        bool inDirection = false;
        switch (direction) {
        case Qt::Key_Left: inDirection = r.right() < origin.left(); break;
        case Qt::Key_Right: inDirection = r.left() > origin.right(); break;
        case Qt::Key_Up: inDirection = r.bottom() < origin.top(); break;
        case Qt::Key_Down: inDirection = r.top() > origin.bottom(); break;
        default: break;
        }
        if (!inDirection) continue;
        const QPoint delta = r.center() - origin.center();
        const int distance = delta.manhattanLength();
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    }
    if (best) best->setFocus();
}

void TabPage::updateFrames() {
    const QList<TerminalView *> all = views();
    for (TerminalView *v : all) {
        v->setActiveFrame(all.size() > 1 && v == m_active);
        v->setHeaderVisible(all.size() > 1);
    }
    if (int(all.size()) != m_lastCount) {
        m_lastCount = int(all.size());
        Q_EMIT panesChanged();
    }
}

void TabPage::toggleZoom(TerminalView *view) {
    const bool zoomIn = !m_zoomed && view;
    const QList<TerminalView *> all = views();
    if (zoomIn) {
        // Hide every pane and splitter that is not on the way to this one.
        for (TerminalView *v : all) v->setVisible(v == view);
        for (QSplitter *s : m_root->findChildren<QSplitter *>()) s->setVisible(s->isAncestorOf(view));
        m_zoomed = view;
    } else {
        for (QSplitter *s : m_root->findChildren<QSplitter *>()) s->setVisible(true);
        for (TerminalView *v : all) v->setVisible(true);
        m_zoomed = nullptr;
    }
    for (TerminalView *v : all) v->setZoomed(zoomIn && v == view);
    if (view) view->setFocus();
}
