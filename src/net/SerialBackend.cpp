// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include "net/SerialBackend.h"

#include <QTimer>

QString SerialOptions::summary() const {
    QChar parityChar = QLatin1Char('N');
    switch (parity) {
    case QSerialPort::EvenParity: parityChar = QLatin1Char('E'); break;
    case QSerialPort::OddParity: parityChar = QLatin1Char('O'); break;
    case QSerialPort::SpaceParity: parityChar = QLatin1Char('S'); break;
    case QSerialPort::MarkParity: parityChar = QLatin1Char('M'); break;
    default: break;
    }
    const QString stop = stopBits == QSerialPort::OneAndHalfStop ? QStringLiteral("1.5")
                                                                 : QString::number(int(stopBits));
    return QStringLiteral("%1 %2%3%4").arg(baudRate).arg(int(dataBits)).arg(parityChar).arg(stop);
}

SerialBackend::SerialBackend(SerialOptions options, QObject *parent)
    : Backend(parent), m_options(std::move(options)), m_port(new QSerialPort(this)) {
    connect(m_port, &QSerialPort::readyRead, this, [this] { appendData(m_port->readAll()); });
    connect(m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError || error == QSerialPort::TimeoutError) return;
        const QString message = m_port->errorString();
        if (error == QSerialPort::ResourceError) {
            // Unplugged adapter or port taken away.
            m_port->close();
            Q_EMIT finished(tr("%1 disconnected: %2").arg(m_options.port, message));
        } else if (!m_port->isOpen()) {
            Q_EMIT errorOccurred(tr("Cannot open %1: %2").arg(m_options.port, message));
        }
    });
}

SerialBackend::~SerialBackend() {
    if (m_port->isOpen()) {
        m_port->close();
    }
}

void SerialBackend::start(int, int) {
    m_port->setPortName(m_options.port);
    m_port->setBaudRate(m_options.baudRate);
    m_port->setDataBits(m_options.dataBits);
    m_port->setParity(m_options.parity);
    m_port->setStopBits(m_options.stopBits);
    m_port->setFlowControl(m_options.flowControl);
    if (!m_port->open(QIODevice::ReadWrite)) {
        // errorOccurred() already reported it; make sure the view knows.
        if (m_port->error() == QSerialPort::NoError) {
            Q_EMIT errorOccurred(tr("Cannot open %1").arg(m_options.port));
        }
        return;
    }
    // Many devices need DTR/RTS raised to talk (USB adapters, some consoles).
    m_port->setDataTerminalReady(true);
    if (m_options.flowControl != QSerialPort::HardwareControl) {
        m_port->setRequestToSend(true);
    }
    const QString banner = tr("[Connected to %1, %2. Press Enter to wake up the console.]")
                               .arg(m_options.port, m_options.summary());
    appendData(QByteArray("\x1b[90m") + banner.toUtf8() + QByteArray("\x1b[0m\r\n"));
}

void SerialBackend::write(const QByteArray &data) {
    if (!m_port->isOpen()) return;
    if (m_options.sendCrLf) {
        QByteArray converted = data;
        converted.replace("\r", "\r\n");
        m_port->write(converted);
    } else {
        m_port->write(data);
    }
}

void SerialBackend::sendBreak() {
    if (!m_port->isOpen()) return;
    m_port->setBreakEnabled(true);
    QTimer::singleShot(300, this, [this] {
        if (m_port->isOpen()) m_port->setBreakEnabled(false);
    });
}
