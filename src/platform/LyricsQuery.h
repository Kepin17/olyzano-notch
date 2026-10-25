#pragma once

#include <QRegularExpression>
#include <QString>

// What a lyrics service is asked: the song as a player names it, minus the clutter ("MAZZEL - Topic",
// "Song (Official Video)", "[MV]"). Pure, for the tests.
namespace LyricsQuery {

// "Name - Topic" is how YouTube names an artist's auto-made channel; "VEVO" its label's.
inline QString cleanArtist(const QString &artist)
{
    static const QRegularExpression topic(QStringLiteral(R"(\s*-\s*Topic\s*$|VEVO\s*$)"), QRegularExpression::CaseInsensitiveOption);
    QString a = artist;
    a.remove(topic);
    return a.split(QStringLiteral(", ")).value(0).trimmed(); // the first of several: services list the main one
}

inline QString cleanTitle(const QString &title, const QString &artist)
{
    static const QRegularExpression tags(
        QStringLiteral(R"([\(\[][^\)\]]*\b(official|video|mv|m/v|audio|lyrics?|visuali[sz]er|live|remaster(?:ed)?|explicit|hd|4k|full|ver\.?)\b[^\)\]]*[\)\]])"),
        QRegularExpression::CaseInsensitiveOption);
    QString t = title;
    t.remove(tags);
    const QString prefix = artist + QStringLiteral(" - ");
    if (!artist.isEmpty() && t.startsWith(prefix, Qt::CaseInsensitive)) // "Artist - Song"
        t = t.mid(prefix.size());
    return t.simplified();
}

// Letters and digits only, lower case: "Ado" == "ADO", "Mr. Children" == "mrchildren".
inline QString plain(const QString &s)
{
    QString out;
    for (const QChar c : s)
        if (c.isLetterOrNumber())
            out += c.toLower();
    return out;
}

// One artist name inside the other (a feature, a band's full name).
inline bool sameArtist(const QString &a, const QString &b)
{
    const QString x = plain(a), y = plain(b);
    return !x.isEmpty() && !y.isEmpty() && (x.contains(y) || y.contains(x));
}

}
