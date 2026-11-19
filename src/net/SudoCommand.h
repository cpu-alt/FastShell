// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Remote commands of the built-in editor for files the SSH account cannot
// read or write (e.g. /etc/freeradius): run through `sudo`. Built here
// without libssh so they are tested.
//
// Safety rules:
// - every path is one single-quoted shell argument, never pasted in the script;
// - the password never appears in a command: it goes to sudo's standard input
//   (`-S`, empty prompt `-p ''`, `-k` so it is always read);
// - the new content is first written to a private file (0600) in the user's
//   home, then copied next to the target and moved over it in one step, with
//   the owner and mode of the original and a `.bak` copy of the old version:
//   a broken connection never leaves a half-written config file.
#pragma once

#include <QString>

namespace sudo {

// 'text' with every ' written as '\''; safe for any byte sequence.
inline QString shellQuote(const QString &text) {
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

inline QString prefix() {
    return QStringLiteral("sudo -S -k -p '' -- ");
}

// Prints the file on standard output.
inline QString readCommand(const QString &path) {
    return prefix() + QStringLiteral("cat -- ") + shellQuote(path);
}

// Replaces `target` with the content of `source` (see the rules above).
// The script contains no single quote, so it can be single-quoted itself;
// the paths arrive as $1 and $2.
inline QString writeCommand(const QString &target, const QString &source) {
    const QString script = QStringLiteral(
        "set -e; t=\"$1\"; s=\"$2\"; n=\"$t.fastshell-new\"; "
        "trap \"rm -f -- \\\"\\$n\\\"\" EXIT; "
        "test -f \"$t\"; "
        "cp -p -- \"$t\" \"$t.bak\"; "
        "cp -- \"$s\" \"$n\"; "
        "chown --reference=\"$t\" -- \"$n\"; "
        "chmod --reference=\"$t\" -- \"$n\"; "
        "mv -f -- \"$n\" \"$t\"");
    return prefix() + QStringLiteral("sh -c ") + shellQuote(script) + QStringLiteral(" fastshell-edit ")
           + shellQuote(target) + QLatin1Char(' ') + shellQuote(source);
}

}  // namespace sudo
