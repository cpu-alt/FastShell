// Copyright 2020 WindTerm, 2026 WindTerm NG contributors. Apache-2.0.
#include "pty/Pty.h"

#include <QFileInfo>
#include <QStandardPaths>

#ifdef _WIN32
#include "pty/ConPty.h"
#else
#include "pty/UnixPty.h"
#endif

Pty::Pty(Options options, QObject *parent) : Backend(parent), m_options(std::move(options)) {
    if (m_options.program.isEmpty()) {
        m_options.program = defaultShell();
    }
}

std::unique_ptr<Pty> Pty::create(Options options, QObject *parent) {
#ifdef _WIN32
    return std::make_unique<ConPty>(std::move(options), parent);
#else
    return std::make_unique<UnixPty>(std::move(options), parent);
#endif
}

QString Pty::defaultShell() {
#ifdef _WIN32
    for (const char *candidate : {"pwsh.exe", "powershell.exe"}) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(candidate));
        if (!path.isEmpty()) {
            return path;
        }
    }
    const QString comspec = qEnvironmentVariable("COMSPEC");
    return comspec.isEmpty() ? QStringLiteral("cmd.exe") : comspec;
#else
    const QString shell = qEnvironmentVariable("SHELL");
    return shell.isEmpty() ? QStringLiteral("/bin/sh") : shell;
#endif
}

QString Pty::displayName() const {
    return QFileInfo(m_options.program).completeBaseName();
}

