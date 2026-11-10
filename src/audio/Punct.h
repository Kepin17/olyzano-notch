#pragma once

#include "Aligner.h"

#include <QList>
#include <QString>

#include <functional>

// Full stops, commas and capital letters for text that has none. The model for it (a download) knows English only, so other languages are
// left as they are: Whisper already writes Indonesian and English with punctuation, this is for what it leaves bare. The text of a segment
// changes, never its times or its speaker. Pure.
namespace Audio {
namespace Punct {

inline bool supported(const QString &language)
{
    const QString l = language.toLower();
    return l == QLatin1String("en") || l.startsWith(QLatin1String("en-")) || l.startsWith(QLatin1String("en_"));
}

// Text that is worth sending: two words or more, and no capital letter and no sentence mark anywhere in it.
inline bool needs(const QString &text)
{
    const QString t = text.simplified();
    if (t.count(u' ') < 1)
        return false;
    for (const QChar c : t)
        if (c.isUpper() || c == u'.' || c == u',' || c == u'?' || c == u'!' || c == u';' || c == u':')
            return false;
    return true;
}

// texts in -> the same texts with marks and capitals, in the same order; returns why not, "" when it worked
using Batch = std::function<QString(const QStringList &, QStringList *)>;

inline QString letters(const QString &t)
{
    QString x;
    for (const QChar c : t)
        if (c.isLetterOrNumber())
            x += c.toLower();
    return x;
}

// One call for all the segments that need it. A segment whose answer has other words than it had (the model may only add marks and
// capitals) is left alone. Returns "" or why not; `changed` is how many segments got new text.
inline QString apply(QList<Segment> *segments, const QString &language, const Batch &punctuate, int *changed = nullptr)
{
    if (changed)
        *changed = 0;
    if (!supported(language) || !punctuate)
        return {};
    QList<int> at;
    QStringList in;
    for (int i = 0; i < segments->size(); ++i)
        if (needs((*segments)[i].text)) {
            at << i;
            in << (*segments)[i].text.simplified();
        }
    if (in.isEmpty())
        return {};
    QStringList out;
    if (const QString err = punctuate(in, &out); !err.isEmpty())
        return err;
    if (out.size() != in.size())
        return QStringLiteral("the answer did not fit");
    for (int k = 0; k < at.size(); ++k) {
        const QString o = out[k].simplified();
        if (o.isEmpty() || letters(o) != letters(in[k]))
            continue;
        (*segments)[at[k]].text = o;
        if (changed)
            ++*changed;
    }
    return {};
}

} // namespace Punct
} // namespace Audio
