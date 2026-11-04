// Copyright 2026 FastShell contributors. Apache-2.0.
#pragma once

#include <memory>

#include "core/Backend.h"
#include "core/TelnetProtocol.h"

class QTcpSocket;

class TelnetBackend final : public Backend {
    Q_OBJECT

public:
    TelnetBackend(QString host, int port, QObject *parent = nullptr);
    ~TelnetBackend() override;

    void start(int rows, int columns) override;
    void write(const QByteArray &data) override;
    void resize(int rows, int columns) override;
    QString displayName() const override { return m_host; }

private:
    QString m_host;
    int m_port;
    QTcpSocket *m_socket;
    std::unique_ptr<telnet::Engine> m_engine;
};
