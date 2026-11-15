#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// Tidying a transcript without a language model: fillers ("eee", "um"), a word said three times or more in a row, and the person's own list of
// words the recognizer gets wrong ("kuen" -> "Qwen"). Plain rules, so the result is predictable, never invents a word, and doing it twice changes
// nothing more. The caller keeps the original text (the transcript store holds it apart), so every change can be undone.
namespace Audio {
namespace Tidy {

struct Word {
    QString heard, written;
};

struct Options {
    bool fillers = true;
    bool repeats = true;
    bool words = true;
};

namespace detail {

inline const QString &edge() // not inside a longer word
{
    static const QString e = QStringLiteral("(?<![\\p{L}\\p{N}_])");
    return e;
}

inline QString capitalised(QString s)
{
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    return s;
}

} // namespace detail

// "heard" as a whole word, in any case, becomes "written"; a capital at the start of what was heard is kept when "written" has none of its own.
inline QString applyWords(const QString &text, const QList<Word> &words)
{
    QString out = text;
    for (const Word &w : words) {
        const QString heard = w.heard.trimmed(), written = w.written.trimmed();
        if (heard.isEmpty() || written.isEmpty() || heard == written)
            continue;
        const QRegularExpression re(detail::edge() + QRegularExpression::escape(heard) + QStringLiteral("(?![\\p{L}\\p{N}_])"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        const bool hasCapital = written != written.toLower();
        QString built;
        qsizetype last = 0;
        auto it = re.globalMatch(out);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            built += out.mid(last, m.capturedStart() - last);
            built += (!hasCapital && m.captured().at(0).isUpper()) ? detail::capitalised(written) : written;
            last = m.capturedEnd();
        }
        built += out.mid(last);
        out = built;
    }
    return out;
}

// Only sounds that carry no meaning, on their own: "eee", "emm", "hmm", "um", "uh", "er". Words with a meaning are never touched.
inline QString removeFillers(const QString &text)
{
    static const QRegularExpression re(detail::edge() + QStringLiteral("(?:e{2,}|em{1,}|ehm+|hm+|mm+|um+|uhm+|uh+|er)(?![\\p{L}\\p{N}_]),?\\s*"),
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return QString(text).remove(re);
}

// The same word three times or more in a row becomes one ("saya saya saya mau" -> "saya mau"); twice is left, it can be meant ("pelan pelan").
inline QString collapseRepeats(const QString &text)
{
    static const QRegularExpression re(detail::edge() + QStringLiteral("(\\p{L}+)(?:[ ,]+\\1(?![\\p{L}\\p{N}_])){2,}"),
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    QString out = text;
    return out.replace(re, QStringLiteral("\\1"));
}

inline QString tidySpaces(const QString &text, bool startedUpper)
{
    static const QRegularExpression spaces(QStringLiteral("[ \\t]{2,}")), before(QStringLiteral("\\s+([,.;:!?])")), twice(QStringLiteral("([,;:])\\s*\\1+"));
    QString s = text;
    s.replace(spaces, QStringLiteral(" ")).replace(before, QStringLiteral("\\1")).replace(twice, QStringLiteral("\\1"));
    s = s.trimmed();
    while (!s.isEmpty() && QStringLiteral(",;:").contains(s.at(0)))
        s = s.mid(1).trimmed();
    if (startedUpper && !s.isEmpty() && s.at(0).isLower())
        s[0] = s.at(0).toUpper();
    return s;
}

// One line of a transcript, tidied. A line that would end up empty (it was only "eee") comes back empty: the caller leaves such a line alone.
inline QString cleanSegment(const QString &text, const Options &options, const QList<Word> &words)
{
    QString s = text;
    const bool upper = !s.isEmpty() && s.trimmed().at(0).isUpper();
    if (options.fillers)
        s = removeFillers(s);
    if (options.repeats)
        s = collapseRepeats(s);
    if (options.words)
        s = applyWords(s, words);
    return tidySpaces(s, upper);
}

// The words of the language model's list that name `written` words: for the speech model's prompt, which settles how names are spelt.
inline QString vocabularyOf(const QList<Word> &words, int maxChars = 200)
{
    QStringList seen;
    int used = 0;
    for (const Word &w : words) {
        const QString t = w.written.trimmed();
        if (t.isEmpty() || seen.contains(t, Qt::CaseInsensitive) || used + t.size() + 2 > maxChars)
            continue;
        seen << t;
        used += t.size() + 2;
    }
    return seen.join(QStringLiteral(", "));
}

} // namespace Tidy
} // namespace Audio
