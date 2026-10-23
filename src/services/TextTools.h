#pragma once

#include <QJsonDocument>
#include <QJsonParseError>
#include <QString>
#include <QUrl>

// Quick text transforms for the note editor.
struct TextResult {
    bool ok;
    QString text;  // result, or the input unchanged on failure
    QString error;
};

// Re-indents already-validated JSON as written. QJsonDocument would sort object keys, which is
// not what anyone formatting an API response expects.
inline QString indentJson(const QString &json)
{
    QString out;
    int depth = 0;
    bool inString = false, escaped = false;
    const auto newline = [&] { out += u'\n' + QString(depth * 4, u' '); };
    for (int i = 0; i < json.size(); ++i) {
        const QChar c = json.at(i);
        if (inString) {
            out += c;
            if (escaped)
                escaped = false;
            else if (c == u'\\')
                escaped = true;
            else if (c == u'"')
                inString = false;
            continue;
        }
        if (c.isSpace())
            continue;
        if (c == u'"') {
            inString = true;
            out += c;
        } else if (c == u'{' || c == u'[') {
            int j = i + 1; // keep empty {} / [] on one line
            while (j < json.size() && json.at(j).isSpace())
                ++j;
            if (j < json.size() && (json.at(j) == u'}' || json.at(j) == u']')) {
                out += c;
                out += json.at(j);
                i = j;
                continue;
            }
            out += c;
            ++depth;
            newline();
        } else if (c == u'}' || c == u']') {
            --depth;
            newline();
            out += c;
        } else if (c == u',') {
            out += c;
            newline();
        } else if (c == u':') {
            out += QStringLiteral(": ");
        } else {
            out += c;
        }
    }
    return out;
}

// kind: json | base64-encode | base64-decode | url-encode | url-decode | upper | lower
inline TextResult transformText(const QString &kind, const QString &text)
{
    if (kind == QLatin1String("json")) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError)
            return {false, text, QStringLiteral("Not valid JSON: %1 (at %2)").arg(err.errorString()).arg(err.offset)};
        Q_UNUSED(doc)
        return {true, indentJson(text), {}};
    }
    if (kind == QLatin1String("base64-encode"))
        return {true, QString::fromLatin1(text.toUtf8().toBase64()), {}};
    if (kind == QLatin1String("base64-decode")) {
        const auto decoded = QByteArray::fromBase64Encoding(text.trimmed().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return {false, text, QStringLiteral("Not valid Base64")};
        return {true, QString::fromUtf8(*decoded), {}};
    }
    if (kind == QLatin1String("url-encode"))
        return {true, QString::fromLatin1(QUrl::toPercentEncoding(text)), {}};
    if (kind == QLatin1String("url-decode"))
        return {true, QUrl::fromPercentEncoding(text.toUtf8()), {}};
    if (kind == QLatin1String("upper"))
        return {true, text.toUpper(), {}};
    if (kind == QLatin1String("lower"))
        return {true, text.toLower(), {}};
    return {false, text, QStringLiteral("Unknown transform %1").arg(kind)};
}
