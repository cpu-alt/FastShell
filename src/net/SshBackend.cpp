// Copyright 2026 FastShell contributors. Apache-2.0.
#include "net/SshBackend.h"

#include <algorithm>
#include <chrono>
#include <memory>

#include <libssh/libssh.h>

namespace {

constexpr int ReadTimeoutMs = 10;
constexpr int IdleReadTimeoutMs = 40;
constexpr int ReadBufferSize = 64 * 1024;
// Idle connections are dropped by firewalls and NAT after a few minutes:
// a keepalive keeps them open and reveals a dead link instead of a frozen tab.
constexpr auto KeepaliveInterval = std::chrono::seconds(30);

struct ChannelDeleter {
    void operator()(ssh_channel c) const {
        if (ssh_channel_is_open(c)) {
            ssh_channel_close(c);
        }
        ssh_channel_free(c);
    }
};
using Channel = std::unique_ptr<ssh_channel_struct, ChannelDeleter>;

}  // namespace

SshBackend::SshBackend(Options options, QObject *parent) : Backend(parent), m_options(std::move(options)) {}

SshBackend::~SshBackend() {
    m_stop = true;
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

SshOptions SshBackend::connectedOptions() const {
    QMutexLocker lock(&m_mutex);
    SshOptions options = m_options;
    if (!m_workingUser.isEmpty()) {
        options.user = m_workingUser;
    }
    if (!m_workingPassword.isEmpty()) {
        options.password = m_workingPassword;
    }
    options.legacyAlgorithms = options.legacyAlgorithms || m_usedLegacy;
    return options;
}

void SshBackend::start(int rows, int columns) {
    m_rows = rows;
    m_columns = columns;
    m_worker = std::thread([this] { run(); });
}

void SshBackend::write(const QByteArray &data) {
    QMutexLocker lock(&m_mutex);
    m_pendingWrite.append(data);
}

void SshBackend::resize(int rows, int columns) {
    QMutexLocker lock(&m_mutex);
    m_rows = rows;
    m_columns = columns;
    m_resizePending = true;
}

std::optional<QString> SshBackend::readLine(const QString &prompt, bool secret) {
    appendData(prompt.toUtf8());
    QByteArray line;
    // Arrows and other special keys arrive as escape sequences, possibly
    // split over several writes: skip them whole, never as text.
    enum class Escape { None, Start, Sequence } escape = Escape::None;
    while (!m_stop) {
        QByteArray input;
        {
            QMutexLocker lock(&m_mutex);
            input.swap(m_pendingWrite);
        }
        if (input.isEmpty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            continue;
        }
        for (qsizetype i = 0; i < input.size(); ++i) {
            const char c = input.at(i);
            if (escape == Escape::Start) {  // after ESC: "[" or "O" starts a sequence
                escape = (c == '[' || c == 'O') ? Escape::Sequence : Escape::None;
                continue;
            }
            if (escape == Escape::Sequence) {  // ends with a byte from '@' to '~'
                if (uchar(c) >= 0x40 && uchar(c) <= 0x7E) escape = Escape::None;
                continue;
            }
            if (c == 0x1b) {
                escape = Escape::Start;
                continue;
            }
            if (c == '\r' || c == '\n') {
                qsizetype rest = i + 1;
                if (c == '\r' && rest < input.size() && input.at(rest) == '\n') ++rest;
                if (rest < input.size()) {  // typed ahead (or pasted): keep it for the next prompt
                    QMutexLocker lock(&m_mutex);
                    m_pendingWrite.prepend(input.mid(rest));
                }
                appendData(QByteArray("\r\n"));
                return QString::fromUtf8(line);
            }
            if (c == 0x03 || c == 0x04) {  // Ctrl+C / Ctrl+D
                appendData(QByteArray("^C\r\n"));
                return std::nullopt;
            }
            if (c == 0x7f || c == 0x08) {  // Backspace: drop the last (UTF-8) character
                if (line.isEmpty()) continue;
                while (!line.isEmpty() && (uchar(line.back()) & 0xC0) == 0x80) line.chop(1);
                line.chop(1);
                if (!secret) appendData(QByteArray("\b \b"));
                continue;
            }
            if (uchar(c) < 0x20) continue;
            line.append(c);
            if (!secret) appendData(&c, 1);
        }
    }
    return std::nullopt;
}

void SshBackend::run() {
    SshPrompter prompter;
    prompter.guiContext = this;
    prompter.stop = &m_stop;
    prompter.confirm = confirm;
    prompter.ask = ask;
    prompter.askInTerminal = [this](const QString &prompt, bool secret) { return readLine(prompt, secret); };
    prompter.showInTerminal = [this](const QString &text) { appendData(text.toUtf8() + QByteArray("\r\n")); };

    SshConnectResult connection = sshConnect(m_options, prompter);
    if (!connection.session) {
        if (m_stop) return;
        if (connection.error.isEmpty()) {
            Q_EMIT finished(tr("Connection cancelled"));
        } else {
            Q_EMIT errorOccurred(connection.error);
        }
        return;
    }
    ssh_session s = connection.session.get();
    {
        QMutexLocker lock(&m_mutex);
        m_workingUser = connection.user;
        m_workingPassword = connection.password;
        m_usedLegacy = connection.usedLegacyAlgorithms;
    }
    // Old algorithms (SHA-1/CBC): no line in the terminal, the window shows
    // it on the tab tooltip and in the status bar (usedLegacyAlgorithms()).

    Channel channel(ssh_channel_new(s));
    int rows, columns;
    {
        QMutexLocker lock(&m_mutex);
        rows = m_rows;
        columns = m_columns;
        m_resizePending = false;
    }
    if (!channel || ssh_channel_open_session(channel.get()) != SSH_OK
        || ssh_channel_request_pty_size(channel.get(), "xterm-256color", columns, rows) != SSH_OK
        || ssh_channel_request_shell(channel.get()) != SSH_OK) {
        Q_EMIT errorOccurred(tr("Cannot open a shell: %1").arg(QString::fromUtf8(ssh_get_error(s))));
        return;
    }
    m_loggedIn = true;
    Q_EMIT connected();

    std::unique_ptr<char[]> buffer(new char[ReadBufferSize]);
    ssh_channel ch = channel.get();
    auto lastActivity = std::chrono::steady_clock::now();  // data in or out
    auto lastKeepalive = lastActivity;
    while (!m_stop) {
        QByteArray out;
        bool resize = false;
        {
            QMutexLocker lock(&m_mutex);
            out.swap(m_pendingWrite);
            resize = std::exchange(m_resizePending, false);
            rows = m_rows;
            columns = m_columns;
        }
        if (resize) {
            ssh_channel_change_pty_size(ch, columns, rows);
        }
        for (qsizetype offset = 0; offset < out.size();) {
            const int n = ssh_channel_write(ch, out.constData() + offset, uint32_t(out.size() - offset));
            if (n == SSH_ERROR) {
                break;
            }
            offset += n;
        }

        // Short waits while data flows (and right after a key); after a few
        // seconds of silence, longer ones: an idle session no longer wakes up
        // 100 times a second, and the first key still answers within 40 ms.
        const bool quiet = std::chrono::steady_clock::now() - lastActivity > std::chrono::seconds(3);
        const int n = ssh_channel_read_timeout(ch, buffer.get(), ReadBufferSize, 0, quiet ? IdleReadTimeoutMs : ReadTimeoutMs);
        if (n > 0) {
            appendData(buffer.get(), n);
        } else if (n == SSH_ERROR) {
            Q_EMIT errorOccurred(tr("Connection lost: %1").arg(QString::fromUtf8(ssh_get_error(s))));
            return;
        }
        const int e = ssh_channel_read_nonblocking(ch, buffer.get(), ReadBufferSize, 1);
        if (e > 0) {
            appendData(buffer.get(), e);
        }
        const auto now = std::chrono::steady_clock::now();
        if (n > 0 || e > 0 || !out.isEmpty()) {
            lastActivity = now;
        } else if (now - std::max(lastActivity, lastKeepalive) >= KeepaliveInterval) {
            lastKeepalive = now;
            if (ssh_send_ignore(s, "") == SSH_ERROR) {  // SSH_MSG_IGNORE: harmless traffic
                Q_EMIT errorOccurred(tr("Connection lost: %1").arg(QString::fromUtf8(ssh_get_error(s))));
                return;
            }
        }
        if (ssh_channel_is_eof(ch) || !ssh_channel_is_open(ch)) {
            break;
        }
    }
    if (!m_stop) {
        Q_EMIT finished(tr("Connection to %1 closed").arg(m_options.host));
    }
}
