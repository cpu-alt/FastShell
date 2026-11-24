// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Small file server to send a file from this PC to a network device, mostly
// for firmware updates: "copy tftp://10.1.4.250/s3900.bin flash:Switch.bin".
//
// - TFTP (RFC 1350) with the blksize / tsize / timeout options (RFC 2347-2349)
//   and block numbers that wrap after 65535 (files over 32 MB);
// - optional HTTP (GET / HEAD, one folder, simple listing);
// - read only by default; uploads (WRQ, "copy running-config tftp:") only when
//   allowed, into new files only (never overwrites);
// - every path stays inside the root folder (no "..", no drive, no ':' stream,
//   no Windows device name, symbolic links resolved).
#pragma once

#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>

class QTcpServer;
class QUdpSocket;

namespace fileserver {

// The file the client asked for, inside root; empty if refused. forWrite: a
// new file directly in root (it must not exist yet).
QString resolvePath(const QString &root, const QString &requested, bool forWrite = false);

enum Opcode : quint16 { RRQ = 1, WRQ = 2, DATA = 3, ACK = 4, ERROR = 5, OACK = 6 };

struct Request {
    quint16 opcode = 0;
    QString file;
    QString mode;                      // "octet", "netascii"
    QHash<QString, QString> options;   // names in lower case
};
bool parseRequest(const QByteArray &packet, Request &out);
QByteArray errorPacket(quint16 code, const QByteArray &message);
QByteArray dataPacket(quint16 block, const QByteArray &data);
QByteArray ackPacket(quint16 block);

}  // namespace fileserver

class TftpTransfer;
class HttpTransfer;

class FileServer : public QObject {
    Q_OBJECT

public:
    struct Config {
        QString root;
        QHostAddress address = QHostAddress::AnyIPv4;
        quint16 tftpPort = 69;     // 0 = any free port (tests)
        bool http = false;         // also serve the folder over HTTP
        quint16 httpPort = 8080;   // 0 = any free port (tests)
        bool allowUpload = false;  // TFTP WRQ into new files
    };

    struct Transfer {
        int id = 0;
        QString protocol;  // "tftp", "http"
        QString file;
        QString peer;
        bool upload = false;
        qint64 bytes = 0;
        qint64 total = -1;  // -1: unknown
        enum class State { Running, Done, Failed } state = State::Running;
        QString error;
        QDateTime started;
        QDateTime finished;
    };

    explicit FileServer(QObject *parent = nullptr);
    ~FileServer() override;

    bool start(const Config &config, QString *error = nullptr);
    void stop();
    bool isRunning() const { return m_running; }
    const Config &config() const { return m_config; }
    quint16 tftpPort() const;  // the real port (useful with port 0 in tests)
    quint16 httpPort() const;

    QList<Transfer> transfers() const;  // the last 50, oldest first
    int runningTransfers() const;
    // State, addresses, files, the command to type and the last transfers (for the user and Claude).
    QString summary() const;
    static QList<QPair<QString, QHostAddress>> localAddresses();  // (interface, IPv4), up, not loopback
    static QString sizeText(qint64 bytes);

    int retryMs = 1000;  // TFTP: resend after this long without an answer (option "timeout" overrides it)
    int retries = 5;

Q_SIGNALS:
    void runningChanged(bool running);
    void transferStarted(int id);
    void transferFinished(int id);

private:
    friend class TftpTransfer;
    friend class HttpTransfer;
    void onTftpRequest();
    void onHttpConnection();
    int addTransfer(Transfer transfer);
    Transfer *transfer(int id);
    void finish(int id, const QString &error);

    Config m_config;
    bool m_running = false;
    QUdpSocket *m_tftp = nullptr;
    QTcpServer *m_http = nullptr;
    QList<Transfer> m_transfers;
    int m_nextId = 1;
};
