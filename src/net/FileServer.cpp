// Copyright 2026 FastShell contributors. Apache-2.0.
#include "net/FileServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>

#include <algorithm>

namespace fileserver {

namespace {
bool insideRoot(const QString &canonicalRoot, const QString &canonicalPath) {
    if (canonicalRoot.isEmpty() || canonicalPath.isEmpty()) return false;
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    const QString prefix = canonicalRoot.endsWith(QLatin1Char('/')) ? canonicalRoot : canonicalRoot + QLatin1Char('/');
    return canonicalPath.startsWith(prefix, cs);
}

bool safeSegment(const QString &segment) {
    static const QRegularExpression device(QStringLiteral("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\\..*)?$"),
                                           QRegularExpression::CaseInsensitiveOption);
    if (segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String("..")) return false;
    if (device.match(segment.trimmed()).hasMatch()) return false;
    for (QChar c : segment) {
        if (c.unicode() < 0x20 || c == QLatin1Char(':') || c == QLatin1Char('*') || c == QLatin1Char('?') ||
            c == QLatin1Char('"') || c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('|') ||
            c.category() == QChar::Other_Format) {
            return false;
        }
    }
    return true;
}
}  // namespace

QString resolvePath(const QString &root, const QString &requested, bool forWrite) {
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalRoot.isEmpty() || !QFileInfo(canonicalRoot).isDir()) return {};
    QString name = requested;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QLatin1Char('/'))) name.remove(0, 1);
    if (name.isEmpty() || name.size() > 512) return {};
    const QStringList segments = name.split(QLatin1Char('/'));
    for (const QString &s : segments) {
        if (!safeSegment(s)) return {};
    }
    if (forWrite) {
        if (segments.size() != 1) return {};  // only directly in the root folder
        const QString path = canonicalRoot + QLatin1Char('/') + segments.first();
        if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) return {};
        return path;
    }
    const QString canonical = QFileInfo(canonicalRoot + QLatin1Char('/') + segments.join(QLatin1Char('/'))).canonicalFilePath();
    if (!insideRoot(canonicalRoot, canonical)) return {};
    return canonical;
}

bool parseRequest(const QByteArray &packet, Request &out) {
    if (packet.size() < 4) return false;
    out = Request();
    out.opcode = quint16((quint8(packet[0]) << 8) | quint8(packet[1]));
    if (out.opcode != RRQ && out.opcode != WRQ) return false;
    QList<QByteArray> fields = packet.mid(2).split('\0');
    if (fields.isEmpty() || !packet.endsWith('\0')) return false;
    fields.removeLast();  // after the last NUL
    if (fields.size() < 2 || fields.size() % 2 != 0) return false;
    out.file = QString::fromUtf8(fields[0]);
    out.mode = QString::fromLatin1(fields[1]).toLower();
    if (out.file.isEmpty() || (out.mode != QLatin1String("octet") && out.mode != QLatin1String("netascii"))) return false;
    for (int i = 2; i + 1 < fields.size(); i += 2) {
        out.options.insert(QString::fromLatin1(fields[i]).toLower(), QString::fromLatin1(fields[i + 1]));
    }
    return true;
}

QByteArray errorPacket(quint16 code, const QByteArray &message) {
    QByteArray p;
    p.append(char(0)).append(char(ERROR)).append(char(code >> 8)).append(char(code & 0xFF));
    p.append(message).append('\0');
    return p;
}

QByteArray dataPacket(quint16 block, const QByteArray &data) {
    QByteArray p;
    p.reserve(4 + data.size());
    p.append(char(0)).append(char(DATA)).append(char(block >> 8)).append(char(block & 0xFF));
    p.append(data);
    return p;
}

QByteArray ackPacket(quint16 block) {
    QByteArray p;
    p.append(char(0)).append(char(ACK)).append(char(block >> 8)).append(char(block & 0xFF));
    return p;
}

}  // namespace fileserver

using namespace fileserver;

// --- TFTP: one transfer, on its own UDP port (the TFTP "transfer ID") ----------

class TftpTransfer : public QObject {
public:
    TftpTransfer(FileServer *server, int id, const QNetworkDatagram &request, const Request &req, const QString &path)
        : QObject(server), m_server(server), m_id(id), m_peer(request.senderAddress()), m_peerPort(quint16(request.senderPort())),
          m_upload(req.opcode == WRQ), m_file(path), m_timer(new QTimer(this)), m_retryMs(server->retryMs) {
        m_socket = new QUdpSocket(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, &TftpTransfer::onTimeout);
        connect(m_socket, &QUdpSocket::readyRead, this, &TftpTransfer::onRead);
        if (!m_socket->bind(server->config().address, 0)) {
            fail(0, QStringLiteral("Cannot open a UDP port: %1").arg(m_socket->errorString()), false);
            return;
        }
        if (m_upload) {
            m_final = path;
            m_file.setFileName(path + QStringLiteral(".part"));
            if (QFileInfo::exists(m_file.fileName()) || !m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                fail(2, QStringLiteral("Cannot create the file."));
                return;
            }
        } else if (!m_file.open(QIODevice::ReadOnly)) {
            fail(2, QStringLiteral("Cannot read the file."));
            return;
        }
        if (FileServer::Transfer *t = m_server->transfer(m_id)) t->total = m_upload ? -1 : m_file.size();

        // Options (RFC 2347): answered with an OACK, then the client sends ACK 0 (read) / DATA 1 (write).
        QByteArray oack;
        auto option = [&oack](const char *name, const QByteArray &value) { oack.append(name).append('\0').append(value).append('\0'); };
        bool ok = false;
        if (req.options.contains(QStringLiteral("blksize"))) {
            const int size = req.options.value(QStringLiteral("blksize")).toInt(&ok);
            if (ok && size >= 8) {
                m_blockSize = std::min(size, 65464);
                option("blksize", QByteArray::number(m_blockSize));
            }
        }
        if (req.options.contains(QStringLiteral("timeout"))) {
            const int seconds = req.options.value(QStringLiteral("timeout")).toInt(&ok);
            if (ok && seconds >= 1 && seconds <= 255) {
                m_retryMs = seconds * 1000;
                option("timeout", QByteArray::number(seconds));
            }
        }
        if (req.options.contains(QStringLiteral("tsize"))) {
            if (m_upload) {
                const qint64 size = req.options.value(QStringLiteral("tsize")).toLongLong(&ok);
                if (ok) {
                    if (FileServer::Transfer *t = m_server->transfer(m_id)) t->total = size;
                    option("tsize", QByteArray::number(size));
                }
            } else {
                option("tsize", QByteArray::number(m_file.size()));
            }
        }
        if (!oack.isEmpty()) {
            sendPacket(QByteArray().append(char(0)).append(char(OACK)) + oack);
            m_block = 0;  // read: waiting for ACK 0; write: waiting for DATA 1
        } else if (m_upload) {
            sendPacket(ackPacket(0));
        } else {
            sendNext();
        }
    }

private:
    void sendPacket(const QByteArray &packet) {
        m_last = packet;
        m_tries = 0;
        m_socket->writeDatagram(packet, m_peer, m_peerPort);
        m_timer->start(m_retryMs);
    }

    void sendNext() {
        const QByteArray data = m_file.read(m_blockSize);
        if (data.isNull() && m_file.error() != QFileDevice::NoError) {
            fail(0, QStringLiteral("Read error: %1").arg(m_file.errorString()));
            return;
        }
        m_started = true;
        ++m_block;  // wraps from 65535 to 0, like most clients expect
        m_lastSize = int(data.size());
        sendPacket(dataPacket(m_block, data));
    }

    void onRead() {
        while (m_socket->hasPendingDatagrams()) {
            const QNetworkDatagram d = m_socket->receiveDatagram();
            if (m_done) continue;
            if (d.senderAddress().toIPv4Address() != m_peer.toIPv4Address() || quint16(d.senderPort()) != m_peerPort) {
                m_socket->writeDatagram(errorPacket(5, "Unknown transfer ID"), d.senderAddress(), quint16(d.senderPort()));
                continue;
            }
            const QByteArray p = d.data();
            if (p.size() < 4) continue;
            const quint16 opcode = quint16((quint8(p[0]) << 8) | quint8(p[1]));
            const quint16 block = quint16((quint8(p[2]) << 8) | quint8(p[3]));
            if (opcode == ERROR) {
                fail(0, QStringLiteral("Cancelled by the device: %1").arg(QString::fromUtf8(p.mid(4)).remove(QChar(0))), false);
                return;
            }
            if (!m_upload && opcode == ACK) {
                if (block != m_block) continue;  // old duplicate: ignore (no "sorcerer's apprentice")
                if (m_started) progress(m_lastSize);
                if (m_started && m_lastSize < m_blockSize) {
                    succeed();
                    return;
                }
                sendNext();
            } else if (m_upload && opcode == DATA) {
                const quint16 expected = quint16(m_block + 1);
                if (block == m_block) {  // our ACK was lost: send it again
                    m_socket->writeDatagram(ackPacket(block), m_peer, m_peerPort);
                    continue;
                }
                if (block != expected) continue;
                const QByteArray data = p.mid(4);
                if (data.size() > m_blockSize) {
                    fail(4, QStringLiteral("Block too large."));
                    return;
                }
                if (m_file.write(data) != data.size()) {
                    fail(3, QStringLiteral("Disk full or write error."));
                    return;
                }
                m_block = expected;
                progress(data.size());
                sendPacket(ackPacket(m_block));
                if (data.size() < m_blockSize) {
                    m_file.close();
                    if (!QFile::rename(m_file.fileName(), m_final)) {
                        QFile::remove(m_file.fileName());
                        finish(QStringLiteral("Cannot rename the received file."));
                        return;
                    }
                    succeed();  // the last ACK may be lost: the device retries on its side
                    return;
                }
            }
        }
    }

    void onTimeout() {
        if (m_done) return;
        if (++m_tries > m_server->retries) {
            fail(0, QStringLiteral("Timeout: the device stopped answering."));
            return;
        }
        m_socket->writeDatagram(m_last, m_peer, m_peerPort);
        m_timer->start(m_retryMs);
    }

    void progress(qint64 n) {
        if (FileServer::Transfer *t = m_server->transfer(m_id)) t->bytes += n;
    }

    void succeed() { finish(QString()); }

    void fail(quint16 code, const QString &message, bool tell = true) {
        if (tell && m_socket->state() == QAbstractSocket::BoundState) {
            m_socket->writeDatagram(errorPacket(code, message.toUtf8()), m_peer, m_peerPort);
        }
        if (m_upload && m_file.isOpen()) {
            m_file.close();
            m_file.remove();
        }
        finish(message);
    }

    void finish(const QString &error) {
        if (m_done) return;
        m_done = true;
        m_timer->stop();
        if (m_file.isOpen()) m_file.close();
        m_server->finish(m_id, error);
        deleteLater();
    }

    FileServer *m_server;
    int m_id;
    QHostAddress m_peer;
    quint16 m_peerPort;
    bool m_upload;
    QFile m_file;
    QString m_final;
    QUdpSocket *m_socket = nullptr;
    QTimer *m_timer;
    QByteArray m_last;
    int m_retryMs;
    int m_tries = 0;
    int m_blockSize = 512;
    int m_lastSize = 0;
    quint16 m_block = 0;
    bool m_started = false;  // read: the first DATA was sent
    bool m_done = false;
};

// --- HTTP: one connection, GET or HEAD, then close ------------------------------

class HttpTransfer : public QObject {
public:
    HttpTransfer(FileServer *server, QTcpSocket *socket) : QObject(server), m_server(server), m_socket(socket) {
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, &HttpTransfer::onRead);
        connect(socket, &QTcpSocket::bytesWritten, this, &HttpTransfer::pump);
        connect(socket, &QTcpSocket::disconnected, this, [this] {
            if (m_id && !m_done) end(QStringLiteral("The client closed the connection."));
            deleteLater();
        });
        QTimer::singleShot(30000, this, [this] {
            if (m_header) return;
            m_socket->abort();
            deleteLater();
        });
    }

private:
    void onRead() {
        if (m_header) return;
        m_buffer += m_socket->readAll();
        const qsizetype end = m_buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (m_buffer.size() > 8192) reply(431, "Request header too large");
            return;
        }
        m_header = true;
        const QList<QByteArray> parts = m_buffer.left(m_buffer.indexOf("\r\n")).split(' ');
        if (parts.size() != 3 || !parts[2].startsWith("HTTP/1.")) return reply(400, "Bad request");
        const QByteArray method = parts[0];
        m_head = method == "HEAD";
        if (method != "GET" && !m_head) return reply(405, "Method not allowed");
        QByteArray target = parts[1];
        if (const qsizetype q = target.indexOf('?'); q >= 0) target.truncate(q);
        const QString name = QUrl::fromPercentEncoding(target);
        const QString root = QFileInfo(m_server->config().root).canonicalFilePath();
        QString path;
        if (name == QLatin1String("/") || name.isEmpty()) path = root;
        else path = resolvePath(m_server->config().root, name);
        if (path.isEmpty()) return reply(404, "Not found");
        if (QFileInfo(path).isDir()) return listing(path, name);

        m_file.setFileName(path);
        if (!m_file.open(QIODevice::ReadOnly)) return reply(403, "Forbidden");
        FileServer::Transfer t;
        t.protocol = QStringLiteral("http");
        t.file = QDir(root).relativeFilePath(path);
        t.peer = m_socket->peerAddress().toString();
        t.total = m_file.size();
        if (!m_head) m_id = m_server->addTransfer(t);
        m_socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                        QByteArray::number(m_file.size()) + "\r\nConnection: close\r\n\r\n");
        if (m_head) {
            m_done = true;
            m_socket->disconnectFromHost();
            return;
        }
        pump();
    }

    void pump() {
        if (!m_id || m_done) return;
        while (m_socket->bytesToWrite() < 256 * 1024) {
            const QByteArray chunk = m_file.read(64 * 1024);
            if (chunk.isEmpty()) {
                if (m_socket->bytesToWrite() == 0) {
                    end(QString());
                    m_socket->disconnectFromHost();
                }
                return;
            }
            m_socket->write(chunk);
            if (FileServer::Transfer *t = m_server->transfer(m_id)) t->bytes += chunk.size();
        }
    }

    void listing(const QString &dir, const QString &name) {
        QString base = name;
        if (!base.endsWith(QLatin1Char('/'))) base += QLatin1Char('/');
        QString html = QStringLiteral("<!doctype html><meta charset=utf-8><title>FastShell</title><h3>%1</h3><ul>").arg(base.toHtmlEscaped());
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        for (const QFileInfo &e : entries) {
            const QString label = e.fileName() + (e.isDir() ? QStringLiteral("/") : QString());
            html += QStringLiteral("<li><a href=\"%1\">%2</a> %3</li>")
                        .arg(QString::fromLatin1(QUrl::toPercentEncoding(e.fileName())) + (e.isDir() ? QStringLiteral("/") : QString()),
                             label.toHtmlEscaped(), e.isDir() ? QString() : QString::number(e.size()));
        }
        html += QStringLiteral("</ul>");
        reply(200, html.toUtf8(), "text/html; charset=utf-8");
    }

    void reply(int status, const QByteArray &body, const QByteArray &type = "text/plain") {
        m_header = true;
        m_done = true;
        QByteArray text = status == 200 ? "OK" : body;
        m_socket->write("HTTP/1.1 " + QByteArray::number(status) + ' ' + text + "\r\nContent-Type: " + type +
                        "\r\nContent-Length: " + QByteArray::number(m_head ? 0 : body.size()) + "\r\nConnection: close\r\n\r\n");
        if (!m_head) m_socket->write(body);
        m_socket->disconnectFromHost();
    }

    void end(const QString &error) {
        if (m_done) return;
        m_done = true;
        m_file.close();
        m_server->finish(m_id, error);
    }

    FileServer *m_server;
    QTcpSocket *m_socket;
    QByteArray m_buffer;
    QFile m_file;
    int m_id = 0;
    bool m_header = false;
    bool m_head = false;
    bool m_done = false;
};

// --- FileServer -----------------------------------------------------------------

FileServer::FileServer(QObject *parent) : QObject(parent) {}

FileServer::~FileServer() { stop(); }

bool FileServer::start(const Config &config, QString *error) {
    stop();
    auto failed = [&](const QString &message) {
        if (error) *error = message;
        stop();
        return false;
    };
    if (!QFileInfo(config.root).isDir()) return failed(tr("The folder does not exist."));
    m_config = config;
    m_tftp = new QUdpSocket(this);
    if (!m_tftp->bind(config.address, config.tftpPort)) {
        return failed(tr("TFTP: cannot listen on %1 port %2 (%3).").arg(config.address.toString()).arg(config.tftpPort).arg(m_tftp->errorString()));
    }
    connect(m_tftp, &QUdpSocket::readyRead, this, &FileServer::onTftpRequest);
    if (config.http) {
        m_http = new QTcpServer(this);
        if (!m_http->listen(config.address, config.httpPort)) {
            return failed(tr("HTTP: cannot listen on %1 port %2 (%3).").arg(config.address.toString()).arg(config.httpPort).arg(m_http->errorString()));
        }
        connect(m_http, &QTcpServer::newConnection, this, &FileServer::onHttpConnection);
    }
    m_running = true;
    Q_EMIT runningChanged(true);
    return true;
}

void FileServer::stop() {
    const bool was = m_running;
    m_running = false;
    // Running transfers end with the server.
    const QObjectList list = children();  // a copy: deleting changes children()
    for (QObject *child : list) {
        if (dynamic_cast<TftpTransfer *>(child) || dynamic_cast<HttpTransfer *>(child)) delete child;
    }
    for (Transfer &t : m_transfers) {
        if (t.state == Transfer::State::Running) {
            t.state = Transfer::State::Failed;
            t.error = tr("Server stopped.");
            t.finished = QDateTime::currentDateTime();
        }
    }
    delete m_tftp;
    m_tftp = nullptr;
    delete m_http;
    m_http = nullptr;
    if (was) Q_EMIT runningChanged(false);
}

quint16 FileServer::tftpPort() const { return m_tftp ? m_tftp->localPort() : 0; }
quint16 FileServer::httpPort() const { return m_http ? m_http->serverPort() : 0; }

QList<FileServer::Transfer> FileServer::transfers() const { return m_transfers; }

int FileServer::runningTransfers() const {
    return int(std::count_if(m_transfers.cbegin(), m_transfers.cend(), [](const Transfer &t) { return t.state == Transfer::State::Running; }));
}

void FileServer::onTftpRequest() {
    while (m_tftp && m_tftp->hasPendingDatagrams()) {
        const QNetworkDatagram d = m_tftp->receiveDatagram();
        const QHostAddress peer = d.senderAddress();
        const quint16 port = quint16(d.senderPort());
        Request req;
        if (!parseRequest(d.data(), req)) {
            m_tftp->writeDatagram(errorPacket(4, "Illegal TFTP operation"), peer, port);
            continue;
        }
        if (req.opcode == WRQ && !m_config.allowUpload) {
            m_tftp->writeDatagram(errorPacket(2, "Uploads are not allowed"), peer, port);
            continue;
        }
        const QString path = resolvePath(m_config.root, req.file, req.opcode == WRQ);
        if (path.isEmpty()) {
            if (req.opcode == WRQ) m_tftp->writeDatagram(errorPacket(6, "File already exists or bad name"), peer, port);
            else m_tftp->writeDatagram(errorPacket(1, "File not found"), peer, port);
            continue;
        }
        if (req.opcode == RRQ && !QFileInfo(path).isFile()) {
            m_tftp->writeDatagram(errorPacket(1, "File not found"), peer, port);
            continue;
        }
        Transfer t;
        t.protocol = QStringLiteral("tftp");
        t.file = req.opcode == WRQ ? QFileInfo(path).fileName() : QDir(QFileInfo(m_config.root).canonicalFilePath()).relativeFilePath(path);
        t.peer = QHostAddress(peer.toIPv4Address()).toString();
        t.upload = req.opcode == WRQ;
        const int id = addTransfer(t);
        new TftpTransfer(this, id, d, req, path);
    }
}

void FileServer::onHttpConnection() {
    while (m_http && m_http->hasPendingConnections()) new HttpTransfer(this, m_http->nextPendingConnection());
}

int FileServer::addTransfer(Transfer transfer) {
    transfer.id = m_nextId++;
    transfer.started = QDateTime::currentDateTime();
    m_transfers << transfer;
    // Keep the last 50 (running ones are never dropped: they are the newest).
    while (m_transfers.size() > 50 && m_transfers.first().state != Transfer::State::Running) m_transfers.removeFirst();
    Q_EMIT transferStarted(transfer.id);
    return transfer.id;
}

FileServer::Transfer *FileServer::transfer(int id) {
    for (Transfer &t : m_transfers) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

void FileServer::finish(int id, const QString &error) {
    Transfer *t = transfer(id);
    if (!t || t->state != Transfer::State::Running) return;
    t->state = error.isEmpty() ? Transfer::State::Done : Transfer::State::Failed;
    t->error = error;
    t->finished = QDateTime::currentDateTime();
    Q_EMIT transferFinished(id);
}

// --- text for the user and for Claude ---------------------------------------------

QString FileServer::sizeText(qint64 bytes) {
    if (bytes < 0) return QStringLiteral("?");
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(double(bytes) / 1024, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024 * 1024), 0, 'f', 1);
}

QList<QPair<QString, QHostAddress>> FileServer::localAddresses() {
    QList<QPair<QString, QHostAddress>> list;
    for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp) || (i.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry &e : i.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) list.append({i.humanReadableName(), e.ip()});
        }
    }
    return list;
}

QString FileServer::summary() const {
    QStringList out;
    if (!m_running) {
        out << QStringLiteral("File server stopped.");
    } else {
        QString line = QStringLiteral("TFTP on %1 port %2").arg(m_config.address.toString()).arg(tftpPort());
        if (m_http) line += QStringLiteral(", HTTP port %1").arg(httpPort());
        line += QStringLiteral(", folder \"%1\", uploads %2.").arg(QDir::toNativeSeparators(m_config.root),
                                                                   m_config.allowUpload ? QStringLiteral("allowed (new files only)") : QStringLiteral("off"));
        out << line;
        QString ip = m_config.address.toString();
        if (m_config.address == QHostAddress(QHostAddress::AnyIPv4) || m_config.address == QHostAddress(QHostAddress::Any)) {
            QStringList addresses;
            for (const auto &[name, address] : localAddresses()) addresses << address.toString() + QStringLiteral(" (") + name + QLatin1Char(')');
            out << QStringLiteral("Addresses of this PC: ") + (addresses.isEmpty() ? QStringLiteral("none") : addresses.join(QStringLiteral(", ")));
            ip = QStringLiteral("<PC address in the device's network>");
        }
        const QFileInfoList files = QDir(m_config.root).entryInfoList(QDir::Files, QDir::Time);
        QStringList names;
        for (const QFileInfo &f : files.mid(0, 30)) names << f.fileName() + QStringLiteral(" (") + sizeText(f.size()) + QLatin1Char(')');
        if (files.size() > 30) names << QStringLiteral("... %1 more").arg(files.size() - 30);
        out << QStringLiteral("Files: ") + (names.isEmpty() ? QStringLiteral("none") : names.join(QStringLiteral(", ")));
        out << QStringLiteral("On the device, for example: copy tftp://%1/%2 flash:<name>  (FS S3900: copy tftp:<file> "
                              "flash:Switch.bin, then the PC address when asked)")
                   .arg(ip, files.isEmpty() ? QStringLiteral("<file>")
                                        : std::max_element(files.cbegin(), files.cend(), [](const QFileInfo &a, const QFileInfo &b) {
                                              return a.size() < b.size();  // the firmware, not a small config
                                          })->fileName());
    }
    const QList<Transfer> list = m_transfers.mid(std::max<qsizetype>(0, m_transfers.size() - 10));
    for (const Transfer &t : list) {
        QString state;
        switch (t.state) {
        case Transfer::State::Running:
            state = t.total > 0 ? QStringLiteral("running %1%").arg(t.bytes * 100 / t.total) : QStringLiteral("running");
            break;
        case Transfer::State::Done:
            state = QStringLiteral("done in %1 s").arg(std::max<qint64>(1, t.started.secsTo(t.finished)));
            break;
        case Transfer::State::Failed: state = QStringLiteral("FAILED: ") + t.error; break;
        }
        out << QStringLiteral("#%1 %2 %3 %4 %5 %6/%7 %8")
                   .arg(t.id)
                   .arg(t.protocol, t.file, t.upload ? QStringLiteral("<-") : QStringLiteral("->"), t.peer, sizeText(t.bytes),
                        sizeText(t.total), state);
    }
    return out.join(QLatin1Char('\n'));
}
