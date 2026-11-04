// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Cleans text that comes from outside before it is shown or sent:
// - window titles set by a remote program (escape sequence OSC 0/2),
// - text pasted into a terminal (from a web page, a mail...),
// - file names chosen by an SFTP server, before saving them on this PC.
#pragma once

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>

namespace safety {

// True for characters that must never reach the screen or a shell silently:
// C0/C1 controls (ESC...), DEL, and invisible format characters such as the
// bidirectional overrides that can make "gpj.exe" look like "exe.jpg".
inline bool isHiddenOrControl(QChar c) {
    const ushort u = c.unicode();
    if (u < 0x20 || u == 0x7F || (u >= 0x80 && u <= 0x9F)) return true;
    if ((u >= 0x202A && u <= 0x202E) || (u >= 0x2066 && u <= 0x2069) || u == 0x200E || u == 0x200F) return true;
    return false;
}

// A title from the remote side, for a tab or a window: one line, no control
// or direction characters, at most 120 characters.
inline QString displayTitle(const QString &raw) {
    QString out;
    out.reserve(std::min<qsizetype>(raw.size(), 120));
    for (QChar c : raw) {
        if (out.size() >= 120) break;
        out += isHiddenOrControl(c) ? QChar(QLatin1Char(' ')) : c;
    }
    return out.simplified();
}

// Number of lines a paste would run (a line break acts as Enter).
inline int pasteLineCount(const QString &text) {
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    while (t.endsWith(QLatin1Char('\n')) || t.endsWith(QLatin1Char('\r'))) t.chop(1);
    return int(t.count(QLatin1Char('\n')) + t.count(QLatin1Char('\r'))) + 1;
}

// Bytes sent to the terminal for a paste: line breaks become Enter (CR),
// tabs are kept, every other control character (ESC, Ctrl+C, ...) and the
// direction characters are dropped, so pasted text cannot carry hidden
// key presses or escape out of bracketed paste.
inline QByteArray pasteBytes(const QString &text, bool bracketed) {
    QString clean;
    clean.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\r')) {
            clean += QLatin1Char('\r');
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) ++i;
        } else if (c == QLatin1Char('\n')) {
            clean += QLatin1Char('\r');
        } else if (c == QLatin1Char('\t') || !isHiddenOrControl(c)) {
            clean += c;
        }
    }
    QByteArray bytes = clean.toUtf8();
    if (bracketed) bytes = "\x1b[200~" + bytes + "\x1b[201~";
    return bytes;
}

// A file name chosen by a remote server, made safe to create in a Windows
// folder: only the last component, no drive or stream (":"), no reserved
// characters or device names (CON, NUL, COM1...), no trailing dot or space.
inline QString localFileName(const QString &remoteName) {
    QString name = remoteName;
    name = name.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
    QString out;
    for (QChar c : name) {
        if (isHiddenOrControl(c)) continue;
        out += QStringLiteral("<>:\"/\\|?*").contains(c) ? QChar(QLatin1Char('_')) : c;
    }
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' '))) out.chop(1);
    out = out.trimmed().left(200);
    if (out.isEmpty() || out == QLatin1String(".") || out == QLatin1String("..")) out = QStringLiteral("download");
    static const QRegularExpression device(QStringLiteral(R"(^(con|prn|aux|nul|com[0-9¹²³]|lpt[0-9¹²³])(\..*)?$)"),
                                           QRegularExpression::CaseInsensitiveOption);
    if (device.match(out).hasMatch()) out.prepend(QLatin1Char('_'));
    return out;
}

// A path in `dir` that does not exist yet: "name.txt", then "name (1).txt"...
inline QString uniqueFilePath(const QString &dir, const QString &name) {
    const QDir folder(dir);
    QString candidate = folder.filePath(name);
    const QFileInfo info(name);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    for (int i = 1; QFileInfo::exists(candidate) || QFileInfo::exists(candidate + QStringLiteral(".part")); ++i) {
        candidate = folder.filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(suffix));
    }
    return candidate;
}

}  // namespace safety
