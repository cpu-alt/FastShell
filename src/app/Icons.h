// Copyright 2026 WindTerm NG contributors. Apache-2.0.
//
// Small icons drawn at runtime, so the app needs no image resources.
#pragma once

#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include "app/SessionStore.h"

namespace icons {

inline QColor defaultColor(SessionInfo::Type type) {
    switch (type) {
    case SessionInfo::Type::Local: return QColor(0x56, 0xB6, 0xC2);
    case SessionInfo::Type::Telnet: return QColor(0x61, 0xAF, 0xEF);
    case SessionInfo::Type::Serial: return QColor(0xE5, 0xC0, 0x7B);
    case SessionInfo::Type::Rdp: return QColor(0x98, 0xC3, 0x79);
    default: return QColor(0xE8, 0x3E, 0x8C);
    }
}

inline QColor sessionColor(const SessionInfo &s) {
    return s.color.isValid() ? s.color : defaultColor(s.type);
}

// Round dot for SSH, square for Telnet, ">_" box for local shells, a screen for RDP.
inline QIcon session(const SessionInfo &s) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = sessionColor(s);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    switch (s.type) {
    case SessionInfo::Type::Ssh:
        p.drawEllipse(QRectF(6, 6, 20, 20));
        break;
    case SessionInfo::Type::Telnet:
        p.drawRoundedRect(QRectF(6, 6, 20, 20), 3, 3);
        break;
    case SessionInfo::Type::Serial:  // a diamond, like a console connector
        p.drawPolygon(QPolygonF({QPointF(16, 4), QPointF(28, 16), QPointF(16, 28), QPointF(4, 16)}));
        break;
    case SessionInfo::Type::Local: {
        p.drawRoundedRect(QRectF(2, 5, 28, 22), 4, 4);
        QPen pen(QColor(0x1B, 0x1D, 0x23), 3);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawPolyline(QPolygonF({QPointF(8, 11), QPointF(13, 16), QPointF(8, 21)}));
        p.drawLine(QPointF(16, 21), QPointF(23, 21));
        break;
    }
    case SessionInfo::Type::Rdp:  // a monitor on its stand
        p.drawRoundedRect(QRectF(3, 5, 26, 17), 3, 3);
        p.drawRect(QRectF(14, 22, 4, 4));
        p.drawRoundedRect(QRectF(9, 25, 14, 3), 1.5, 1.5);
        p.setBrush(QColor(0x1B, 0x1D, 0x23));
        p.drawRoundedRect(QRectF(6, 8, 20, 11), 1.5, 1.5);
        break;
    }
    return QIcon(pixmap);
}

inline QIcon folder(bool open = false, QColor color = QColor()) {
    if (!color.isValid()) color = QColor(0xE5, 0xB5, 0x4B);
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color.darker(115));
    p.drawRoundedRect(QRectF(3, 7, 12, 6), 2, 2);
    p.setBrush(open ? color.lighter(112) : color);
    p.drawRoundedRect(QRectF(3, 10, 26, 17), 2, 2);
    return QIcon(pixmap);
}

inline QIcon file() {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0xAB, 0xB2, 0xBF), 2));
    p.setBrush(QColor(0x2A, 0x2D, 0x35));
    p.drawPolygon(QPolygonF({QPointF(8, 4), QPointF(20, 4), QPointF(26, 10), QPointF(26, 28), QPointF(8, 28)}));
    return QIcon(pixmap);
}

}  // namespace icons
