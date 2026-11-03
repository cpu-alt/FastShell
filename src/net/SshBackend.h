// Copyright 2026 WindTerm NG contributors. Apache-2.0.
//
// Interactive SSH shell over the system libssh (0.11+). Replaces the bundled
// libssh 0.9.5 fork, which is affected by CVE-2023-48795 (Terrapin) and other
// fixed issues. Agent, default keys, key files, password and
// keyboard-interactive authentication are supported.
#pragma once

#include <QMutex>
#include <atomic>
#include <thread>

#include "core/Backend.h"
#include "net/SshCommon.h"

class SshBackend final : public Backend {
    Q_OBJECT

public:
    using Options = SshOptions;

    // Called on the GUI thread. Return false / nullopt to cancel.
    std::function<bool(const QString &question)> confirm;
    std::function<std::optional<QString>(const QString &prompt, bool secret)> ask;

    explicit SshBackend(Options options, QObject *parent = nullptr);
    ~SshBackend() override;

    void start(int rows, int columns) override;
    void write(const QByteArray &data) override;
    void resize(int rows, int columns) override;
    QString displayName() const override { return m_options.displayName(); }

    // Options with the password that actually worked (for SFTP). GUI thread.
    SshOptions connectedOptions() const;

Q_SIGNALS:
    void connected();

private:
    void run();

    Options m_options;
    std::thread m_worker;
    std::atomic<bool> m_stop{false};

    mutable QMutex m_mutex;
    QByteArray m_pendingWrite;
    QString m_workingPassword;
    bool m_usedLegacy = false;
    int m_rows = 24;
    int m_columns = 80;
    bool m_resizePending = false;
};
