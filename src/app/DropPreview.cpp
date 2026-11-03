// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/DropPreview.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>

#include "app/Style.h"

DropPreview::DropPreview(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    hide();

    m_geometry.setDuration(140);
    m_geometry.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_geometry, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_rect = value.toRectF();
        update();
    });
    m_opacity.setDuration(120);
    m_opacity.setStartValue(0.0);
    m_opacity.setEndValue(1.0);
    connect(&m_opacity, &QVariantAnimation::valueChanged, this, [this] { update(); });
    connect(&m_opacity, &QVariantAnimation::finished, this, [this] {
        if (!m_shown) hide();
    });
}

void DropPreview::showZone(const QRect &rect, const QString &label) {
    m_label = label;
    if (m_shown && rect == m_target) return;
    setGeometry(parentWidget()->rect());
    raise();
    if (!m_shown) {
        // Appear from a slightly smaller box, like a window snapping in.
        m_shown = true;
        show();
        const QRectF from = QRectF(rect).adjusted(rect.width() * 0.08, rect.height() * 0.08,
                                                  -rect.width() * 0.08, -rect.height() * 0.08);
        m_geometry.stop();
        m_geometry.setStartValue(from);
        m_opacity.stop();
        m_opacity.setDirection(QAbstractAnimation::Forward);
        m_opacity.start();
    } else {
        m_geometry.stop();
        m_geometry.setStartValue(m_rect);
    }
    m_target = rect;
    m_geometry.setEndValue(QRectF(rect));
    m_geometry.start();
}

void DropPreview::hideZone() {
    if (!m_shown) return;
    m_shown = false;
    m_target = QRect();
    m_opacity.stop();
    m_opacity.setDirection(QAbstractAnimation::Backward);
    m_opacity.start();
}

void DropPreview::paintEvent(QPaintEvent *) {
    if (m_rect.isEmpty()) return;
    const qreal opacity = m_opacity.state() == QAbstractAnimation::Running ? m_opacity.currentValue().toReal()
                        : m_shown                                          ? 1.0
                                                                           : 0.0;
    if (opacity <= 0) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(opacity);

    const QRectF zone = m_rect.adjusted(6, 6, -6, -6);
    const QColor accent = style::Accent;
    QColor fill = accent;
    fill.setAlpha(46);
    QColor glow = accent;
    glow.setAlpha(28);
    // Soft outer glow, then the frosted zone and its border.
    p.setPen(QPen(glow, 8));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(zone, 10, 10);
    p.setPen(QPen(accent, 2));
    p.setBrush(fill);
    p.drawRoundedRect(zone, 10, 10);
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(zone.adjusted(3, 3, -3, -3), 8, 8);

    if (m_label.isEmpty() || zone.width() < 80 || zone.height() < 40) return;
    QFont font = QApplication::font();
    font.setBold(true);
    p.setFont(font);
    const QFontMetricsF fm(font);
    const QSizeF size(fm.horizontalAdvance(m_label) + 28, fm.height() + 14);
    const QRectF pill(zone.center() - QPointF(size.width() / 2, size.height() / 2), size);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x10, 0x14, 0x1B, 225));
    p.drawRoundedRect(pill, size.height() / 2, size.height() / 2);
    p.setPen(Qt::white);
    p.drawText(pill, Qt::AlignCenter, m_label);
}
