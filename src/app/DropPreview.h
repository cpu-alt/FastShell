// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Translucent "snap" preview shown while a pane or a session is dragged over
// a tab: like Windows Snap, it outlines where the pane will land and glides
// from one zone to the next.
#pragma once

#include <QVariantAnimation>
#include <QWidget>

class DropPreview : public QWidget {
    Q_OBJECT

public:
    explicit DropPreview(QWidget *parent);

    // Shows (or moves) the preview to rect, in parent coordinates.
    void showZone(const QRect &rect, const QString &label);
    void hideZone();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVariantAnimation m_geometry;  // QRectF from the last zone to the new one
    QVariantAnimation m_opacity;   // 0..1 fade in / out
    QRectF m_rect;
    QRect m_target;
    QString m_label;
    bool m_shown = false;
};
