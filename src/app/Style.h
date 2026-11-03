// Copyright 2026 FastShell contributors. Apache-2.0.
//
// The application look: dark palette, style sheet, fonts, line icons for the
// toolbar, and the Windows title bar matched to the app (dark, rounded).
#pragma once

#include <QColor>
#include <QIcon>

class QApplication;
class QWidget;

namespace style {

inline const QColor Window(0x13, 0x15, 0x1A);   // frame, menus bar, tab strip
inline const QColor Panel(0x18, 0x1B, 0x21);    // docks, trees
inline const QColor Surface(0x1F, 0x23, 0x2A);  // inputs, buttons, menus
inline const QColor Border(0x2C, 0x31, 0x3A);
inline const QColor Hover(0x27, 0x2C, 0x35);
inline const QColor Text(0xD8, 0xDC, 0xE3);
inline const QColor Muted(0x8A, 0x91, 0x9E);
inline const QColor Accent(0x3D, 0x9B, 0xF5);
inline const QColor TerminalBackground(0x1B, 0x1D, 0x23);  // same as Theme::background

// Palette, fonts and style sheet for the whole application.
void apply(QApplication &app);

// Line icons drawn at runtime for the toolbar.
enum class Glyph {
    NewSession, Terminal, SplitRight, SplitDown, Broadcast, RemoteDesktop, Panels, Close,
    ZoomIn, ZoomOut, Font, Clock, LineNumbers, SessionList, Filer, Outline, FullScreen,
};
QIcon icon(Glyph glyph);

}  // namespace style
