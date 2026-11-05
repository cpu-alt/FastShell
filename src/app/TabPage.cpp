// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/TabPage.h"

#include <QDragEnterEvent>
#include <QEvent>
#include <QMimeData>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>
#include <cmath>

#include "app/DropPreview.h"
#include "app/TerminalView.h"

namespace {

QSplitter *newSplitter(Qt::Orientation orientation) {
    auto *splitter = new QSplitter(orientation);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(5);  // a thin gap between panes (colour from the style sheet)
    return splitter;
}

TabPage *pageOf(QWidget *widget) {
    for (QWidget *w = widget; w; w = w->parentWidget()) {
        if (auto *page = qobject_cast<TabPage *>(w)) return page;
    }
    return nullptr;
}

bool isHorizontal(Qt::Edge edge) {
    return edge == Qt::LeftEdge || edge == Qt::RightEdge;
}

bool isBefore(Qt::Edge edge) {
    return edge == Qt::LeftEdge || edge == Qt::TopEdge;
}

// The part of r that a pane put on that edge would take.
QRect sideOf(const QRect &r, Qt::Edge edge, qreal share) {
    const int w = int(r.width() * share);
    const int h = int(r.height() * share);
    switch (edge) {
    case Qt::LeftEdge: return QRect(r.left(), r.top(), w, r.height());
    case Qt::RightEdge: return QRect(r.right() - w + 1, r.top(), w, r.height());
    case Qt::TopEdge: return QRect(r.left(), r.top(), r.width(), h);
    default: return QRect(r.left(), r.bottom() - h + 1, r.width(), h);
    }
}

// Nearest border of r to pos, measured relative to the size of r.
Qt::Edge nearestEdge(const QRect &r, const QPoint &pos) {
    const qreal fx = qreal(pos.x() - r.left()) / std::max(1, r.width());
    const qreal fy = qreal(pos.y() - r.top()) / std::max(1, r.height());
    const qreal left = fx, right = 1 - fx, top = fy, bottom = 1 - fy;
    const qreal nearest = std::min({left, right, top, bottom});
    return nearest == left ? Qt::LeftEdge : nearest == right ? Qt::RightEdge : nearest == top ? Qt::TopEdge : Qt::BottomEdge;
}

}  // namespace

TabPage::TabPage(TerminalView *first, QWidget *parent)
    : QWidget(parent), m_root(newSplitter(Qt::Horizontal)), m_preview(new DropPreview(this)) {
    setAcceptDrops(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_root);
    m_root->addWidget(first);
    first->show();  // it may come from another tab, hidden on the way
    adopt(first);
    m_active = first;
    setFocusProxy(first);
}

TabPage::TabPage(const QList<TerminalView *> &views, QWidget *parent)
    : QWidget(parent), m_root(newSplitter(Qt::Vertical)), m_preview(new DropPreview(this)) {
    setAcceptDrops(true);

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
    connect(view, &TerminalView::menuRequested, this,
            [this, view](const QPoint &pos, int command) { Q_EMIT paneMenuRequested(view, pos, command); });
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
        newView->show();
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
        newView->show();
        target->show();
        parent->setSizes(sizes);
        QTimer::singleShot(0, nested, [nested] { equalize(nested); });
    }
    updateFrames();
    focusLater(newView);
}

// Moving the old pane into a new splitter drops its focus, and the new
// pane cannot take it before it is shown: focus it once laid out.
void TabPage::focusLater(TerminalView *view) {
    QPointer<TerminalView> focusTarget(view);
    QTimer::singleShot(0, this, [this, focusTarget] {
        if (focusTarget && pageOf(focusTarget) == this) {
            focusTarget->setFocus(Qt::OtherFocusReason);
            setActive(focusTarget);
        }
    });
}

void TabPage::place(TerminalView *newView, TerminalView *target, Qt::Edge edge) {
    const Qt::Orientation orientation = isHorizontal(edge) ? Qt::Horizontal : Qt::Vertical;
    if (target) {
        split(target, newView, orientation, isBefore(edge));
        return;
    }
    // A side of the whole tab.
    if (m_zoomed) toggleZoom(m_zoomed);
    adopt(newView);
    if (m_root->count() <= 1 || m_root->orientation() == orientation) {
        if (m_root->count() <= 1) m_root->setOrientation(orientation);
        m_root->insertWidget(isBefore(edge) ? 0 : m_root->count(), newView);
        newView->show();
        equalize(m_root);
    } else {
        // Everything already there moves into one splitter, next to the new pane.
        const QList<int> sizes = m_root->sizes();
        QSplitter *inner = newSplitter(m_root->orientation());
        m_root->insertWidget(0, inner);
        while (m_root->count() > 1) {
            QWidget *w = m_root->widget(1);
            inner->addWidget(w);
            w->show();
        }
        inner->setSizes(sizes);
        m_root->setOrientation(orientation);
        m_root->insertWidget(isBefore(edge) ? 0 : 1, newView);
        newView->show();
        QTimer::singleShot(0, m_root, [root = m_root] { equalize(root); });
    }
    updateFrames();
    focusLater(newView);
}

void TabPage::swap(TerminalView *a, TerminalView *b) {
    auto *pa = qobject_cast<QSplitter *>(a ? a->parentWidget() : nullptr);
    auto *pb = qobject_cast<QSplitter *>(b ? b->parentWidget() : nullptr);
    if (!pa || !pb || a == b) return;
    if (m_zoomed) toggleZoom(m_zoomed);
    // replaceWidget() keeps the size of the slot: go through a placeholder.
    auto *placeholder = new QWidget;
    pa->replaceWidget(pa->indexOf(a), placeholder);
    pb->replaceWidget(pb->indexOf(b), a);
    pa->replaceWidget(pa->indexOf(placeholder), b);
    delete placeholder;
    a->show();
    b->show();
    updateFrames();
    Q_EMIT panesChanged();
    focusLater(a);
}

void TabPage::detach(TerminalView *view) {
    auto *parent = qobject_cast<QSplitter *>(view->parentWidget());
    if (!parent) return;
    if (m_zoomed) toggleZoom(m_zoomed);
    view->removeEventFilter(this);
    disconnect(view, nullptr, this, nullptr);
    view->setZoomed(false);
    view->hide();
    view->setParent(nullptr);
    collapse(parent);
}

void TabPage::collapse(QSplitter *parent) {
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
    } else if (parent != m_root && parent->count() == 0) {
        parent->deleteLater();
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
}

void TabPage::closeView(TerminalView *view) {
    if (!qobject_cast<QSplitter *>(view->parentWidget())) return;
    detach(view);
    view->deleteLater();
    release(view);  // already out: only updates the focus and the frames
}

void TabPage::release(TerminalView *view) {
    detach(view);
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
    if (TerminalView *next = neighbor(activeView(), direction)) next->setFocus();
}

void TabPage::moveActive(Qt::Key direction) {
    TerminalView *from = activeView();
    if (TerminalView *to = neighbor(from, direction)) swap(from, to);  // focus stays on the moved pane
}

TerminalView *TabPage::neighbor(TerminalView *from, Qt::Key direction) const {
    if (!from) return nullptr;
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
    return best;
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

// --- drag and drop ------------------------------------------------------------------

TabPage::DropTarget TabPage::dropTargetAt(const QPoint &pos, TerminalView *dragged) const {
    DropTarget t;
    const QList<TerminalView *> all = views();
    const bool fromHere = dragged && all.contains(dragged);
    const int others = int(all.size()) - (fromHere ? 1 : 0);

    // Close to a border of the tab: that whole side, when it holds several panes.
    constexpr int outerMargin = 18;
    const QRect page = rect();
    if (others >= 2) {
        const int dl = pos.x() - page.left(), dr = page.right() - pos.x();
        const int dt = pos.y() - page.top(), db = page.bottom() - pos.y();
        const int nearest = std::min({dl, dr, dt, db});
        if (nearest < outerMargin) {
            const Qt::Edge edge = nearest == dl ? Qt::LeftEdge : nearest == dr ? Qt::RightEdge
                                : nearest == dt ? Qt::TopEdge : Qt::BottomEdge;
            const Qt::Orientation orientation = isHorizontal(edge) ? Qt::Horizontal : Qt::Vertical;
            const qreal share = m_root->orientation() == orientation ? 1.0 / (m_root->count() + 1) : 0.5;
            t.edge = edge;
            t.zone = sideOf(page, edge, std::max(share, 0.2));
            t.label = edge == Qt::LeftEdge  ? tr("Whole left side")
                    : edge == Qt::RightEdge ? tr("Whole right side")
                    : edge == Qt::TopEdge   ? tr("Whole top")
                                            : tr("Whole bottom");
            return t;
        }
    }

    // Otherwise the pane under the mouse.
    TerminalView *under = nullptr;
    QRect r;
    for (TerminalView *v : all) {
        if (!v->isVisible()) continue;
        const QRect g(v->mapTo(this, QPoint(0, 0)), v->size());
        if (g.contains(pos)) {
            under = v;
            r = g;
            break;
        }
    }
    if (!under || under == dragged) return t;
    t.view = under;
    const qreal fx = qreal(pos.x() - r.left()) / std::max(1, r.width());
    const qreal fy = qreal(pos.y() - r.top()) / std::max(1, r.height());
    if (fromHere && fx > 0.3 && fx < 0.7 && fy > 0.3 && fy < 0.7) {
        t.swap = true;
        t.edge = Qt::LeftEdge;  // unused, marks the target as valid
        t.zone = r;
        t.label = tr("Swap panes");
        return t;
    }
    const Qt::Edge edge = nearestEdge(r, pos);
    t.edge = edge;
    t.zone = sideOf(r, edge, 0.5);
    t.label = edge == Qt::LeftEdge ? tr("Left") : edge == Qt::RightEdge ? tr("Right") : edge == Qt::TopEdge ? tr("Top") : tr("Bottom");
    return t;
}

namespace {

bool isPaneDrag(const QMimeData *mime) {
    return mime->hasFormat(QLatin1String(PaneMimeType)) && TerminalView::dragSource();
}

bool isSessionDrag(const QMimeData *mime) {
    return mime->hasFormat(QLatin1String(SessionMimeType));
}

}  // namespace

void TabPage::dragEnterEvent(QDragEnterEvent *event) {
    if (!isPaneDrag(event->mimeData()) && !isSessionDrag(event->mimeData())) return;
    dragMoveEvent(event);
    // The enter must be accepted even where nothing can be dropped (over the
    // dragged pane itself), or Qt stops sending the moves that follow.
    const bool droppable = event->isAccepted();
    event->accept();
    if (!droppable) event->setDropAction(Qt::IgnoreAction);
}

void TabPage::dragMoveEvent(QDragMoveEvent *event) {
    const bool pane = isPaneDrag(event->mimeData());
    if (!pane && !isSessionDrag(event->mimeData())) return;
    m_drop = dropTargetAt(event->position().toPoint(), pane ? TerminalView::dragSource() : nullptr);
    if (m_drop.edge < 0) {
        m_preview->hideZone();
        event->ignore();
        return;
    }
    m_preview->showZone(m_drop.zone, m_drop.label);
    // Copy for sessions: the session tree must keep its items.
    event->setDropAction(pane ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
}

void TabPage::dragLeaveEvent(QDragLeaveEvent *) {
    m_drop = DropTarget();
    m_preview->hideZone();
}

void TabPage::dropEvent(QDropEvent *event) {
    m_preview->hideZone();
    const bool pane = isPaneDrag(event->mimeData());
    TerminalView *dragged = pane ? TerminalView::dragSource() : nullptr;
    const DropTarget t = dropTargetAt(event->position().toPoint(), dragged);
    m_drop = DropTarget();
    if (t.edge < 0) return;

    if (!pane) {
        const QString data = QString::fromUtf8(event->mimeData()->data(QLatin1String(SessionMimeType)));
        const QStringList ids = data.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (ids.isEmpty()) return;
        event->setDropAction(Qt::CopyAction);
        event->accept();
        Q_EMIT sessionsDropped(t.view, ids, Qt::Edge(t.edge));
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
    if (t.swap) {
        swap(dragged, t.view);
        return;
    }
    // Out of its tab (which closes if it was alone there), then in here.
    if (TabPage *from = pageOf(dragged)) from->release(dragged);
    place(dragged, t.view, Qt::Edge(t.edge));
    Q_EMIT panesChanged();
}
