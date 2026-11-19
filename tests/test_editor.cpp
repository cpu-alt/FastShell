// Copyright 2026 FastShell contributors. Apache-2.0.
// Built-in editor: files must come back byte for byte as they were.
#include <gtest/gtest.h>

#include "app/EditorText.h"

using namespace editor;

namespace {
const QByteArray Sample = "client switch-a41 {\n\tipaddr = 192.0.2.10\n\tsecret = test-only\n}\n";
}

TEST(EditorText, UnixFileRoundTrip) {
    const Decoded d = decode(Sample);
    EXPECT_FALSE(d.crlf);
    EXPECT_FALSE(d.bom);
    EXPECT_FALSE(d.binary);
    EXPECT_EQ(encode(d.text, d.crlf, d.bom, d.notUtf8), Sample);
}

TEST(EditorText, WindowsFileKeepsCrlf) {
    QByteArray crlf = Sample;
    crlf.replace("\n", "\r\n");
    const Decoded d = decode(crlf);
    EXPECT_TRUE(d.crlf);
    EXPECT_FALSE(d.text.contains(QLatin1Char('\r')));
    EXPECT_EQ(encode(d.text + QStringLiteral("x\n"), d.crlf, d.bom), crlf + "x\r\n");
}

TEST(EditorText, BomAndUtf8) {
    const QByteArray data = "\xEF\xBB\xBF# caf\xC3\xA9\n";
    const Decoded d = decode(data);
    EXPECT_TRUE(d.bom);
    EXPECT_EQ(d.text, QString::fromUtf8("# caf\xC3\xA9\n"));
    EXPECT_EQ(encode(d.text, d.crlf, d.bom, d.notUtf8), data);
}

TEST(EditorText, Latin1RoundTrip) {
    const QByteArray data = "# caf\xE9\n";
    const Decoded d = decode(data);
    EXPECT_TRUE(d.notUtf8);
    EXPECT_EQ(encode(d.text, d.crlf, d.bom, d.notUtf8), data);
}

TEST(EditorText, BinaryDetected) {
    EXPECT_TRUE(decode(QByteArray("ab\0cd", 5)).binary);
    EXPECT_FALSE(decode(QByteArray()).binary);
}

TEST(EditorText, FindWrapsAndIgnoresCase) {
    const QString t = QStringLiteral("client a {}\nCLIENT b {}\n");
    EXPECT_EQ(find(t, QStringLiteral("client"), 1, false, false), 12);
    EXPECT_EQ(find(t, QStringLiteral("client"), 13, false, false), 0);  // wraps
    EXPECT_EQ(find(t, QStringLiteral("client"), 13, false, true), 0);
    EXPECT_EQ(find(t, QStringLiteral("client"), 12, true, false), 0);   // previous
    EXPECT_EQ(find(t, QStringLiteral("client"), 0, true, false), 12);   // wraps back
    EXPECT_EQ(find(t, QStringLiteral("absent"), 0, false, false), -1);
}
