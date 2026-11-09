#pragma once

#include "Aligner.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>

// Names that the conversation itself gives: "my name is Budi" names whoever says it; "Halo Budi" or "Pak Budi, how is..." names the person who
// answers (or, failing that, the one who spoke before). These are SUGGESTIONS: a wrong name on a person is worse than none, so the person
// decides (Apply / Ignore). Pure and tested; works on the Indonesian and English ways of saying it.
namespace Audio {

struct NameHint {
    QString speaker; // the speaker's label (SPEAKER_00 ...), which is what a rename applies to
    QString name;
    double time = 0;
    QString quote;      // the words it came from
    bool certain = false; // the person said it about themselves
    int evidence = 1;
};

namespace detail {

inline const QSet<QString> &notNames()
{
    static const QSet<QString> words = [] {
        const QString list = QStringLiteral(
            "yang dan di ke dari ini itu saya aku gue kamu kau anda kita kami mereka dia apa napa kenapa siapa kapan dimana mana bagaimana gimana ya tidak nggak gak "
            "enggak bisa mau akan sudah udah belum lagi juga saja aja dong deh sih kok nih tuh pagi siang sore malam semua teman rekan bapak ibu pak bu mas mbak kak bang "
            "halo hai oke baik ok terima kasih makasih tes test coba ngomong sama sekali banget untuk dengan karena jadi nanti besok kemarin hari ini sekarang tadi "
            "gimana mungkin harus perlu boleh silakan tolong maaf permisi selamat datang rapat meeting "
            "the a an and is are you we they he she it this that what why who when where how yes no not okay hello hi hey everyone everybody guys team all good morning "
            "afternoon evening thanks thank please sorry just so well now then here there");
        QSet<QString> set;
        for (const QString &w : list.split(u' ', Qt::SkipEmptyParts))
            set.insert(w);
        return set;
    }();
    return words;
}

inline bool plausibleName(const QString &w, bool needCapital)
{
    if (w.size() < 2 || w.size() > 20)
        return false;
    for (const QChar c : w)
        if (!c.isLetter())
            return false;
    if (notNames().contains(w.toLower()))
        return false;
    return !needCapital || w.at(0).isUpper();
}

inline QString nice(const QString &w) { return w.left(1).toUpper() + w.mid(1).toLower(); }

} // namespace detail

// `usedNames`: names already given to speakers (not suggested again, for anyone). `named`: the labels that already have a name (left alone).
inline QList<NameHint> nameHints(const QList<Segment> &lines, const QStringList &usedNames = {}, const QStringList &named = {})
{
    using Opt = QRegularExpression::PatternOption;
    const auto rx = [](const QString &p) { return QRegularExpression(p, Opt::CaseInsensitiveOption | Opt::UseUnicodePropertiesOption); };
    const QString name = QStringLiteral("(\\p{L}{2,20})");
    const QString honorific = QStringLiteral("(?:pak|bu|bapak|ibu|mas|mbak|mba|kak|bang|bro|sis|mr\\.?|mrs\\.?|ms\\.?|dr\\.?)");
    // about themselves: certain (lower case is fine: the recognizer often writes names small)
    static const QList<QRegularExpression> selfStrong = {
        rx(QStringLiteral("\\bnama\\s+(?:saya|aku|gue)\\s+(?:adalah\\s+)?(?:") + honorific + QStringLiteral("\\s+)?") + name),
        rx(QStringLiteral("\\b(?:perkenalkan|kenalkan)[,]?\\s+(?:nama\\s+)?(?:saya|aku)\\s+(?:") + honorific + QStringLiteral("\\s+)?") + name),
        rx(QStringLiteral("\\bmy name(?:'s| is)\\s+") + name),
    };
    // about themselves, only when written with a capital
    static const QList<QRegularExpression> selfWeak = {
        rx(QStringLiteral("\\b(?:saya|aku)\\s+(\\p{Lu}\\p{L}{1,19})\\s+(?:dari|di|sebagai|dan)\\b")),
        rx(QStringLiteral("\\b(?:saya|aku)\\s+(\\p{L}{2,20})\\s*[.!?]?$")),
        rx(QStringLiteral("\\b(?:i am|i'm|this is)\\s+(\\p{Lu}\\p{L}{1,19})\\b")),
    };
    // about the other person
    static const QList<QRegularExpression> toStrong = { // an honorific then a name: "Pak Budi"
        rx(QStringLiteral("\\b") + honorific + QStringLiteral("\\s+") + name),
    };
    static const QList<QRegularExpression> toWeak = { // capital letters only
        rx(QStringLiteral("\\b(?:halo|hai|hi|hello|hey|selamat\\s+(?:pagi|siang|sore|malam))[, ]+(?:") + honorific + QStringLiteral("\\s+)?(\\p{Lu}\\p{L}{1,19})")),
        rx(QStringLiteral("\\b(?:terima kasih|makasih|thanks|thank you)[, ]+(?:") + honorific + QStringLiteral("\\s+)?(\\p{Lu}\\p{L}{1,19})")),
        rx(QStringLiteral("^(\\p{Lu}\\p{L}{1,19})[,!?]")),
    };
    const QSet<QString> used = [&] { QSet<QString> u; for (const QString &n : usedNames) u.insert(n.trimmed().toLower()); return u; }();
    const QSet<QString> skip(named.cbegin(), named.cend());

    QList<NameHint> found;
    auto add = [&](const QString &speaker, const QString &raw, bool certain, const Segment &s) {
        if (speaker.isEmpty() || skip.contains(speaker) || used.contains(raw.toLower()))
            return;
        NameHint h;
        h.speaker = speaker;
        h.name = detail::nice(raw);
        h.time = s.start;
        h.quote = s.text.simplified().left(90);
        h.certain = certain;
        found.append(h);
    };
    // the speaker who answers (the next different one), or else the one before
    auto other = [&](int i) -> QString {
        const QString me = lines[i].speaker;
        for (int j = i + 1; j < lines.size() && j <= i + 4; ++j)
            if (lines[j].speaker != me)
                return lines[j].speaker;
        for (int j = i - 1; j >= 0 && j >= i - 4; --j)
            if (lines[j].speaker != me)
                return lines[j].speaker;
        return {};
    };
    for (int i = 0; i < lines.size(); ++i) {
        const Segment &s = lines[i];
        const QString text = s.text.simplified();
        bool selfFound = false;
        for (const QRegularExpression &re : selfStrong)
            if (const auto m = re.match(text); m.hasMatch() && detail::plausibleName(m.captured(1), false)) {
                add(s.speaker, m.captured(1), true, s);
                selfFound = true;
                break;
            }
        if (!selfFound)
            for (const QRegularExpression &re : selfWeak)
                if (const auto m = re.match(text); m.hasMatch() && detail::plausibleName(m.captured(1), true)) {
                    add(s.speaker, m.captured(1), true, s);
                    selfFound = true;
                    break;
                }
        if (selfFound)
            continue; // "perkenalkan, saya Pak Budi" is not about someone else
        QString target;
        for (const QRegularExpression &re : toStrong)
            if (const auto m = re.match(text); m.hasMatch() && detail::plausibleName(m.captured(1), false)) {
                target = m.captured(1);
                break;
            }
        if (target.isEmpty())
            for (const QRegularExpression &re : toWeak)
                if (const auto m = re.match(text); m.hasMatch() && detail::plausibleName(m.captured(1), true)) {
                    target = m.captured(1);
                    break;
                }
        if (!target.isEmpty())
            add(other(i), target, false, s);
    }
    // one name per speaker (certain first, then the most often said), and a name goes to one speaker only
    QHash<QString, QHash<QString, NameHint>> byPerson; // speaker -> name -> hint
    for (const NameHint &h : std::as_const(found)) {
        NameHint &slot = byPerson[h.speaker][h.name.toLower()];
        if (slot.name.isEmpty())
            slot = h;
        else {
            ++slot.evidence;
            slot.certain = slot.certain || h.certain;
        }
    }
    QList<NameHint> best;
    for (auto it = byPerson.cbegin(); it != byPerson.cend(); ++it) {
        NameHint top;
        for (const NameHint &h : it.value())
            if (top.name.isEmpty() || h.certain > top.certain || (h.certain == top.certain && (h.evidence > top.evidence || (h.evidence == top.evidence && h.time < top.time))))
                top = h;
        best.append(top);
    }
    std::sort(best.begin(), best.end(), [](const NameHint &a, const NameHint &b) {
        return a.certain != b.certain ? a.certain : a.evidence != b.evidence ? a.evidence > b.evidence : a.time < b.time;
    });
    QSet<QString> taken;
    QList<NameHint> out;
    for (const NameHint &h : std::as_const(best)) {
        if (taken.contains(h.name.toLower()))
            continue;
        taken.insert(h.name.toLower());
        out.append(h);
    }
    std::sort(out.begin(), out.end(), [](const NameHint &a, const NameHint &b) { return a.time < b.time; });
    return out;
}

} // namespace Audio
