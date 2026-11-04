// Copyright 2026 FastShell contributors. Apache-2.0.
#include "net/TelnetBackend.h"

#include <QTcpSocket>

TelnetBackend::TelnetBackend(QString host, int port, QObject *parent)
    : Backend(parent), m_host(std::move(host)), m_port(port), m_socket(new QTcpSocket(this)) {
    m_engine = std::make_unique<telnet::Engine>([this](const QByteArray &reply) { m_socket->write(reply); });

    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_socket, &QTcpSocket::connected, this, [this] { m_engine->begin(); });
    connect(m_socket, &QTcpSocket::readyRead, this, [this] { appendData(m_engine->receive(m_socket->readAll())); });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::RemoteHostClosedError) {
            Q_EMIT finished(tr("Connection to %1 closed").arg(m_host));
        } else {
            Q_EMIT errorOccurred(m_socket->errorString());
        }
    });
}

TelnetBackend::~TelnetBackend() = default;

void TelnetBackend::start(int rows, int columns) {
    m_engine->setWindowSize(columns, rows);
    m_socket->connectToHost(m_host, quint16(m_port));
}

void TelnetBackend::write(const QByteArray &data) {
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(m_engine->encode(data));
    }
}

void TelnetBackend::resize(int rows, int columns) {
    m_engine->setWindowSize(columns, rows);
}
