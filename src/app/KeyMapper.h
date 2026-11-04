// Copyright 2026 FastShell contributors. Apache-2.0.
#pragma once

#include <QByteArray>

class QKeyEvent;

struct KeyModes {
    bool applicationCursor = false;
    bool applicationKeypad = false;
    bool newLine = false;
    bool backspaceCtrlH = false;  // some consoles want ^H instead of DEL
};

// Translates a key press into the bytes an xterm would send.
// Returns an empty array when the key produces nothing.
QByteArray mapKey(const QKeyEvent &event, const KeyModes &modes);
