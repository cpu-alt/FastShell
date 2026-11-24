// Copyright 2026 FastShell contributors. Apache-2.0.
//
// File server (TFTP + HTTP): paths kept in the folder, packets, real
// transfers on 127.0.0.1 with a small TFTP client.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QNetworkDatagram>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUdpSocket>

#include "net/FileServer.h"

using namespace fileserver;

namespace {

void ensureApp() {
    static int argc = 1;
    static char name[] = "fileserver-tests";
    static char *argv[] = {name, nullptr};
    if (!QCoreApplication::instance()) new QCoreApplication(argc, argv);
}

QByteArray pattern(int size) {
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i) data[i] = char((i * 7 + i / 251) & 0xFF);
    return data;
}

void writeFile(const QString &path, const QByteArray &data) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QByteArray request(quint16 opcode, const QByteArray &file, const QList<QPair<QByteArray, QByteArray>> &options = {}) {
    QByteArray p;
    p.append(char(0)).append(char(opcode)).append(file).append('\0').append("octet").append('\0');
    for (const auto &[k, v] : options) p.append(k).append('\0').append(v).append('\0');
    return p;
}

quint16 opcodeOf(const QByteArray &p) { return p.size() < 2 ? 0 : quint16((quint8(p[0]) << 8) | quint8(p[1])); }
quint16 blockOf(const QByteArray &p) { return p.size() < 4 ? 0 : quint16((quint8(p[2]) << 8) | quint8(p[3])); }

// Waits for a datagram while the server runs in the same event loop.
bool receive(QUdpSocket &socket, QNetworkDatagram &out, int ms = 3000) {
    const qint64 end = QDateTime::currentMSecsSinceEpoch() + ms;
    while (QDateTime::currentMSecsSinceEpoch() < end) {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        if (socket.hasPendingDatagrams()) {
            out = socket.receiveDatagram();
            return true;
        }
    }
    return false;
}

struct Fixture {
    QTemporaryDir dir;
    FileServer server;
    FileServer::Config config;
    Fixture() {
        ensureApp();
        config.root = dir.path();
        config.address = QHostAddress::LocalHost;
        config.tftpPort = 0;
        config.httpPort = 0;
    }
    bool start() { return server.start(config); }
};

// Downloads a file; returns the data, or the TFTP error text.
QByteArray download(quint16 port, const QByteArray &file, const QList<QPair<QByteArray, QByteArray>> &options,
                    int blockSize = 512, QByteArray *oack = nullptr, QString *error = nullptr) {
    QUdpSocket client;
    client.bind(QHostAddress::LocalHost, 0);
    client.writeDatagram(request(RRQ, file, options), QHostAddress::LocalHost, port);
    QByteArray data;
    quint16 expected = 1;
    QNetworkDatagram d;
    while (receive(client, d)) {
        const QByteArray p = d.data();
        if (opcodeOf(p) == ERROR) {
            if (error) *error = QString::fromUtf8(p.mid(4)).remove(QChar(0));
            return {};
        }
        if (opcodeOf(p) == OACK) {
            if (oack) *oack = p.mid(2);
            client.writeDatagram(ackPacket(0), d.senderAddress(), quint16(d.senderPort()));
            continue;
        }
        if (opcodeOf(p) != DATA || blockOf(p) != expected) continue;
        data += p.mid(4);
        client.writeDatagram(ackPacket(expected), d.senderAddress(), quint16(d.senderPort()));
        ++expected;
        if (p.size() - 4 < blockSize) return data;
    }
    if (error) *error = QStringLiteral("timeout");
    return {};
}

void waitIdle(FileServer &server) {
    const qint64 end = QDateTime::currentMSecsSinceEpoch() + 3000;
    while (server.runningTransfers() > 0 && QDateTime::currentMSecsSinceEpoch() < end) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

}  // namespace

TEST(FileServer, PathsStayInTheFolder) {
    ensureApp();
    QTemporaryDir dir;
    QDir(dir.path()).mkdir(QStringLiteral("sub"));
    writeFile(dir.filePath(QStringLiteral("s3900.bin")), "fw");
    writeFile(dir.filePath(QStringLiteral("sub/a.txt")), "a");
    const QString root = dir.path();

    EXPECT_FALSE(resolvePath(root, QStringLiteral("s3900.bin")).isEmpty());
    EXPECT_FALSE(resolvePath(root, QStringLiteral("/s3900.bin")).isEmpty());     // "tftp://pc/s3900.bin"
    EXPECT_FALSE(resolvePath(root, QStringLiteral("sub\\a.txt")).isEmpty());
    for (const char *bad : {"../s3900.bin", "sub/../../x", "..", "C:/Windows/win.ini", "C:\\Windows\\win.ini",
                            "s3900.bin:stream", "CON", "nul.txt", "sub/COM1", "a*b", "", "/", "./s3900.bin"}) {
        EXPECT_TRUE(resolvePath(root, QString::fromLatin1(bad)).isEmpty()) << bad;
    }
    // Writing: only new files, directly in the folder.
    EXPECT_TRUE(resolvePath(root, QStringLiteral("s3900.bin"), true).isEmpty());
    EXPECT_TRUE(resolvePath(root, QStringLiteral("sub/new.txt"), true).isEmpty());
    EXPECT_FALSE(resolvePath(root, QStringLiteral("backup.cfg"), true).isEmpty());
    EXPECT_TRUE(resolvePath(root + QStringLiteral("/missing"), QStringLiteral("s3900.bin")).isEmpty());
}

TEST(FileServer, ParsesRequests) {
    Request r;
    ASSERT_TRUE(parseRequest(request(RRQ, "s3900.bin", {{"BLKSIZE", "1468"}, {"tsize", "0"}}), r));
    EXPECT_EQ(r.opcode, RRQ);
    EXPECT_EQ(r.file, QStringLiteral("s3900.bin"));
    EXPECT_EQ(r.mode, QStringLiteral("octet"));
    EXPECT_EQ(r.options.value(QStringLiteral("blksize")), QStringLiteral("1468"));
    EXPECT_EQ(r.options.value(QStringLiteral("tsize")), QStringLiteral("0"));
    EXPECT_FALSE(parseRequest(QByteArray("\0\1abc", 5), r));               // no NUL at the end
    EXPECT_FALSE(parseRequest(QByteArray("\0\3\0\1data", 8), r));          // DATA is not a request
    EXPECT_FALSE(parseRequest(QByteArray("\0\1f\0mail\0", 10), r));        // unknown mode
    EXPECT_EQ(errorPacket(1, "File not found"), QByteArray("\0\5\0\1File not found\0", 19));
}

TEST(FileServer, TftpSendsAFile) {
    Fixture f;
    const QByteArray data = pattern(1500);
    writeFile(f.dir.filePath(QStringLiteral("s3900.bin")), data);
    ASSERT_TRUE(f.start());
    EXPECT_EQ(download(f.server.tftpPort(), "s3900.bin", {}), data);
    // Exact multiple of 512: ends with an empty block.
    writeFile(f.dir.filePath(QStringLiteral("k.bin")), pattern(1024));
    EXPECT_EQ(download(f.server.tftpPort(), "k.bin", {}), pattern(1024));
    waitIdle(f.server);
    const auto transfers = f.server.transfers();
    ASSERT_EQ(transfers.size(), 2);
    EXPECT_EQ(transfers[0].state, FileServer::Transfer::State::Done);
    EXPECT_EQ(transfers[0].bytes, 1500);
    EXPECT_EQ(transfers[0].total, 1500);
    EXPECT_EQ(transfers[0].file, QStringLiteral("s3900.bin"));
}

TEST(FileServer, TftpOptionsAndBlockWrap) {
    Fixture f;
    // 8-byte blocks: 75 000 blocks, so the block number wraps after 65535.
    const QByteArray data = pattern(600000);
    writeFile(f.dir.filePath(QStringLiteral("big.bin")), data);
    ASSERT_TRUE(f.start());
    QByteArray oack;
    const QByteArray got = download(f.server.tftpPort(), "big.bin", {{"blksize", "8"}, {"tsize", "0"}}, 8, &oack);
    EXPECT_EQ(oack, QByteArray("blksize\0" "8\0tsize\0" "600000\0", 23));
    EXPECT_EQ(got.size(), data.size());
    EXPECT_TRUE(got == data);
}

TEST(FileServer, TftpRefusals) {
    Fixture f;
    writeFile(f.dir.filePath(QStringLiteral("a.bin")), "x");
    ASSERT_TRUE(f.start());
    QString error;
    EXPECT_TRUE(download(f.server.tftpPort(), "missing.bin", {}, 512, nullptr, &error).isEmpty());
    EXPECT_EQ(error, QStringLiteral("File not found"));
    EXPECT_TRUE(download(f.server.tftpPort(), "../a.bin", {}, 512, nullptr, &error).isEmpty());
    EXPECT_EQ(error, QStringLiteral("File not found"));

    // Upload refused while not allowed.
    QUdpSocket client;
    client.bind(QHostAddress::LocalHost, 0);
    client.writeDatagram(request(WRQ, "cfg.txt"), QHostAddress::LocalHost, f.server.tftpPort());
    QNetworkDatagram d;
    ASSERT_TRUE(receive(client, d));
    EXPECT_EQ(opcodeOf(d.data()), ERROR);
    EXPECT_FALSE(QFile::exists(f.dir.filePath(QStringLiteral("cfg.txt"))));
}

TEST(FileServer, TftpResendsWhenTheAckIsLost) {
    Fixture f;
    writeFile(f.dir.filePath(QStringLiteral("a.bin")), pattern(700));
    f.server.retryMs = 100;
    ASSERT_TRUE(f.start());
    QUdpSocket client;
    client.bind(QHostAddress::LocalHost, 0);
    client.writeDatagram(request(RRQ, "a.bin"), QHostAddress::LocalHost, f.server.tftpPort());
    QNetworkDatagram first, again;
    ASSERT_TRUE(receive(client, first));
    ASSERT_TRUE(receive(client, again));  // no ACK: block 1 comes again
    EXPECT_EQ(first.data(), again.data());
    EXPECT_EQ(blockOf(again.data()), 1);
    // A packet from another port gets "Unknown transfer ID" and changes nothing.
    QUdpSocket intruder;
    intruder.bind(QHostAddress::LocalHost, 0);
    intruder.writeDatagram(ackPacket(1), QHostAddress::LocalHost, quint16(first.senderPort()));
    QNetworkDatagram e;
    ASSERT_TRUE(receive(intruder, e));
    EXPECT_EQ(opcodeOf(e.data()), ERROR);
    EXPECT_EQ(blockOf(e.data()), 5);
    f.server.stop();
    EXPECT_EQ(f.server.transfers().first().state, FileServer::Transfer::State::Failed);
}

TEST(FileServer, TftpUploadIntoANewFile) {
    Fixture f;
    f.config.allowUpload = true;
    writeFile(f.dir.filePath(QStringLiteral("exists.cfg")), "old");
    ASSERT_TRUE(f.start());
    QUdpSocket client;
    client.bind(QHostAddress::LocalHost, 0);
    client.writeDatagram(request(WRQ, "running.cfg"), QHostAddress::LocalHost, f.server.tftpPort());
    QNetworkDatagram d;
    ASSERT_TRUE(receive(client, d));
    ASSERT_EQ(opcodeOf(d.data()), ACK);
    EXPECT_EQ(blockOf(d.data()), 0);
    const QByteArray content = pattern(600);
    client.writeDatagram(dataPacket(1, content.left(512)), d.senderAddress(), quint16(d.senderPort()));
    ASSERT_TRUE(receive(client, d));
    EXPECT_EQ(blockOf(d.data()), 1);
    client.writeDatagram(dataPacket(2, content.mid(512)), d.senderAddress(), quint16(d.senderPort()));
    ASSERT_TRUE(receive(client, d));
    EXPECT_EQ(blockOf(d.data()), 2);
    waitIdle(f.server);
    QFile file(f.dir.filePath(QStringLiteral("running.cfg")));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), content);
    EXPECT_FALSE(QFile::exists(f.dir.filePath(QStringLiteral("running.cfg.part"))));

    // Never overwrites.
    client.writeDatagram(request(WRQ, "exists.cfg"), QHostAddress::LocalHost, f.server.tftpPort());
    ASSERT_TRUE(receive(client, d));
    EXPECT_EQ(opcodeOf(d.data()), ERROR);
    EXPECT_EQ(blockOf(d.data()), 6);
}

TEST(FileServer, HttpGetAndRefusals) {
    Fixture f;
    f.config.http = true;
    const QByteArray data = pattern(300000);
    writeFile(f.dir.filePath(QStringLiteral("fw 1.bin")), data);
    ASSERT_TRUE(f.start());
    auto get = [&](const QByteArray &path) {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, f.server.httpPort());
        socket.write("GET " + path + " HTTP/1.1\r\nHost: x\r\n\r\n");
        QByteArray all;
        const qint64 end = QDateTime::currentMSecsSinceEpoch() + 5000;
        while (socket.state() != QAbstractSocket::UnconnectedState && QDateTime::currentMSecsSinceEpoch() < end) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            socket.waitForReadyRead(1);
            all += socket.readAll();
        }
        return all + socket.readAll();
    };
    const QByteArray ok = get("/fw%201.bin");
    ASSERT_TRUE(ok.startsWith("HTTP/1.1 200 OK"));
    EXPECT_TRUE(ok.contains("Content-Length: 300000"));
    EXPECT_TRUE(ok.mid(ok.indexOf("\r\n\r\n") + 4) == data);
    EXPECT_TRUE(get("/").contains("fw%201.bin"));
    EXPECT_TRUE(get("/../secret").startsWith("HTTP/1.1 404"));
    EXPECT_TRUE(get("/%2e%2e/secret").startsWith("HTTP/1.1 404"));
    EXPECT_TRUE(get("/C:%5CWindows%5Cwin.ini").startsWith("HTTP/1.1 404"));
    waitIdle(f.server);
    ASSERT_FALSE(f.server.transfers().isEmpty());
    EXPECT_EQ(f.server.transfers().first().state, FileServer::Transfer::State::Done);
    EXPECT_EQ(f.server.transfers().first().bytes, 300000);
}
