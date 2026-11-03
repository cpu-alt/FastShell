// Copyright 2026 WindTerm NG contributors. Apache-2.0.
//
// Small icons drawn at runtime, so the app needs no image resources.
#pragma once

#include <QFont>
#include <QIcon>
#include <QList>
#include <QPainter>
#include <QPixmap>

#include <cmath>

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

// --- systems ------------------------------------------------------------------------
// Small marks for the system running on a device, chosen per session and shown
// in the session tree and on the tab. Simple shapes in the usual colors.

struct System {
    const char *id;     // saved in the session ("ubuntu")
    const char *label;  // shown in the list (translated with QObject::tr)
};

inline const QList<System> &systems() {
    static const QList<System> list = {
        {"windows-server", "Windows Server"},
        {"windows", "Windows"},
        {"ubuntu", "Ubuntu"},
        {"debian", "Debian"},
        {"redhat", "Red Hat"},
        {"rocky", "CentOS / Rocky / Alma"},
        {"linux", "Linux (other)"},
        {"server", "Server (other)"},
        {"cisco", "Cisco"},
        {"fs", "FS"},
        {"fortinet", "Fortinet"},
        {"mikrotik", "MikroTik"},
        {"switch", "Switch"},
        {"router", "Router"},
        {"firewall", "Firewall"},
    };
    return list;
}

// Draws the mark of system id in a 32x32 box. False if id is unknown.
inline bool paintSystem(QPainter &p, const QString &id) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setPen(Qt::NoPen);
    const QColor dark(0x1B, 0x1D, 0x23);
    auto letters = [&](const QString &text, const QColor &background, const QColor &foreground) {
        p.setBrush(background);
        p.drawRoundedRect(QRectF(2, 2, 28, 28), 6, 6);
        QFont font(QStringLiteral("Segoe UI"));
        font.setBold(true);
        font.setPixelSize(text.size() > 1 ? 14 : 18);
        p.setFont(font);
        p.setPen(foreground);
        p.drawText(QRectF(2, 2, 28, 28), Qt::AlignCenter, text);
    };
    auto rack = [&](const QColor &body, int bays) {
        p.setBrush(body);
        p.drawRoundedRect(QRectF(5, 3, 22, 26), 3, 3);
        for (int i = 0; i < bays; ++i) {
            const qreal y = 7 + i * 7;
            p.setBrush(dark);
            p.drawRoundedRect(QRectF(8, y, 16, 4), 1, 1);
            p.setBrush(QColor(0x98, 0xC3, 0x79));
            p.drawEllipse(QRectF(20.5, y + 1, 2.2, 2.2));
        }
    };
    auto windowsTiles = [&](const QRectF &r, const QColor &color) {
        const qreal gap = r.width() / 14, w = (r.width() - gap) / 2, h = (r.height() - gap) / 2;
        p.setBrush(color);
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 2; ++col)
                p.drawRect(QRectF(r.x() + col * (w + gap), r.y() + row * (h + gap), w, h));
    };

    bool known = true;
    if (id == QLatin1String("windows")) {
        windowsTiles(QRectF(4, 4, 24, 24), QColor(0x00, 0xA4, 0xEF));
    } else if (id == QLatin1String("windows-server")) {
        p.setBrush(QColor(0x3A, 0x41, 0x50));
        p.drawRoundedRect(QRectF(3, 3, 26, 26), 4, 4);
        windowsTiles(QRectF(8, 6, 16, 14), QColor(0x00, 0xA4, 0xEF));
        p.setBrush(dark);
        p.drawRoundedRect(QRectF(7, 23, 18, 3), 1, 1);
    } else if (id == QLatin1String("ubuntu")) {
        p.setBrush(QColor(0xE9, 0x54, 0x20));
        p.drawEllipse(QRectF(2, 2, 28, 28));
        p.setPen(QPen(Qt::white, 2.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(16, 16), 7.5, 7.5);
        p.setPen(QPen(QColor(0xE9, 0x54, 0x20), 1.6));
        p.setBrush(Qt::white);
        for (const qreal angle : {0.0, 120.0, 240.0}) {
            const qreal rad = angle * 3.14159265 / 180.0;
            p.drawEllipse(QPointF(16 + 9 * std::cos(rad), 16 + 9 * std::sin(rad)), 3.4, 3.4);
        }
    } else if (id == QLatin1String("debian")) {
        QPen pen(QColor(0xD7, 0x0A, 0x53), 3.2, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(4, 4, 24, 24), 60 * 16, 290 * 16);    // a swirl: two open arcs
        p.drawArc(QRectF(10, 10, 13, 13), 200 * 16, 250 * 16);
    } else if (id == QLatin1String("redhat")) {
        p.setBrush(QColor(0xEE, 0x00, 0x00));
        p.drawEllipse(QRectF(2, 17, 28, 9));                   // brim
        p.drawRoundedRect(QRectF(8, 7, 16, 15), 7, 7);         // crown
        p.setBrush(dark);
        p.drawRect(QRectF(8, 17, 16, 2.4));                    // band
    } else if (id == QLatin1String("rocky")) {
        p.setBrush(QColor(0x10, 0xB9, 0x81));
        p.drawEllipse(QRectF(2, 2, 28, 28));
        p.setBrush(Qt::white);
        p.drawPolygon(QPolygonF({QPointF(6, 23), QPointF(13, 12), QPointF(17, 18), QPointF(20, 14), QPointF(26, 23)}));
    } else if (id == QLatin1String("linux")) {
        p.setBrush(QColor(0x20, 0x20, 0x24));
        p.drawEllipse(QRectF(7, 3, 18, 26));                   // body
        p.setBrush(QColor(0xF2, 0xF2, 0xF2));
        p.drawEllipse(QRectF(10, 12, 12, 15));                 // belly
        p.drawEllipse(QRectF(11, 7, 4, 4));                    // eyes
        p.drawEllipse(QRectF(17, 7, 4, 4));
        p.setBrush(QColor(0xF5, 0xB8, 0x00));                  // beak and feet
        p.drawPolygon(QPolygonF({QPointF(13, 11.5), QPointF(19, 11.5), QPointF(16, 15)}));
        p.drawEllipse(QRectF(6, 25, 9, 5));
        p.drawEllipse(QRectF(17, 25, 9, 5));
    } else if (id == QLatin1String("server")) {
        rack(QColor(0x5C, 0x63, 0x70), 3);
    } else if (id == QLatin1String("cisco")) {
        p.setBrush(QColor(0x04, 0x9F, 0xD9));  // bars of growing height, like a bridge
        const qreal heights[] = {6, 12, 18, 12, 6, 12, 18, 12, 6};
        for (int i = 0; i < 9; ++i) {
            const qreal h = heights[i];
            p.drawRoundedRect(QRectF(2.5 + i * 3.1, 16 - h / 2, 2, h), 1, 1);
        }
    } else if (id == QLatin1String("fs")) {
        letters(QStringLiteral("FS"), QColor(0xC8, 0x10, 0x2E), Qt::white);
    } else if (id == QLatin1String("fortinet")) {
        p.setBrush(QColor(0xEE, 0x31, 0x24));
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                if (!(row == 1 && col == 1)) p.drawRoundedRect(QRectF(3 + col * 9.3, 3 + row * 9.3, 7.6, 7.6), 1.5, 1.5);
    } else if (id == QLatin1String("mikrotik")) {
        letters(QStringLiteral("M"), QColor(0x29, 0x32, 0x39), QColor(0xE6, 0xE8, 0xEB));
    } else if (id == QLatin1String("switch")) {
        p.setBrush(QColor(0x4A, 0x6F, 0xA5));
        p.drawRoundedRect(QRectF(2, 9, 28, 14), 3, 3);
        p.setBrush(dark);
        for (int i = 0; i < 5; ++i) p.drawRect(QRectF(5 + i * 5, 14, 3.4, 4));
        p.setBrush(QColor(0x98, 0xC3, 0x79));
        p.drawEllipse(QRectF(5, 11, 2, 2));
    } else if (id == QLatin1String("router")) {
        p.setBrush(QColor(0x2D, 0x7D, 0xD2));
        p.drawEllipse(QRectF(2, 2, 28, 28));
        QPen pen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.drawLine(QPointF(9, 16), QPointF(23, 16));  // four arrows, in and out
        p.drawLine(QPointF(16, 9), QPointF(16, 23));
        p.drawPolyline(QPolygonF({QPointF(20, 13), QPointF(23, 16), QPointF(20, 19)}));
        p.drawPolyline(QPolygonF({QPointF(12, 13), QPointF(9, 16), QPointF(12, 19)}));
        p.drawPolyline(QPolygonF({QPointF(13, 12), QPointF(16, 9), QPointF(19, 12)}));
        p.drawPolyline(QPolygonF({QPointF(13, 20), QPointF(16, 23), QPointF(19, 20)}));
    } else if (id == QLatin1String("firewall")) {
        p.setBrush(QColor(0xD9, 0x5D, 0x39));  // a brick wall
        for (int row = 0; row < 4; ++row) {
            const qreal offset = row % 2 ? -4.5 : 0;
            for (int col = 0; col < 4; ++col) {
                const QRectF brick(QRectF(3 + offset + col * 9, 4 + row * 6.3, 8, 5.3).intersected(QRectF(3, 0, 26, 32)));
                if (brick.width() > 1) p.drawRect(brick);
            }
        }
    } else {
        known = false;
    }
    p.restore();
    return known;
}

inline QIcon system(const QString &id) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    if (!paintSystem(p, id)) return QIcon();
    p.end();
    return QIcon(pixmap);
}

// The session's system mark when it has one (with a small dot of the
// session color), else: round dot for SSH, square for Telnet, ">_" box for
// local shells, a screen for RDP.
inline QIcon session(const SessionInfo &s) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = sessionColor(s);
    if (!s.os.isEmpty() && paintSystem(p, s.os)) {
        p.setPen(QPen(QColor(0x13, 0x15, 0x1A), 2));
        p.setBrush(c);
        p.drawEllipse(QRectF(21, 21, 10, 10));
        p.end();
        return QIcon(pixmap);
    }
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
