#pragma once

#include "Aligner.h"

#include <QRegularExpression>
#include <QStringList>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QString>

// What the helper programs print, read by code that needs no program to be tested.
namespace Audio {

// "  Duration: 01:02:03.50, start: ..." from ffmpeg's banner -> seconds; -1 when there is none
inline double parseFfmpegDuration(const QString &banner)
{
    static const QRegularExpression re(QStringLiteral("Duration:\\s*(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch m = re.match(banner);
    return m.hasMatch() ? m.captured(1).toInt() * 3600.0 + m.captured(2).toInt() * 60.0 + m.captured(3).toDouble() : -1;
}

// one line of `-progress pipe:1` ("out_time_us=12345678", "out_time_ms=12345678": both are microseconds) -> seconds; -1 for any other line
inline double parseFfmpegProgress(const QString &line)
{
    const QString l = line.trimmed();
    for (const QString &key : {QStringLiteral("out_time_us="), QStringLiteral("out_time_ms=")}) {
        if (l.startsWith(key)) {
            bool ok = false;
            const double us = l.mid(key.size()).toDouble(&ok);
            return ok && us >= 0 ? us / 1e6 : -1;
        }
    }
    return -1;
}

// sherpa-onnx's "0.318 -- 6.865 speaker_00" lines -> turns labelled SPEAKER_00 ...
inline QList<Turn> parseDiarization(const QString &output)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([0-9]+(?:\\.[0-9]+)?)\\s*--\\s*([0-9]+(?:\\.[0-9]+)?)\\s+speaker_(\\d+)\\s*$"));
    QList<Turn> turns;
    for (const QString &line : output.split(u'\n')) {
        const QRegularExpressionMatch m = re.match(line.trimmed());
        if (!m.hasMatch())
            continue;
        const double a = m.captured(1).toDouble(), b = m.captured(2).toDouble();
        if (b > a)
            turns.append({a, b, QStringLiteral("SPEAKER_%1").arg(m.captured(3).toInt(), 2, 10, QLatin1Char('0'))});
    }
    return turns;
}

// Qwen3 writes its reasoning between <think> tags unless it is switched off: the answer is what comes after
inline QString stripThinking(QString text)
{
    static const QRegularExpression think(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
    text.remove(think);
    const int open = text.indexOf(QStringLiteral("<think>")); // a reasoning that never ended: nothing usable after it
    if (open >= 0)
        text.truncate(open);
    return text.trimmed();
}

// The answer of llama-server as it streams ("stream": true): lines "data: {json}" ending with "data: [DONE]". Bytes arrive in any pieces, so a line may be cut
// anywhere: what is not a whole line yet waits for the next piece. `feed` gives the new words (choices[0].delta.content) of the whole lines it has.
class SseParser
{
public:
    QStringList feed(const QByteArray &bytes)
    {
        QStringList out;
        m_buf += bytes;
        for (int nl = m_buf.indexOf('\n'); nl >= 0; nl = m_buf.indexOf('\n')) {
            const QByteArray line = m_buf.left(nl).trimmed();
            m_buf.remove(0, nl + 1);
            if (!line.startsWith("data:"))
                continue; // a comment (": keep-alive") or an empty line between events
            const QByteArray payload = line.mid(5).trimmed();
            if (payload == "[DONE]") {
                m_done = true;
                continue;
            }
            const QString delta = QJsonDocument::fromJson(payload).object().value(QStringLiteral("choices")).toArray().at(0).toObject().value(QStringLiteral("delta")).toObject().value(QStringLiteral("content")).toString();
            if (!delta.isEmpty())
                out << delta;
        }
        return out;
    }
    bool done() const { return m_done; }

private:
    QByteArray m_buf;
    bool m_done = false;
};

} // namespace Audio
