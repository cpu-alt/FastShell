// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Common interface for everything a terminal tab can talk to: a local pty,
// an SSH channel or a Telnet socket.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <mutex>
#include <utility>

class Backend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~Backend() override = default;

    // Starts the session. Errors are reported through errorOccurred().
    virtual void start(int rows, int columns) = 0;
    virtual void write(const QByteArray &data) = 0;
    virtual void resize(int rows, int columns) = 0;
    virtual QString displayName() const = 0;

    // Returns and clears everything received since the last call.
    QByteArray readAll() {
        std::lock_guard lock(m_bufferMutex);
        return std::exchange(m_buffer, QByteArray());
    }

Q_SIGNALS:
    // Emitted once when data arrives into an empty buffer, not once per
    // chunk, so a busy session does not flood the event loop.
    void readyRead();
    void errorOccurred(const QString &message);
    void finished(const QString &reason);

protected:
    // Thread safe; may be called from a reader thread.
    void appendData(const char *data, qsizetype length) {
        if (length <= 0) {
            return;
        }
        bool wasEmpty;
        {
            std::lock_guard lock(m_bufferMutex);
            wasEmpty = m_buffer.isEmpty();
            m_buffer.append(data, length);
        }
        if (wasEmpty) {
            Q_EMIT readyRead();
        }
    }
    void appendData(const QByteArray &data) { appendData(data.constData(), data.size()); }

private:
    std::mutex m_bufferMutex;
    QByteArray m_buffer;
};
