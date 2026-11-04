// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
//
// Frameless balloon popup anchored to its parent widget, with an arrow
// pointing at it. Closes on Escape, outside clicks or focus loss.
#pragma once

#include <QFrame>

class PopupWidget : public QFrame {
    Q_OBJECT

public:
    enum Area { LeftArea, MiddleArea, RightArea, TopArea, BottomArea };

    explicit PopupWidget(QWidget *parent = nullptr);
    ~PopupWidget() override;

    void setCentralWidget(QWidget *widget);
    void show(Area horizontal, Area vertical);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
};
