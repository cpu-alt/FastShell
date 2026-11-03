// Copyright 2026 EMF Hexa contributors. Apache-2.0.
//
// The EMF Hexa logo: the EMF NET hexagon as a beveled 3D tile holding a
// terminal prompt. Drawn with QPainter so the window icon, the .ico file and
// the About box all come from the same code and stay sharp at any size.
#pragma once

#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>
#include <cmath>

namespace logo {

inline const QColor Navy(0x07, 0x12, 0x1F);
inline const QColor NavyLight(0x17, 0x3A, 0x63);
inline const QColor Blue(0x1E, 0x88, 0xE5);
inline const QColor Cyan(0x4F, 0xC3, 0xF7);

// Pointy-top hexagon centred on (cx, cy) with the given radius.
inline QPainterPath hexagon(qreal cx, qreal cy, qreal r) {
    QPainterPath path;
    for (int i = 0; i < 6; ++i) {
        const qreal angle = (60.0 * i - 90.0) * M_PI / 180.0;
        const QPointF p(cx + r * std::cos(angle), cy + r * std::sin(angle));
        i == 0 ? path.moveTo(p) : path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}

// Draws the logo in a square of the given side, at (0, 0).
inline void paint(QPainter &p, qreal side) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(side / 100.0, side / 100.0);
    const qreal cx = 50, cy = 48;

    // Soft drop shadow under the tile.
    for (int i = 6; i >= 1; --i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 14));
        p.drawPath(hexagon(cx, cy + 3.5, 46 + i * 0.6));
    }

    // Bevel: lit from the top left, dark towards the bottom right.
    QLinearGradient bevel(15, 5, 85, 95);
    bevel.setColorAt(0.0, QColor(0x9B, 0xE4, 0xFF));
    bevel.setColorAt(0.35, Cyan);
    bevel.setColorAt(0.7, Blue);
    bevel.setColorAt(1.0, QColor(0x0B, 0x3D, 0x91));
    p.setBrush(bevel);
    p.drawPath(hexagon(cx, cy, 46));

    // Thin dark groove between bevel and face.
    p.setBrush(QColor(0x05, 0x0C, 0x16));
    p.drawPath(hexagon(cx, cy, 39.5));

    // Recessed face, darker at the bottom.
    QLinearGradient face(0, 12, 0, 86);
    face.setColorAt(0, NavyLight);
    face.setColorAt(1, Navy);
    p.setBrush(face);
    p.drawPath(hexagon(cx, cy + 0.6, 38));

    // Glass highlight on the upper half of the face.
    p.save();
    p.setClipPath(hexagon(cx, cy + 0.6, 38));
    QLinearGradient gloss(0, 10, 0, 50);
    gloss.setColorAt(0, QColor(255, 255, 255, 46));
    gloss.setColorAt(1, QColor(255, 255, 255, 0));
    p.setBrush(gloss);
    p.drawEllipse(QRectF(6, -18, 88, 64));
    p.restore();

    // The two bars of the EMF NET mark, with a faint glow.
    for (const qreal y : {21.0, 75.0}) {
        QPen glow(QColor(0x4F, 0xC3, 0xF7, 70), 5.5);
        glow.setCapStyle(Qt::RoundCap);
        p.setPen(glow);
        p.drawLine(QPointF(41, y), QPointF(59, y));
        QPen bar(Cyan, 2.2);
        bar.setCapStyle(Qt::RoundCap);
        p.setPen(bar);
        p.drawLine(QPointF(41, y), QPointF(59, y));
    }

    // Prompt ">" embossed: shadow, then a white-to-ice gradient stroke.
    const QPolygonF chevron({QPointF(30, 36), QPointF(43, 48), QPointF(30, 60)});
    QPen shadow(QColor(0, 0, 0, 120), 8.5);
    shadow.setCapStyle(Qt::RoundCap);
    shadow.setJoinStyle(Qt::RoundJoin);
    p.setPen(shadow);
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(chevron.translated(0.8, 1.6));
    QLinearGradient ice(0, 34, 0, 62);
    ice.setColorAt(0, Qt::white);
    ice.setColorAt(1, QColor(0xC9, 0xEC, 0xFF));
    QPen stroke(QBrush(ice), 7.5);
    stroke.setCapStyle(Qt::RoundCap);
    stroke.setJoinStyle(Qt::RoundJoin);
    p.setPen(stroke);
    p.drawPolyline(chevron);

    // Block cursor with a glow.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x4F, 0xC3, 0xF7, 60));
    p.drawRoundedRect(QRectF(48, 53, 25, 11), 3, 3);
    QLinearGradient cursor(0, 55, 0, 62);
    cursor.setColorAt(0, QColor(0x9B, 0xE4, 0xFF));
    cursor.setColorAt(1, Blue);
    p.setBrush(cursor);
    p.drawRoundedRect(QRectF(49.5, 54.5, 22, 8), 2, 2);
    p.restore();
}

inline QPixmap pixmap(int side) {
    QPixmap pm(side, side);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paint(p, side);
    return pm;
}

inline QIcon icon() {
    QIcon icon;
    for (int side : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        icon.addPixmap(pixmap(side));
    }
    return icon;
}

}  // namespace logo
