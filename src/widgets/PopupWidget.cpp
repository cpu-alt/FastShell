// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
//
// Qt 6 port: QDesktopWidget and the private qFadeEffect are gone, replaced
// by QScreen and a QPropertyAnimation. Positions now take the screen origin
// into account, so the popup lands on the right monitor.
#include "widgets/PopupWidget.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QScreen>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int TriangleHeight = 18;
}

PopupWidget::PopupWidget(QWidget *parent) : QFrame(parent) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::X11BypassWindowManagerHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_X11DoNotAcceptFocus, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setFocusPolicy(Qt::NoFocus);
    setFrameShape(QFrame::StyledPanel);
    setMouseTracking(true);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(0);
}

PopupWidget::~PopupWidget() {
    qApp->removeEventFilter(this);
    if (auto *button = qobject_cast<QAbstractButton *>(parent())) {
        if (auto *toolButton = qobject_cast<QToolButton *>(button)) {
            if (QAction *action = toolButton->defaultAction()) {
                action->setChecked(false);
            }
        }
        button->setChecked(false);
    }
}

void PopupWidget::setCentralWidget(QWidget *widget) {
    if (widget->layout()) {
        widget->layout()->setContentsMargins(QMargins());
    }
    widget->setParent(this);
    layout()->addWidget(widget);
    adjustSize();
}

bool PopupWidget::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::FocusOut:
        if (auto *widget = qobject_cast<QWidget *>(watched)) {
            if (!isAncestorOf(widget) && !QApplication::activePopupWidget()) {
                bool inMenu = false;
                for (QWidget *w = widget; w; w = w->parentWidget()) {
                    if (qobject_cast<QMenu *>(w)) {
                        inMenu = true;
                        break;
                    }
                }
                if (!inMenu) {
                    close();
                }
            }
        }
        break;
    case QEvent::KeyPress: {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        QWidget *activePopup = QApplication::activePopupWidget();
        if (keyEvent->matches(QKeySequence::Cancel)) {
            activePopup ? activePopup->close() : close();
            return true;
        }
        if (!isAncestorOf(QApplication::focusWidget()) && !activePopup) {
            close();
        }
        break;
    }
    case QEvent::NonClientAreaMouseButtonPress:
    case QEvent::WindowStateChange:
        close();
        break;
    case QEvent::MouseButtonPress: {
        const QPoint globalPos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        if (rect().contains(mapFromGlobal(globalPos))) {
            break;
        }
        if (auto *button = qobject_cast<QAbstractButton *>(parentWidget())) {
            if (button->rect().contains(button->mapFromGlobal(globalPos))) {
                break;
            }
        }
        setAttribute(Qt::WA_NoMouseReplay);
        close();
        break;
    }
    case QEvent::WindowDeactivate:
        if (QWidget *p = parentWidget(); p && !p->isAncestorOf(QApplication::focusWidget())) {
            close();
        }
        break;
    default:
        break;
    }
    return false;
}

void PopupWidget::mousePressEvent(QMouseEvent *event) {
    setAttribute(Qt::WA_NoMouseReplay);
    QFrame::mousePressEvent(event);
}

void PopupWidget::show(Area horizontal, Area vertical) {
    QWidget *anchor = parentWidget();
    Q_ASSERT(anchor);
    if (!anchor) {
        return;
    }

    const QPoint pos = anchor->mapToGlobal(QPoint());
    const QRect anchorRect = anchor->rect();
    QScreen *screen = anchor->screen() ? anchor->screen() : QGuiApplication::primaryScreen();
    const QRect screenRect = screen->availableGeometry();

    const int top = style()->pixelMetric(QStyle::PM_LayoutTopMargin);
    const int bottom = style()->pixelMetric(QStyle::PM_LayoutBottomMargin);
    const int left = style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
    const int right = style()->pixelMetric(QStyle::PM_LayoutRightMargin);
    const int triangle = std::max(TriangleHeight, top);

    // Flip to the side that has room.
    if (vertical == TopArea && pos.y() - screenRect.top() < height()) {
        vertical = BottomArea;
    } else if (vertical == BottomArea && screenRect.bottom() - (pos.y() + anchorRect.height()) < height()) {
        vertical = TopArea;
    }

    QPoint newPos;
    if (horizontal == MiddleArea) {
        newPos.setX(pos.x() + (anchorRect.width() - width()) / 2);
    } else {
        if (horizontal == LeftArea && pos.x() + anchorRect.width() - width() < screenRect.left()) {
            horizontal = RightArea;
        } else if (horizontal == RightArea && pos.x() + width() > screenRect.right()) {
            horizontal = LeftArea;
        }
        newPos.setX(horizontal == LeftArea ? pos.x() + anchorRect.width() - width() : pos.x());
    }
    newPos.setX(qBound(screenRect.left(), newPos.x(), screenRect.right() - width()));
    newPos.setY(vertical == BottomArea ? pos.y() + anchorRect.height() : pos.y() - height() - triangle);

    const int xCenter = pos.x() + anchorRect.width() / 2 - newPos.x();
    QPolygon arrow;
    QRect body;
    if (vertical == TopArea) {
        arrow << QPoint(xCenter - triangle, height()) << QPoint(xCenter, height() + triangle)
              << QPoint(xCenter + triangle, height());
        body = QRect(0, 0, width(), height());
        layout()->setContentsMargins(left, top, right, bottom + triangle * 2);
    } else {
        arrow << QPoint(xCenter - triangle, triangle) << QPoint(xCenter, 0) << QPoint(xCenter + triangle, triangle);
        body = QRect(0, triangle, width(), height());
        layout()->setContentsMargins(left, top + triangle, right, bottom);
    }
    setMask(QRegion(body).united(QRegion(arrow)));

    move(newPos);
    setWindowOpacity(0.0);
    QWidget::show();

    auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(200);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);

    qApp->installEventFilter(this);
}
