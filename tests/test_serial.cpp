// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include <gtest/gtest.h>

#include "net/SerialBackend.h"

TEST(Serial, Summary) {
    SerialOptions o;
    EXPECT_EQ(o.summary(), QStringLiteral("9600 8N1"));
    o.baudRate = 115200;
    o.dataBits = QSerialPort::Data7;
    o.parity = QSerialPort::EvenParity;
    o.stopBits = QSerialPort::TwoStop;
    EXPECT_EQ(o.summary(), QStringLiteral("115200 7E2"));
}

TEST(Serial, MissingPortReportsAnError) {
    SerialOptions o;
    o.port = QStringLiteral("COM_DOES_NOT_EXIST_42");
    SerialBackend backend(o);
    QString error;
    QObject::connect(&backend, &Backend::errorOccurred, [&](const QString &e) { error = e; });
    backend.start(24, 80);
    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(error.contains(QStringLiteral("COM_DOES_NOT_EXIST_42")));
    backend.write("ignored when closed");  // must not crash
}
