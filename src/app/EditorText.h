// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Text handling for the built-in editor, without GUI (tested): decode a file
// for editing and encode it back exactly as it was (UTF-8 BOM, CRLF or LF),
// so saving a Linux config file never turns it into a Windows one.
#pragma once

#include <QByteArray>
#include <QString>

namespace editor {

struct Decoded {
    QString text;       // always with '\n' line ends
    bool crlf = false;  // the file used "\r\n"
    bool bom = false;   // UTF-8 byte order mark at the start
    bool binary = false;     // NUL bytes: not a text file
    bool notUtf8 = false;    // invalid UTF-8: decoded as Latin-1
};

Decoded decode(const QByteArray &data);
QByteArray encode(const QString &text, bool crlf, bool bom, bool latin1 = false);

// Next match of `needle` from `from` (wrapping around), or -1.
qsizetype find(const QString &text, const QString &needle, qsizetype from, bool backward, bool caseSensitive);

}  // namespace editor
