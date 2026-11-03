// Copyright 2026 WindTerm NG contributors. Apache-2.0.
//
// SFTP file access on its own thread and its own SSH connection, so file
// transfers never slow down the terminal. Transfers keep several requests in
// flight (libssh async I/O), which is much faster than one block at a time.
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QThread>
#include <atomic>

#include "net/SshCommon.h"

struct RemoteEntry {
    QString name;
    bool isDirectory = false;
    bool isLink = false;
    qint64 size = 0;
    QDateTime modified;
    quint32 permissions = 0;
};
Q_DECLARE_METATYPE(RemoteEntry)

class SftpWorker;

// GUI-side handle. All calls return immediately; results come as signals.
class SftpClient : public QObject {
    Q_OBJECT

public:
    SftpClient(SshOptions options, SshPrompter prompter, QObject *parent = nullptr);
    ~SftpClient() override;

    void open();
    void list(const QString &path);
    quint64 download(const QString &remotePath, const QString &localPath);
    quint64 upload(const QString &localPath, const QString &remotePath);
    void makeDirectory(const QString &path);
    void remove(const QString &path, bool isDirectory);
    void rename(const QString &from, const QString &to);
    void cancelTransfers();
    QString host() const { return m_host; }

Q_SIGNALS:
    void opened(const QString &homePath);
    void listed(const QString &path, const QList<RemoteEntry> &entries);
    void failed(const QString &message);
    void changed(const QString &directory);  // after mkdir / remove / rename / upload
    void transferStarted(quint64 id, const QString &name, bool upload, qint64 size);
    void transferProgress(quint64 id, qint64 done, qint64 total);
    void transferFinished(quint64 id, bool ok, const QString &message);

private:
    QThread m_thread;
    SftpWorker *m_worker;
    QString m_host;
    std::atomic<quint64> m_nextId{1};
    std::shared_ptr<std::atomic<bool>> m_stop;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
