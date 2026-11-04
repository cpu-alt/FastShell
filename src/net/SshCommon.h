// Copyright 2026 FastShell contributors. Apache-2.0.
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
    QString user;          // empty: asked in the terminal ("login as:")
    QString password;      // optional; asked in the terminal when needed
    QString identityFile;  // optional private key file, the only key ever used
    bool legacyAlgorithms = false;  // also allow SHA-1 / CBC algorithms, including group1-sha1
    QString knownHostsFile;         // empty = ~/.ssh/known_hosts (tests use their own)

    QString displayName() const { return user.isEmpty() ? host : user + QLatin1Char('@') + host; }
};

struct SshPrompter {
    QObject *guiContext = nullptr;  // object living in the GUI thread
    const std::atomic<bool> *stop = nullptr;
    std::function<bool(const QString &question)> confirm;
    std::function<std::optional<QString>(const QString &prompt, bool secret)> ask;
    // Optional, called on the worker thread: the login, the passwords and
    // "Access denied" then go to the terminal itself, like PuTTY.
    std::function<std::optional<QString>(const QString &prompt, bool secret)> askInTerminal;
    std::function<void(const QString &text)> showInTerminal;

    bool confirmOnGui(const QString &question) const;
    std::optional<QString> askOnGui(const QString &prompt, bool secret) const;
    std::optional<QString> askLogin(const QString &prompt, bool secret) const;  // terminal, else a dialog
    void notice(const QString &text) const;
};

struct SshSessionDeleter {
    void operator()(ssh_session_struct *session) const;
};
using SshSessionPtr = std::unique_ptr<ssh_session_struct, SshSessionDeleter>;

struct SshConnectResult {
    SshSessionPtr session;  // null on failure
    QString error;          // empty when cancelled by the user
    QString user;           // the user name that worked (reused for SFTP)
    QString password;       // the password that worked, if any (reused for SFTP)
    bool usedLegacyAlgorithms = false;  // connected only thanks to old algorithms
    QString newHostKey;  // fingerprint saved on this first connection, else empty
};

SshConnectResult sshConnect(const SshOptions &options, const SshPrompter &prompter);
