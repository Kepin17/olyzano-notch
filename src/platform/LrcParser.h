#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>

// Synced lyrics in the LRC text format: "[mm:ss.xx] words". Pure, for the tests.
namespace Lrc {

struct Line {
    qint64 ms = 0;
    QString text; // empty: a gap (the music plays, nobody sings)
};

// "mm:ss", "mm:ss.x", "mm:ss.xx" (hundredths) or "mm:ss.xxx" (milliseconds) -> ms, -1 if it isn't one.
inline qint64 stampToMs(const QString &minutes, const QString &seconds, const QString &fraction)
{
    bool ok1 = false, ok2 = false;
    const qint64 m = minutes.toLongLong(&ok1), s = seconds.toLongLong(&ok2);
    if (!ok1 || !ok2)
        return -1;
    qint64 f = 0;
    if (!fraction.isEmpty()) {
        const QString digits = fraction.left(3);
        f = digits.toLongLong() * (digits.size() == 1 ? 100 : digits.size() == 2 ? 10 : 1);
    }
    return (m * 60 + s) * 1000 + f;
}

// Sorted by time. Several stamps on one line give several lines; [offset:±ms] shifts them (a positive
// offset makes the words come sooner); the other [tag:value] lines are ignored.
inline QList<Line> parse(const QString &text)
{
    static const QRegularExpression stamp(QStringLiteral(R"(\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\])"));
    static const QRegularExpression offsetTag(QStringLiteral(R"(^\s*\[offset:\s*([+-]?\d+)\s*\]\s*$)"), QRegularExpression::CaseInsensitiveOption);
    QList<Line> lines;
    qint64 offset = 0;
    for (const QString &raw : text.split(u'\n')) {
        const QString line = raw.trimmed();
        if (const auto o = offsetTag.match(line); o.hasMatch()) {
            offset = o.captured(1).toLongLong();
            continue;
        }
        QList<qint64> stamps;
        qsizetype end = 0;
        for (auto it = stamp.globalMatch(line); it.hasNext();) {
            const auto m = it.next();
            if (m.capturedStart() != end)
                break; // the stamps are only at the start of the line
            if (const qint64 ms = stampToMs(m.captured(1), m.captured(2), m.captured(3)); ms >= 0)
                stamps << ms;
            end = m.capturedEnd();
        }
        if (stamps.isEmpty())
            continue;
        const QString words = line.mid(end).trimmed();
        for (const qint64 ms : std::as_const(stamps))
            lines << Line{std::max<qint64>(0, ms - offset), words};
    }
    std::stable_sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) { return a.ms < b.ms; });
    while (!lines.isEmpty() && lines.last().text.isEmpty())
        lines.removeLast(); // trailing gap markers say nothing
    return lines;
}

// The line being sung at `ms`: the last one that started, -1 before the first.
inline int indexAt(const QList<Line> &lines, qint64 ms)
{
    const auto it = std::upper_bound(lines.cbegin(), lines.cend(), ms, [](qint64 v, const Line &l) { return v < l.ms; });
    return int(it - lines.cbegin()) - 1;
}

}
