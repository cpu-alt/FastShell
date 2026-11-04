// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
//
// Changes from the original: sizes are set directly with setSizes() instead
// of moveSplitter() offsets, and the saved ratio is stored on the pane widget
// rather than on a handle whose index shifts around the main pane.
#include "widgets/Splitter.h"

#include <QEvent>
#include <numeric>

namespace {
constexpr char RatioProperty[] = "fs_ratio";
constexpr double DefaultRatio = 0.18;
}  // namespace

Splitter::Splitter(QWidget *parent) : QSplitter(parent) {}
Splitter::Splitter(Qt::Orientation orientation, QWidget *parent) : QSplitter(orientation, parent) {}

QSplitterHandle *Splitter::createHandle() {
    QSplitterHandle *handle = QSplitter::createHandle();
    handle->installEventFilter(this);
    return handle;
}

bool Splitter::isIndexVisible(int index) const {
    if (index < 0 || index >= count()) {
        return false;
    }
    return sizes().at(index) > 0;
}

void Splitter::setIndexVisible(int index, bool visible) {
    visible ? showIndex(index) : hideIndex(index);
}

void Splitter::showIndex(int index) {
    if (index < 0 || index >= count() || isIndexVisible(index)) {
        return;
    }
    Q_EMIT showIndexRequested(index);

    QList<int> list = sizes();
    const int total = std::accumulate(list.begin(), list.end(), 0);
    double ratio = widget(index)->property(RatioProperty).toDouble();
    if (ratio <= 0.0 || ratio >= 1.0) {
        ratio = DefaultRatio;
    }
    const int size = int(total * ratio);
    const int mainIndex = qBound(0, m_mainIndex, count() - 1);
    list[index] = size;
    list[mainIndex] = std::max(0, list[mainIndex] - size);
    setSizes(list);
}

void Splitter::hideIndex(int index) {
    if (index < 0 || index >= count() || !isIndexVisible(index)) {
        return;
    }
    Q_EMIT hideIndexRequested(index);

    QList<int> list = sizes();
    const int total = std::accumulate(list.begin(), list.end(), 0);
    if (total > 0) {
        widget(index)->setProperty(RatioProperty, double(list[index]) / total);
    }
    const int mainIndex = qBound(0, m_mainIndex, count() - 1);
    list[mainIndex] += list[index];
    list[index] = 0;
    setSizes(list);
}

bool Splitter::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        for (int i = 1; i < count(); ++i) {
            if (handle(i) == watched) {
                // Handle i sits between widget i-1 and i; toggle the side pane.
                const int pane = (i <= m_mainIndex) ? i - 1 : i;
                setIndexVisible(pane, !isIndexVisible(pane));
                return true;
            }
        }
    }
    return QSplitter::eventFilter(watched, event);
}
