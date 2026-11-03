// Copyright 2026 WindTerm NG contributors. Apache-2.0.
//
// Connection + host key check + authentication, shared by the interactive
// shell (SshBackend) and the file browser (SftpClient). Runs on a worker
// thread; questions to the user are executed on the GUI thread.
#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

struct ssh_session_struct;

struct SshOptions {
    QString host;
    int port = 22;
    QString user;
    QString password;      // optional; asked interactively when needed
    QString identityFile;  // optional private key file
    bool legacyAlgorithms = false;  // also allow SHA-1 / CBC algorithms, including group1-sha1

    QString displayName() const { return user.isEmpty() ? host : user + QLatin1Char('@') + host; }
};

struct SshPrompter {
    QObject *guiContext = nullptr;  // object living in the GUI thread
    const std::atomic<bool> *stop = nullptr;
    std::function<bool(const QString &question)> confirm;
    std::function<std::optional<QString>(const QString &prompt, bool secret)> ask;

    bool confirmOnGui(const QString &question) const;
    std::optional<QString> askOnGui(const QString &prompt, bool secret) const;
};

struct SshSessionDeleter {
    void operator()(ssh_session_struct *session) const;
};
using SshSessionPtr = std::unique_ptr<ssh_session_struct, SshSessionDeleter>;

struct SshConnectResult {
    SshSessionPtr session;  // null on failure
    QString error;          // empty when cancelled by the user
    QString password;       // the password that worked, if any (reused for SFTP)
    bool usedLegacyAlgorithms = false;  // connected only thanks to old algorithms
};

SshConnectResult sshConnect(const SshOptions &options, const SshPrompter &prompter);
