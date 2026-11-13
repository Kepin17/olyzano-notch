#pragma once

#include "Aligner.h"
#include "SoundEvents.h"
#include "SummaryModel.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

// A transcript as a file for other programs: subtitles (SRT, WebVTT) and plain text, and the chapters of a recording as a list for a video's
// description (YouTube takes "00:00 Title" lines). Lines that are a sound (speaker Sound::Speaker) are left out or kept as a [marker]. Pure.
namespace Audio {
namespace Export {

enum class Format { Srt, Vtt, Text };
enum class Sounds { Skip, Marker };

struct Options {
    Sounds sounds = Sounds::Marker;
    bool speakers = true;      // "Name: text" (text) / "Name: text" in the cue (subtitles)
    bool timestamps = true;    // plain text only: "[01:02] "
    int maxLine = 42;          // subtitles: characters before a line is broken (two lines per cue at most are made by breaking at spaces)
};

// 3725.5 -> "01:02:05,500" (SRT) / "01:02:05.500" (VTT)
inline QString stamp(double seconds, QChar separator)
{
    const qint64 ms = qint64(std::llround(std::max(0.0, seconds) * 1000.0));
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(separator)
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

// a line of a cue: breaks at a space after `width` characters, so a cue is a few short lines
inline QString wrap(const QString &text, int width)
{
    QStringList lines;
    QString current;
    for (const QString &word : text.simplified().split(u' ', Qt::SkipEmptyParts)) {
        if (!current.isEmpty() && current.size() + 1 + word.size() > width) {
            lines << current;
            current.clear();
        }
        current += (current.isEmpty() ? QString() : QStringLiteral(" ")) + word;
    }
    if (!current.isEmpty())
        lines << current;
    return lines.join(u'\n');
}

// the words of one segment, as shown: "Name: text" or "[Music]"; "" when there is nothing to show
inline QString body(const Segment &s, const Options &o)
{
    const QString text = s.text.simplified();
    if (text.isEmpty())
        return {};
    if (Sound::isSound(s))
        return o.sounds == Sounds::Skip ? QString() : QStringLiteral("[%1]").arg(text);
    const QString who = s.speaker.isEmpty() ? QString() : friendlyName(s.speaker);
    return o.speakers && !who.isEmpty() ? who + QStringLiteral(": ") + text : text;
}

// WebVTT must not hold "-->" or "&" and "<" unescaped in a cue
inline QString vttEscape(QString t)
{
    t.replace(u'&', QStringLiteral("&amp;")).replace(u'<', QStringLiteral("&lt;")).replace(u'>', QStringLiteral("&gt;"));
    return t;
}

inline QString subtitles(const QList<Segment> &segments, bool vtt, const Options &o)
{
    QString out = vtt ? QStringLiteral("WEBVTT\n\n") : QString();
    int n = 0;
    for (const Segment &s : segments) {
        QString text = body(s, o);
        if (text.isEmpty())
            continue;
        const double end = s.end > s.start ? s.end : s.start + 1.0; // a cue must last
        text = wrap(vtt ? vttEscape(text) : text, o.maxLine);
        if (!vtt)
            out += QString::number(++n) + u'\n';
        out += stamp(s.start, vtt ? u'.' : u',') + QStringLiteral(" --> ") + stamp(end, vtt ? u'.' : u',') + u'\n' + text + QStringLiteral("\n\n");
    }
    return out;
}

// The segments with their words replaced by a translation (index for index); a sound ([Music]) keeps its own words, and a segment with no
// translation keeps the original.
inline QList<Segment> translated(const QList<Segment> &segments, const QStringList &translations)
{
    QList<Segment> out = segments;
    for (int i = 0; i < out.size() && i < translations.size(); ++i)
        if (!Sound::isSound(out[i]) && !translations[i].simplified().isEmpty())
            out[i].text = translations[i];
    return out;
}

// Subtitles with two languages in each cue: the words as said, then the translation under them (the speaker's name only on the first line).
inline QString bilingualSubtitles(const QList<Segment> &segments, const QStringList &translations, bool vtt, const Options &o)
{
    QString out = vtt ? QStringLiteral("WEBVTT\n\n") : QString();
    int n = 0;
    for (int i = 0; i < segments.size(); ++i) {
        const Segment &s = segments[i];
        QString first = body(s, o);
        if (first.isEmpty())
            continue;
        const QString second = Sound::isSound(s) ? QString() : translations.value(i).simplified();
        const double end = s.end > s.start ? s.end : s.start + 1.0;
        first = wrap(vtt ? vttEscape(first) : first, o.maxLine);
        QString text = first;
        if (!second.isEmpty() && second != s.text.simplified())
            text += u'\n' + wrap(vtt ? vttEscape(second) : second, o.maxLine);
        if (!vtt)
            out += QString::number(++n) + u'\n';
        out += stamp(s.start, vtt ? u'.' : u',') + QStringLiteral(" --> ") + stamp(end, vtt ? u'.' : u',') + u'\n' + text + QStringLiteral("\n\n");
    }
    return out;
}

inline QString plain(const QList<Segment> &segments, const Options &o)
{
    QString out;
    for (const Segment &s : segments) {
        const QString text = body(s, o);
        if (text.isEmpty())
            continue;
        out += (o.timestamps ? QStringLiteral("[%1] ").arg(clock(s.start)) : QString()) + text + u'\n';
    }
    return out;
}

inline QString render(const QList<Segment> &segments, Format f, const Options &o = {})
{
    switch (f) {
    case Format::Srt: return subtitles(segments, false, o);
    case Format::Vtt: return subtitles(segments, true, o);
    case Format::Text: break;
    }
    return plain(segments, o);
}

inline Format formatOf(const QString &id)
{
    const QString l = id.toLower();
    return l == QLatin1String("srt") ? Format::Srt : l == QLatin1String("vtt") ? Format::Vtt : Format::Text;
}
inline QString extension(Format f) { return f == Format::Srt ? QStringLiteral("srt") : f == Format::Vtt ? QStringLiteral("vtt") : QStringLiteral("txt"); }

// ---- chapters ----

struct Chapter {
    double start = 0;
    QString title;
};

constexpr int MinChapters = 3;      // YouTube shows chapters only from three
constexpr double MinGap = 10.0;     // and each at least ten seconds long

// The topics of the notes that say when they begin, in time order: the first is moved to 0:00 (YouTube needs it), a topic closer than
// MinGap to the one before is dropped. Fewer than MinChapters left: none (a list that YouTube would not show is of no use).
inline QList<Chapter> chapters(const QList<Topic> &topics, double duration = 0)
{
    QList<Chapter> list;
    for (const Topic &t : topics)
        if (t.timestamp >= 0 && !t.title.simplified().isEmpty() && (duration <= 0 || t.timestamp < duration - MinGap))
            list.append({t.timestamp, t.title.simplified()});
    std::stable_sort(list.begin(), list.end(), [](const Chapter &a, const Chapter &b) { return a.start < b.start; });
    QList<Chapter> out;
    for (const Chapter &c : std::as_const(list)) {
        if (!out.isEmpty() && c.start - out.last().start < MinGap)
            continue;
        out.append(c);
    }
    if (out.size() < MinChapters)
        return {};
    out.first().start = 0;
    return out;
}

// "00:00 Intro" per line: YouTube (the video description) takes this as it is. Minutes can pass 59 ("75:10") only if the video is under an
// hour; from an hour on it is "1:02:05" for every line.
inline QString youtube(const QList<Chapter> &list)
{
    const bool hours = !list.isEmpty() && list.last().start >= 3600;
    QString out;
    for (const Chapter &c : list) {
        const int s = int(c.start);
        const QString t = hours ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0')) : clock(c.start);
        out += t + u' ' + c.title + u'\n';
    }
    return out;
}

// "- 12:34 Title": for notes and chat
inline QString chapterText(const QList<Chapter> &list)
{
    QString out;
    for (const Chapter &c : list)
        out += QStringLiteral("- %1  %2\n").arg(clock(c.start), c.title);
    return out;
}

} // namespace Export
} // namespace Audio
