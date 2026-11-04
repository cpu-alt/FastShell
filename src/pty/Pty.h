// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
//
// Local pseudo terminal. ConPty on Windows 10 1809+, POSIX pty elsewhere.
// (The winpty fallback of the original is gone: every supported Windows
// version ships ConPty.)
#pragma once

#include <QStringList>
#include <memory>

#include "core/Backend.h"

class Pty : public Backend {
    Q_OBJECT

public:
    struct Options {
        QString program;           // empty = default shell
        QStringList arguments;
        QString workingDirectory;  // empty = home directory
        QStringList extraEnvironment;  // "NAME=value", added to the inherited environment
    };

    static std::unique_ptr<Pty> create(Options options, QObject *parent = nullptr);
    static QString defaultShell();

    QString displayName() const override;

protected:
    Pty(Options options, QObject *parent);

    Options m_options;
};
