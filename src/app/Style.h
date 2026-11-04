// Copyright 2026 FastShell contributors. Apache-2.0.
//
// The application look: colour themes (chosen in View > Theme and switched
// live), palette, style sheet, fonts, line icons for the toolbar, and the
// Windows title bar matched to the theme.
#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>

#include "app/Theme.h"

class QApplication;
class QWidget;

namespace style {

struct ColorTheme {
    QString id;    // saved in the settings
    QString name;  // shown in the menu
    bool dark = true;
    QColor window;   // frame, menu bar, tab strip
    QColor panel;    // docks, trees
    QColor surface;  // inputs, buttons, menus
    QColor border;
    QColor hover;
    QColor text;
    QColor muted;
    QColor accent;
    QColor strong;  // text on selections and titles
    Theme terminal;
};

// Built-in themes, the first one is the default.
const QList<ColorTheme> &themes();
const ColorTheme &current();
// Accent outlines on panels, tabs, menus and the active pane.
bool glow();

// Palette, fonts and style sheet for the whole application, from the saved
// theme. Call once at start.
void apply(QApplication &app);
// Switch live (saved in the settings). Emits Notifier::changed().
void setTheme(const QString &id);
void setGlow(bool on);

class Notifier : public QObject {
    Q_OBJECT
Q_SIGNALS:
    void changed();
};
Notifier *notifier();

// Line icons drawn at runtime for the toolbar, in the current theme colours.
enum class Glyph {
    NewSession, Terminal, SplitRight, SplitDown, Broadcast, RemoteDesktop, Panels, Close,
    ZoomIn, ZoomOut, Font, Clock, LineNumbers, SessionList, Filer, Outline, FullScreen,
};
QIcon icon(Glyph glyph);

// Matches the title bar of a top-level window to the theme (Windows).
void decorate(QWidget *window);

}  // namespace style
