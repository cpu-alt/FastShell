// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/Fun.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QSettings>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <vector>

#include "app/Style.h"

namespace fun {

namespace {

const QString SettingKey = QStringLiteral("appearance/animations");

qreal clamp01(qreal v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
qreal easeOut(qreal t) { return 1 - (1 - t) * (1 - t) * (1 - t); }

// A transparent child that covers its parent, ignores the mouse and follows
// the parent's size.
class Overlay : public QWidget {
public:
    explicit Overlay(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(parent->rect());
        parent->installEventFilter(this);
        show();
        raise();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == parentWidget() && event->type() == QEvent::Resize) setGeometry(parentWidget()->rect());
        return false;
    }
};

class UnlockOverlay : public Overlay {
public:
    UnlockOverlay(QWidget *parent, const QString &text) : Overlay(parent), m_text(text) {
        auto *anim = new QVariantAnimation(this);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setDuration(1400);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_t = v.toReal();
            update();
        });
        connect(anim, &QVariantAnimation::finished, this, &QObject::deleteLater);
        anim->start();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        const style::ColorTheme &theme = style::current();
        // Pop in (0-0.15), open the shackle (0.2-0.45), sparkle, fade out (0.7-1).
        const qreal pop = easeOut(clamp01(m_t / 0.15));
        const qreal open = easeOut(clamp01((m_t - 0.2) / 0.25));
        const qreal fade = 1 - clamp01((m_t - 0.7) / 0.3);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(fade);

        const QPointF c(width() / 2.0, height() / 2.0);
        const qreal s = 0.85 + 0.15 * pop;  // scale
        // Card behind the lock.
        const QRectF card(c.x() - 110 * s, c.y() - 80 * s, 220 * s, 160 * s);
        QColor bg = theme.surface;
        bg.setAlphaF(0.92);
        p.setPen(QPen(theme.accent, 1.5));
        p.setBrush(bg);
        p.drawRoundedRect(card, 14, 14);

        // Padlock: body and a shackle that lifts and swings open.
        const QRectF body(c.x() - 26 * s, c.y() - 22 * s, 52 * s, 40 * s);
        p.save();
        const QPointF hinge(body.right() - 9 * s, body.top());
        p.translate(hinge.x(), hinge.y() - 10 * s * open);
        p.rotate(30 * open);  // the free leg swings up and out
        p.setPen(QPen(theme.text, 6 * s, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        QPainterPath shackle;
        shackle.moveTo(0, 4 * s);
        shackle.lineTo(0, -14 * s);
        shackle.arcTo(QRectF(-34 * s, -31 * s, 34 * s, 34 * s), 0, 180);
        shackle.lineTo(-34 * s, 4 * s);
        p.drawPath(shackle);
        p.restore();
        p.setPen(Qt::NoPen);
        p.setBrush(theme.accent);
        p.drawRoundedRect(body, 8 * s, 8 * s);
        // Keyhole.
        p.setBrush(theme.surface);
        p.drawEllipse(QPointF(c.x(), body.center().y() - 4 * s), 5 * s, 5 * s);
        p.drawRect(QRectF(c.x() - 2 * s, body.center().y() - 2 * s, 4 * s, 11 * s));

        // Sparkles once open.
        if (open > 0.6) {
            const qreal k = clamp01((m_t - 0.38) / 0.3);
            p.setPen(QPen(theme.accent, 2, Qt::SolidLine, Qt::RoundCap));
            for (int i = 0; i < 5; ++i) {  // upper half only, the name is below
                const qreal a = std::numbers::pi + i * std::numbers::pi / 4;
                const qreal r1 = (40 + 26 * k) * s;
                const qreal r2 = r1 + 8 * (1 - k) * s;
                const QPointF d(std::cos(a), std::sin(a));
                p.drawLine(c + d * r1 - QPointF(0, 2 * s), c + d * r2 - QPointF(0, 2 * s));
            }
        }

        // Account name.
        QFont f = font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 1.1);
        p.setFont(f);
        p.setPen(theme.strong);
        p.drawText(QRectF(card.left(), body.bottom() + 8 * s, card.width(), 24 * s), Qt::AlignCenter,
                   p.fontMetrics().elidedText(m_text, Qt::ElideRight, int(card.width() - 20)));
    }

private:
    QString m_text;
    qreal m_t = 0;
};

class ConfettiOverlay : public Overlay {
public:
    explicit ConfettiOverlay(QWidget *parent) : Overlay(parent) {
        const style::ColorTheme &theme = style::current();
        const QColor colors[] = {theme.accent, QColor(0xFF, 0xC8, 0x3D), QColor(0xFF, 0x6B, 0x8B),
                                 QColor(0x4E, 0xD8, 0x9B), QColor(0x7A, 0xA2, 0xFF), QColor(0xC0, 0x84, 0xFC)};
        QRandomGenerator *rng = QRandomGenerator::global();
        const qreal w = width();
        // Two bursts, from the bottom corners, aimed up and inwards.
        for (int i = 0; i < 120; ++i) {
            Piece piece;
            const bool left = i % 2 == 0;
            piece.x = left ? w * 0.1 : w * 0.9;
            piece.y = height() + 10;
            const qreal angle = (left ? -60 : -120) + rng->bounded(40.0) - 20;
            const qreal speed = 650 + rng->bounded(500.0);
            piece.vx = speed * std::cos(angle * std::numbers::pi / 180);
            piece.vy = speed * std::sin(angle * std::numbers::pi / 180);
            piece.spin = rng->bounded(720.0) - 360;
            piece.angle = rng->bounded(360.0);
            piece.size = 5 + rng->bounded(5.0);
            piece.color = colors[rng->bounded(int(std::size(colors)))];
            piece.round = rng->bounded(4) == 0;
            m_pieces.push_back(piece);
        }
        m_clock.start();
        m_timer.setInterval(16);
        connect(&m_timer, &QTimer::timeout, this, [this] { step(); });
        m_timer.start();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        const qreal fade = 1 - clamp01((m_age - 1.6) / 0.6);
        p.setOpacity(fade);
        for (const Piece &piece : m_pieces) {
            p.save();
            p.translate(piece.x, piece.y);
            p.rotate(piece.angle);
            p.setBrush(piece.color);
            // A flat piece turning in the air: its width follows the spin.
            const qreal flip = std::abs(std::cos(piece.angle * std::numbers::pi / 90));
            if (piece.round) p.drawEllipse(QPointF(0, 0), piece.size / 2, piece.size / 2);
            else p.drawRect(QRectF(-piece.size / 2 * flip, -piece.size / 3, piece.size * flip, piece.size / 1.5));
            p.restore();
        }
    }

private:
    struct Piece {
        qreal x = 0, y = 0, vx = 0, vy = 0, angle = 0, spin = 0, size = 6;
        QColor color;
        bool round = false;
    };

    void step() {
        const qreal now = m_clock.elapsed() / 1000.0;
        const qreal dt = std::min(now - m_age, 0.05);
        m_age = now;
        for (Piece &piece : m_pieces) {
            piece.vy += 900 * dt;              // gravity
            piece.vx *= 1 - 1.2 * dt;          // air
            piece.vy = std::min(piece.vy, 260.0 + piece.size * 10);  // flutter down slowly
            piece.x += piece.vx * dt;
            piece.y += piece.vy * dt;
            piece.angle += piece.spin * dt;
        }
        update();
        if (m_age > 2.2) {
            m_timer.stop();
            deleteLater();
        }
    }

    std::vector<Piece> m_pieces;
    QElapsedTimer m_clock;
    QTimer m_timer;
    qreal m_age = 0;
};

}  // namespace

bool enabled() {
    return QSettings().value(SettingKey, true).toBool();
}

void setEnabled(bool on) {
    QSettings().setValue(SettingKey, on);
}

void unlock(QWidget *widget, const QString &text) {
    if (!widget || !enabled()) return;
    new UnlockOverlay(widget, text);
}

void confetti(QWidget *widget) {
    if (!widget || !enabled()) return;
    new ConfettiOverlay(widget);
}

void paintVaultDoor(QPainter &p, const QRectF &rect, qreal dialTurn) {
    const style::ColorTheme &theme = style::current();
    const qreal side = std::min(rect.width(), rect.height());
    const QRectF box(rect.center().x() - side / 2, rect.center().y() - side / 2, side, side);
    const QPointF c = box.center();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    // The safe: a square body with feet, a round door with hinges and bolts.
    QColor body = theme.surface;
    QColor edge = theme.border.lighter(theme.dark ? 160 : 100);
    p.setPen(QPen(edge, side * 0.02));
    p.setBrush(body);
    p.drawRoundedRect(box.adjusted(side * 0.06, side * 0.04, -side * 0.06, -side * 0.1), side * 0.07, side * 0.07);
    p.setBrush(edge);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(box.left() + side * 0.14, box.bottom() - side * 0.1, side * 0.14, side * 0.06), 2, 2);
    p.drawRoundedRect(QRectF(box.right() - side * 0.28, box.bottom() - side * 0.1, side * 0.14, side * 0.06), 2, 2);
    const qreal r = side * 0.3;
    const QPointF door(c.x(), c.y() - side * 0.03);
    // Hinges on the left.
    for (const qreal dy : {-0.55, 0.55}) {
        p.drawRoundedRect(QRectF(door.x() - r - side * 0.07, door.y() + dy * r - side * 0.035, side * 0.08, side * 0.07),
                          2, 2);
    }
    p.setPen(QPen(edge, side * 0.02));
    p.setBrush(theme.panel);
    p.drawEllipse(door, r, r);
    // Bolts round the door.
    p.setPen(Qt::NoPen);
    p.setBrush(edge);
    for (int i = 0; i < 8; ++i) {
        const qreal a = i * std::numbers::pi / 4 + std::numbers::pi / 8;
        p.drawEllipse(door + QPointF(std::cos(a), std::sin(a)) * r * 0.82, side * 0.018, side * 0.018);
    }
    // The dial: an accent ring with ticks, turned by dialTurn, and a fixed marker.
    const qreal dr = r * 0.5;
    p.setPen(QPen(theme.accent, side * 0.025));
    p.setBrush(theme.surface);
    p.drawEllipse(door, dr, dr);
    p.save();
    p.translate(door);
    p.rotate(dialTurn * 360);
    p.setPen(QPen(theme.text, side * 0.012, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 12; ++i) {
        p.drawLine(QPointF(0, -dr * 0.62), QPointF(0, i % 3 == 0 ? -dr * 0.88 : -dr * 0.78));
        p.rotate(30);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(theme.accent);
    p.drawEllipse(QPointF(0, 0), dr * 0.22, dr * 0.22);
    // The handle: three spokes.
    p.setPen(QPen(theme.accent, side * 0.022, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 3; ++i) {
        p.drawLine(QPointF(0, 0), QPointF(0, dr * 0.48));
        p.rotate(120);
    }
    p.restore();
    p.setPen(Qt::NoPen);
    p.setBrush(theme.accent);
    QPolygonF marker;
    marker << door + QPointF(-side * 0.025, -dr - side * 0.05) << door + QPointF(side * 0.025, -dr - side * 0.05)
           << door + QPointF(0, -dr - side * 0.01);
    p.drawPolygon(marker);
    p.restore();
}

}  // namespace fun
