// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
#pragma once

#ifndef _WIN32

#include <sys/types.h>

#include "pty/Pty.h"

class QSocketNotifier;

class UnixPty final : public Pty {
public:
    UnixPty(Options options, QObject *parent = nullptr);
    ~UnixPty() override;

    void start(int rows, int columns) override;
    void write(const QByteArray &data) override;
    void resize(int rows, int columns) override;

private:
    void onReadable();
    void stop();

    int m_master = -1;
    pid_t m_child = -1;
    QSocketNotifier *m_notifier = nullptr;
};

#endif
