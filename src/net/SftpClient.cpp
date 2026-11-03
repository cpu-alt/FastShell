// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include "net/SftpClient.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <deque>
#include <fcntl.h>
#include <vector>

#include <libssh/libssh.h>
#include <libssh/sftp.h>

namespace {

constexpr int InFlight = 16;                       // pipelined requests per transfer
constexpr std::size_t PreferredChunk = 256 * 1024;  // capped by the server limits

QString joinPath(const QString &dir, const QString &name) {
    return dir.endsWith(QLatin1Char('/')) ? dir + name : dir + QLatin1Char('/') + name;
}

QString parentOf(const QString &path) {
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash <= 0 ? QStringLiteral("/") : path.left(slash);
}

}  // namespace

// Lives in the worker thread; every slot runs there.
class SftpWorker : public QObject {
    Q_OBJECT

public:
    SftpWorker(SshOptions options, SshPrompter prompter, std::shared_ptr<std::atomic<bool>> stop,
               std::shared_ptr<std::atomic<bool>> cancel)
        : m_options(std::move(options)), m_prompter(std::move(prompter)), m_stop(std::move(stop)),
          m_cancel(std::move(cancel)) {
        m_prompter.stop = m_stop.get();
    }

    ~SftpWorker() override { close(); }

    void close() {
        if (m_sftp) {
            sftp_free(m_sftp);
            m_sftp = nullptr;
        }
        m_session.reset();
    }

    bool ensureOpen() {
        if (m_sftp) return true;
        SshConnectResult connection = sshConnect(m_options, m_prompter);
        if (!connection.session) {
            Q_EMIT failed(connection.error.isEmpty() ? tr("SFTP connection cancelled") : connection.error);
            return false;
        }
        m_session = std::move(connection.session);
        m_sftp = sftp_new(m_session.get());
        if (!m_sftp || sftp_init(m_sftp) != SSH_OK) {
            Q_EMIT failed(tr("The server does not offer SFTP: %1").arg(QString::fromUtf8(ssh_get_error(m_session.get()))));
            close();
            return false;
        }
        sftp_limits_t limits = sftp_limits(m_sftp);
        if (limits) {
            m_readChunk = std::min<std::size_t>(PreferredChunk, std::size_t(limits->max_read_length));
            m_writeChunk = std::min<std::size_t>(PreferredChunk, std::size_t(limits->max_write_length));
            sftp_limits_free(limits);
        }
        return true;
    }

    QString error(const QString &what) const {
        return QStringLiteral("%1: %2").arg(what, QString::fromUtf8(ssh_get_error(m_session.get())));
    }

public Q_SLOTS:
    void open() {
        if (!ensureOpen()) return;
        char *home = sftp_canonicalize_path(m_sftp, ".");
        Q_EMIT opened(home ? QString::fromUtf8(home) : QStringLiteral("/"));
        ssh_string_free_char(home);
    }

    void list(const QString &path) {
        if (!ensureOpen()) return;
        sftp_dir dir = sftp_opendir(m_sftp, path.toUtf8().constData());
        if (!dir) {
            Q_EMIT failed(error(tr("Cannot open %1").arg(path)));
            return;
        }
        QList<RemoteEntry> entries;
        while (sftp_attributes a = sftp_readdir(m_sftp, dir)) {
            const QString name = QString::fromUtf8(a->name);
            if (name != QLatin1String(".") && name != QLatin1String("..")) {
                RemoteEntry e;
                e.name = name;
                e.isLink = a->type == SSH_FILEXFER_TYPE_SYMLINK;
                e.isDirectory = a->type == SSH_FILEXFER_TYPE_DIRECTORY;
                if (e.isLink) {  // follow links so linked directories open
                    if (sftp_attributes t = sftp_stat(m_sftp, joinPath(path, name).toUtf8().constData())) {
                        e.isDirectory = t->type == SSH_FILEXFER_TYPE_DIRECTORY;
                        sftp_attributes_free(t);
                    }
                }
                e.size = qint64(a->size);
                e.modified = QDateTime::fromSecsSinceEpoch(qint64(a->mtime));
                e.permissions = a->permissions;
                entries << e;
            }
            sftp_attributes_free(a);
        }
        sftp_closedir(dir);
        Q_EMIT listed(path, entries);
    }

    void download(quint64 id, const QString &remote, const QString &local) {
        m_cancel->store(false);
        if (!ensureOpen()) {
            Q_EMIT transferFinished(id, false, tr("Not connected"));
            return;
        }
        sftp_attributes attrs = sftp_stat(m_sftp, remote.toUtf8().constData());
        const qint64 size = attrs ? qint64(attrs->size) : 0;
        if (attrs) sftp_attributes_free(attrs);
        Q_EMIT transferStarted(id, QFileInfo(remote).fileName(), false, size);

        sftp_file file = sftp_open(m_sftp, remote.toUtf8().constData(), O_RDONLY, 0);
        QFile out(local + QStringLiteral(".part"));
        if (!file || !out.open(QIODevice::WriteOnly)) {
            if (file) sftp_close(file);
            Q_EMIT transferFinished(id, false, file ? out.errorString() : error(tr("Cannot open %1").arg(remote)));
            return;
        }

        std::deque<sftp_aio> pending;
        std::vector<char> buffer(m_readChunk);
        qint64 requested = 0;
        qint64 done = 0;
        bool ok = true;
        QString message;
        auto issue = [&] {
            while (int(pending.size()) < InFlight && requested < size) {
                sftp_aio aio = nullptr;
                const ssize_t n = sftp_aio_begin_read(file, m_readChunk, &aio);
                if (n == SSH_ERROR) {
                    ok = false;
                    message = error(tr("Read failed"));
                    return;
                }
                requested += n;
                pending.push_back(aio);
            }
        };
        issue();
        while (ok && !pending.empty()) {
            sftp_aio aio = pending.front();
            pending.pop_front();
            const ssize_t n = sftp_aio_wait_read(&aio, buffer.data(), buffer.size());
            if (n < 0) {
                ok = false;
                message = error(tr("Read failed"));
                break;
            }
            if (n > 0 && out.write(buffer.data(), n) != n) {
                ok = false;
                message = out.errorString();
                break;
            }
            done += n;
            Q_EMIT transferProgress(id, done, size);
            if (*m_stop || *m_cancel) {
                ok = false;
                message = tr("Cancelled");
                break;
            }
            issue();
        }
        // Requests still in flight must be waited for before closing the file.
        for (sftp_aio aio : pending) {
            sftp_aio_wait_read(&aio, buffer.data(), buffer.size());
        }
        sftp_close(file);
        out.close();
        if (ok) {
            QFile::remove(local);
            ok = out.rename(local);
            if (!ok) message = out.errorString();
        } else {
            out.remove();
        }
        Q_EMIT transferFinished(id, ok, ok ? local : message);
    }

    void upload(quint64 id, const QString &local, const QString &remote) {
        m_cancel->store(false);
        if (!ensureOpen()) {
            Q_EMIT transferFinished(id, false, tr("Not connected"));
            return;
        }
        QFile in(local);
        if (!in.open(QIODevice::ReadOnly)) {
            Q_EMIT transferFinished(id, false, in.errorString());
            return;
        }
        const qint64 size = in.size();
        Q_EMIT transferStarted(id, QFileInfo(local).fileName(), true, size);
        sftp_file file = sftp_open(m_sftp, remote.toUtf8().constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (!file) {
            Q_EMIT transferFinished(id, false, error(tr("Cannot create %1").arg(remote)));
            return;
        }

        // Each in-flight request keeps its own buffer alive until completed.
        std::deque<std::pair<sftp_aio, std::vector<char>>> pending;
        qint64 done = 0;
        bool ok = true;
        QString message;
        bool eof = false;
        while (ok && (!eof || !pending.empty())) {
            while (!eof && int(pending.size()) < InFlight) {
                std::vector<char> chunk(m_writeChunk);
                const qint64 n = in.read(chunk.data(), qint64(chunk.size()));
                if (n <= 0) {
                    eof = true;
                    break;
                }
                chunk.resize(std::size_t(n));
                sftp_aio aio = nullptr;
                if (sftp_aio_begin_write(file, chunk.data(), chunk.size(), &aio) == SSH_ERROR) {
                    ok = false;
                    message = error(tr("Write failed"));
                    break;
                }
                pending.emplace_back(aio, std::move(chunk));
            }
            if (pending.empty()) break;
            auto &[aio, chunk] = pending.front();
            const ssize_t n = sftp_aio_wait_write(&aio);
            if (n < 0) {
                ok = false;
                message = error(tr("Write failed"));
            } else {
                done += n;
                Q_EMIT transferProgress(id, done, size);
            }
            pending.pop_front();
            if (*m_stop || *m_cancel) {
                ok = false;
                message = tr("Cancelled");
            }
        }
        for (auto &[aio, chunk] : pending) {
            sftp_aio_wait_write(&aio);
        }
        sftp_close(file);
        if (!ok) {
            sftp_unlink(m_sftp, remote.toUtf8().constData());
        }
        Q_EMIT transferFinished(id, ok, ok ? remote : message);
        Q_EMIT changed(parentOf(remote));
    }

    void makeDirectory(const QString &path) {
        if (!ensureOpen()) return;
        if (sftp_mkdir(m_sftp, path.toUtf8().constData(), 0755) != SSH_OK) {
            Q_EMIT failed(error(tr("Cannot create %1").arg(path)));
        }
        Q_EMIT changed(parentOf(path));
    }

    void removePath(const QString &path, bool isDirectory) {
        if (!ensureOpen()) return;
        const int rc = isDirectory ? sftp_rmdir(m_sftp, path.toUtf8().constData())
                                   : sftp_unlink(m_sftp, path.toUtf8().constData());
        if (rc != SSH_OK) {
            Q_EMIT failed(error(tr("Cannot delete %1").arg(path)));
        }
        Q_EMIT changed(parentOf(path));
    }

    void renamePath(const QString &from, const QString &to) {
        if (!ensureOpen()) return;
        if (sftp_rename(m_sftp, from.toUtf8().constData(), to.toUtf8().constData()) != SSH_OK) {
            Q_EMIT failed(error(tr("Cannot rename %1").arg(from)));
        }
        Q_EMIT changed(parentOf(to));
    }

Q_SIGNALS:
    void opened(const QString &homePath);
    void listed(const QString &path, const QList<RemoteEntry> &entries);
    void failed(const QString &message);
    void changed(const QString &directory);
    void transferStarted(quint64 id, const QString &name, bool upload, qint64 size);
    void transferProgress(quint64 id, qint64 done, qint64 total);
    void transferFinished(quint64 id, bool ok, const QString &message);

private:
    SshOptions m_options;
    SshPrompter m_prompter;
    std::shared_ptr<std::atomic<bool>> m_stop;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    SshSessionPtr m_session;
    sftp_session m_sftp = nullptr;
    std::size_t m_readChunk = 32 * 1024;
    std::size_t m_writeChunk = 32 * 1024;
};

SftpClient::SftpClient(SshOptions options, SshPrompter prompter, QObject *parent)
    : QObject(parent), m_host(options.host),
      m_stop(std::make_shared<std::atomic<bool>>(false)),
      m_cancel(std::make_shared<std::atomic<bool>>(false)) {
    qRegisterMetaType<RemoteEntry>();
    qRegisterMetaType<QList<RemoteEntry>>();
    if (!prompter.guiContext) prompter.guiContext = this;

    m_worker = new SftpWorker(std::move(options), std::move(prompter), m_stop, m_cancel);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &SftpWorker::opened, this, &SftpClient::opened);
    connect(m_worker, &SftpWorker::listed, this, &SftpClient::listed);
    connect(m_worker, &SftpWorker::failed, this, &SftpClient::failed);
    connect(m_worker, &SftpWorker::changed, this, &SftpClient::changed);
    connect(m_worker, &SftpWorker::transferStarted, this, &SftpClient::transferStarted);
    connect(m_worker, &SftpWorker::transferProgress, this, &SftpClient::transferProgress);
    connect(m_worker, &SftpWorker::transferFinished, this, &SftpClient::transferFinished);
    m_thread.setObjectName(QStringLiteral("sftp"));
    m_thread.start();
}

SftpClient::~SftpClient() {
    m_stop->store(true);
    m_thread.quit();
    m_thread.wait();
}

void SftpClient::open() {
    QMetaObject::invokeMethod(m_worker, &SftpWorker::open, Qt::QueuedConnection);
}

void SftpClient::list(const QString &path) {
    QMetaObject::invokeMethod(m_worker, [w = m_worker, path] { w->list(path); }, Qt::QueuedConnection);
}

quint64 SftpClient::download(const QString &remotePath, const QString &localPath) {
    const quint64 id = m_nextId++;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, id, remotePath, localPath] { w->download(id, remotePath, localPath); },
                              Qt::QueuedConnection);
    return id;
}

quint64 SftpClient::upload(const QString &localPath, const QString &remotePath) {
    const quint64 id = m_nextId++;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, id, localPath, remotePath] { w->upload(id, localPath, remotePath); },
                              Qt::QueuedConnection);
    return id;
}

void SftpClient::makeDirectory(const QString &path) {
    QMetaObject::invokeMethod(m_worker, [w = m_worker, path] { w->makeDirectory(path); }, Qt::QueuedConnection);
}

void SftpClient::remove(const QString &path, bool isDirectory) {
    QMetaObject::invokeMethod(m_worker, [w = m_worker, path, isDirectory] { w->removePath(path, isDirectory); },
                              Qt::QueuedConnection);
}

void SftpClient::rename(const QString &from, const QString &to) {
    QMetaObject::invokeMethod(m_worker, [w = m_worker, from, to] { w->renamePath(from, to); }, Qt::QueuedConnection);
}

void SftpClient::cancelTransfers() {
    m_cancel->store(true);
}

#include "SftpClient.moc"
