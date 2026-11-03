// Copyright 2020 WindTerm, 2026 WindTerm NG contributors. Apache-2.0.
//
// POSIX pty without a reader thread: the master fd is watched by the event
// loop and drained in 64 KiB reads.
#ifndef _WIN32

#include "pty/UnixPty.h"

#include <QDir>
#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

UnixPty::UnixPty(Options options, QObject *parent) : Pty(std::move(options), parent) {}

UnixPty::~UnixPty() {
    stop();
}

void UnixPty::start(int rows, int columns) {
    stop();

    m_master = posix_openpt(O_RDWR | O_NOCTTY);
    if (m_master < 0 || grantpt(m_master) != 0 || unlockpt(m_master) != 0) {
        Q_EMIT errorOccurred(tr("Cannot open pseudo terminal: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        stop();
        return;
    }
    const QByteArray slaveName = ptsname(m_master);

    // Prepare everything that allocates before fork().
    QList<QByteArray> argStorage{m_options.program.toLocal8Bit()};
    for (const QString &arg : m_options.arguments) {
        argStorage << arg.toLocal8Bit();
    }
    std::vector<char *> argv;
    for (QByteArray &a : argStorage) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    const QByteArray directory =
        (m_options.workingDirectory.isEmpty() ? QDir::homePath() : m_options.workingDirectory).toLocal8Bit();
    QList<QByteArray> extraEnv;
    for (const QString &e : m_options.extraEnvironment) {
        extraEnv << e.toLocal8Bit();
    }
    winsize size{};
    size.ws_row = static_cast<unsigned short>(rows);
    size.ws_col = static_cast<unsigned short>(columns);

    m_child = fork();
    if (m_child < 0) {
        Q_EMIT errorOccurred(tr("fork() failed"));
        stop();
        return;
    }
    if (m_child == 0) {
        // Child: become session leader and take the slave as controlling tty.
        setsid();
        const int slave = open(slaveName.constData(), O_RDWR);
        if (slave < 0) {
            _exit(127);
        }
        ioctl(slave, TIOCSCTTY, 0);
        ioctl(slave, TIOCSWINSZ, &size);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) {
            close(slave);
        }
        close(m_master);
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("TERM_PROGRAM", "FastShell", 1);
        for (QByteArray &e : extraEnv) {
            putenv(e.data());
        }
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        if (chdir(directory.constData()) != 0) {
            (void)chdir("/");
        }
        execvp(argv[0], argv.data());
        _exit(127);
    }

    fcntl(m_master, F_SETFL, fcntl(m_master, F_GETFL) | O_NONBLOCK);
    fcntl(m_master, F_SETFD, FD_CLOEXEC);
    m_notifier = new QSocketNotifier(m_master, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &UnixPty::onReadable);
}

void UnixPty::onReadable() {
    char buffer[64 * 1024];
    for (;;) {
        const ssize_t n = ::read(m_master, buffer, sizeof buffer);
        if (n > 0) {
            appendData(buffer, std::size_t(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            return;
        }
        // EOF or EIO: the child closed the terminal.
        int status = 0;
        QString reason = tr("Process exited");
        if (m_child > 0 && waitpid(m_child, &status, 0) == m_child) {
            reason = WIFEXITED(status) ? tr("Process exited with code %1").arg(WEXITSTATUS(status))
                                       : tr("Process killed by signal %1").arg(WTERMSIG(status));
            m_child = -1;
        }
        stop();
        Q_EMIT finished(reason);
        return;
    }
}

void UnixPty::write(const QByteArray &data) {
    const char *p = data.constData();
    qsizetype remaining = data.size();
    while (m_master >= 0 && remaining > 0) {
        const ssize_t n = ::write(m_master, p, std::size_t(remaining));
        if (n > 0) {
            p += n;
            remaining -= n;
        } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
            return;
        }
    }
}

void UnixPty::resize(int rows, int columns) {
    if (m_master >= 0) {
        winsize size{};
        size.ws_row = static_cast<unsigned short>(rows);
        size.ws_col = static_cast<unsigned short>(columns);
        ioctl(m_master, TIOCSWINSZ, &size);
    }
}

void UnixPty::stop() {
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_child > 0) {
        kill(m_child, SIGHUP);
        waitpid(m_child, nullptr, WNOHANG);
        m_child = -1;
    }
    if (m_master >= 0) {
        close(m_master);
        m_master = -1;
    }
}

#endif  // !_WIN32
