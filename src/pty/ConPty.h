// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
#pragma once

#ifdef _WIN32

#include <atomic>
#include <thread>
#include <vector>

#include <windows.h>

#include "pty/Pty.h"

class QWinEventNotifier;

class ConPty final : public Pty {
public:
    ConPty(Options options, QObject *parent = nullptr);
    ~ConPty() override;

    static bool isAvailable();

    void start(int rows, int columns) override;
    void write(const QByteArray &data) override;
    void resize(int rows, int columns) override;

private:
    void readLoop();
    void onProcessExited();
    void stop();
    void fail(const QString &what, DWORD error);

    HPCON m_console = nullptr;
    HANDLE m_input = INVALID_HANDLE_VALUE;   // we write keystrokes here
    HANDLE m_output = INVALID_HANDLE_VALUE;  // we read screen output here
    PROCESS_INFORMATION m_process{};
    std::vector<unsigned char> m_attributeList;
    std::thread m_reader;
    QWinEventNotifier *m_exitNotifier = nullptr;
    int m_rows = 0;
    int m_columns = 0;
};

#endif
