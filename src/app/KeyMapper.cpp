// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include "app/KeyMapper.h"

#include <QKeyEvent>

namespace {

// xterm modifier parameter: 1 + shift(1) + alt(2) + ctrl(4).
int modifierCode(Qt::KeyboardModifiers m) {
    int code = 1;
    if (m & Qt::ShiftModifier) code += 1;
    if (m & Qt::AltModifier) code += 2;
    if (m & Qt::ControlModifier) code += 4;
    return code;
}

// Cursor-style keys: ESC [ X, ESC O X in application mode, ESC [ 1;m X with modifiers.
QByteArray cursorKey(char final, Qt::KeyboardModifiers m, bool application) {
    const int mod = modifierCode(m);
    if (mod > 1) {
        return "\x1b[1;" + QByteArray::number(mod) + final;
    }
    return QByteArray(application ? "\x1bO" : "\x1b[") + final;
}

// Tilde keys: ESC [ n ~, ESC [ n;m ~ with modifiers.
QByteArray tildeKey(int number, Qt::KeyboardModifiers m) {
    const int mod = modifierCode(m);
    QByteArray seq = "\x1b[" + QByteArray::number(number);
    if (mod > 1) {
        seq += ';' + QByteArray::number(mod);
    }
    return seq + '~';
}

}  // namespace

QByteArray mapKey(const QKeyEvent &event, const KeyModes &modes) {
    const Qt::KeyboardModifiers m = event.modifiers() & ~Qt::KeypadModifier;
    const bool ctrl = m & Qt::ControlModifier;
    const bool alt = m & Qt::AltModifier;

    switch (event.key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: return QByteArray(alt ? "\x1b" : "") + (modes.newLine ? "\r\n" : "\r");
    case Qt::Key_Backspace: {
        const bool sendCtrlH = ctrl != modes.backspaceCtrlH;  // Ctrl swaps the two codes
        return QByteArray(alt ? "\x1b" : "") + (sendCtrlH ? "\x08" : "\x7f");
    }
    case Qt::Key_Tab: return ctrl ? QByteArray() : QByteArray("\t");
    case Qt::Key_Backtab: return "\x1b[Z";
    case Qt::Key_Escape: return "\x1b";
    case Qt::Key_Up: return cursorKey('A', m, modes.applicationCursor);
    case Qt::Key_Down: return cursorKey('B', m, modes.applicationCursor);
    case Qt::Key_Right: return cursorKey('C', m, modes.applicationCursor);
    case Qt::Key_Left: return cursorKey('D', m, modes.applicationCursor);
    case Qt::Key_Home: return cursorKey('H', m, modes.applicationCursor);
    case Qt::Key_End: return cursorKey('F', m, modes.applicationCursor);
    case Qt::Key_Insert: return tildeKey(2, m);
    case Qt::Key_Delete: return tildeKey(3, m);
    case Qt::Key_PageUp: return tildeKey(5, m);
    case Qt::Key_PageDown: return tildeKey(6, m);
    case Qt::Key_F1: return cursorKey('P', m, true);
    case Qt::Key_F2: return cursorKey('Q', m, true);
    case Qt::Key_F3: return cursorKey('R', m, true);
    case Qt::Key_F4: return cursorKey('S', m, true);
    case Qt::Key_F5: return tildeKey(15, m);
    case Qt::Key_F6: return tildeKey(17, m);
    case Qt::Key_F7: return tildeKey(18, m);
    case Qt::Key_F8: return tildeKey(19, m);
    case Qt::Key_F9: return tildeKey(20, m);
    case Qt::Key_F10: return tildeKey(21, m);
    case Qt::Key_F11: return tildeKey(23, m);
    case Qt::Key_F12: return tildeKey(24, m);
    default: break;
    }

    // Ctrl + key -> C0 control character.
    if (ctrl) {
        const int key = event.key();
        char control = 0;
        if (key >= Qt::Key_A && key <= Qt::Key_Z) control = char(key - Qt::Key_A + 1);
        else if (key == Qt::Key_Space || key == Qt::Key_2 || key == Qt::Key_At) control = 0;
        else if (key == Qt::Key_BracketLeft || key == Qt::Key_3) control = 0x1b;
        else if (key == Qt::Key_Backslash || key == Qt::Key_4) control = 0x1c;
        else if (key == Qt::Key_BracketRight || key == Qt::Key_5) control = 0x1d;
        else if (key == Qt::Key_6 || key == Qt::Key_AsciiCircum) control = 0x1e;
        else if (key == Qt::Key_Slash || key == Qt::Key_Minus || key == Qt::Key_7 || key == Qt::Key_Underscore) control = 0x1f;
        else if (key == Qt::Key_8 || key == Qt::Key_Question) control = 0x7f;
        else control = -1;

        // AltGr on Windows reports Ctrl+Alt: let the composed text through.
        if (control != -1 && !(alt && !event.text().isEmpty() && event.text().at(0).isPrint())) {
            QByteArray seq(1, control);
            return alt ? "\x1b" + seq : seq;
        }
    }

    const QString text = event.text();
    if (text.isEmpty()) {
        return {};
    }
    QByteArray bytes = text.toUtf8();
    if (alt && !ctrl) {
        bytes.prepend('\x1b');  // Meta sends ESC prefix
    }
    return bytes;
}
