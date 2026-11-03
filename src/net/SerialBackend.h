// Copyright 2026 WindTerm NG contributors. Apache-2.0.
//
// Serial console (COM port / USB-serial adapter), e.g. the console port of
// a switch or router.
#pragma once

#include <QSerialPort>

#include "core/Backend.h"

struct SerialOptions {
    QString port;  // "COM3", "/dev/ttyUSB0"
    qint32 baudRate = 9600;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    // Most network gear expects CR for Enter; some boards want CR LF.
    bool sendCrLf = false;

    // "9600 8N1" style summary.
    QString summary() const;
};

class SerialBackend final : public Backend {
    Q_OBJECT

public:
    explicit SerialBackend(SerialOptions options, QObject *parent = nullptr);
    ~SerialBackend() override;

    void start(int rows, int columns) override;
    void write(const QByteArray &data) override;
    void resize(int, int) override {}  // a serial line has no window size
    QString displayName() const override { return m_options.port; }

    // Sends a BREAK (used by Cisco ROMMON, password recovery...).
    void sendBreak();

private:
    SerialOptions m_options;
    QSerialPort *m_port;
};
