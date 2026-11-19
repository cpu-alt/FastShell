// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/EditorText.h"

#include <QStringDecoder>

namespace editor {

Decoded decode(const QByteArray &data) {
    Decoded d;
    QByteArray bytes = data;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        d.bom = true;
        bytes.remove(0, 3);
    }
    d.binary = bytes.contains('\0');
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    d.text = utf8(bytes);
    if (utf8.hasError()) {
        d.notUtf8 = true;
        d.text = QString::fromLatin1(bytes);
    }
    // The file is CRLF when most of its lines are.
    const qsizetype lf = d.text.count(QLatin1Char('\n'));
    const qsizetype crlf = d.text.count(QStringLiteral("\r\n"));
    d.crlf = lf > 0 && crlf * 2 > lf;
    if (d.crlf) d.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return d;
}

QByteArray encode(const QString &text, bool crlf, bool bom, bool latin1) {
    QString t = text;
    if (crlf) t.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    QByteArray out = latin1 ? t.toLatin1() : t.toUtf8();
    if (bom && !latin1) out.prepend("\xEF\xBB\xBF");
    return out;
}

qsizetype find(const QString &text, const QString &needle, qsizetype from, bool backward, bool caseSensitive) {
    if (needle.isEmpty() || text.isEmpty()) return -1;
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    from = qBound<qsizetype>(0, from, text.size());
    if (backward) {
        qsizetype i = from > 0 ? text.lastIndexOf(needle, from - 1, cs) : -1;
        return i >= 0 ? i : text.lastIndexOf(needle, -1, cs);
    }
    const qsizetype i = text.indexOf(needle, from, cs);
    return i >= 0 ? i : text.indexOf(needle, 0, cs);
}

}  // namespace editor
