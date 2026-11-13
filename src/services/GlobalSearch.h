#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>

// What the search box shows for a hit: the words typed, and the passage around the first of them with the words made bold. The passage comes from
// a file, a recording or somebody's answer to a form, so it is HTML-escaped before anything is made bold (StyledText would otherwise read it). Pure.
namespace Knowledge {

// The words of what was typed: lower case, letters and digits, each once, at most six.
inline QStringList wordsOf(const QString &query)
{
    QStringList out;
    for (const QString &w : query.toLower().split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), Qt::SkipEmptyParts))
        if (!out.contains(w))
            out << w;
    return out.mid(0, 6);
}

inline QString escapeHtml(QString s)
{
    s.replace(QLatin1Char('&'), QStringLiteral("&amp;")).replace(QLatin1Char('<'), QStringLiteral("&lt;")).replace(QLatin1Char('>'), QStringLiteral("&gt;")).replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return s;
}

// Around the first word that is in the text: `before` letters before it and the rest of `width` after; "…" at a cut end. Bold on every word (also as the
// beginning of a longer one, as the search finds them). "" for no text.
inline QString snippetHtml(const QString &text, const QStringList &words, int width = 120, int before = 30)
{
    const QString flat = text.simplified();
    if (flat.isEmpty())
        return {};
    int at = -1;
    const QString lower = flat.toLower();
    for (const QString &w : words) {
        const int i = lower.indexOf(w);
        if (i >= 0 && (at < 0 || i < at))
            at = i;
    }
    const int from = at > before ? at - before : 0;
    const QString piece = flat.mid(from, width);
    const bool cutStart = from > 0, cutEnd = from + width < flat.size();
    // the words are found in the plain text and each piece is escaped on its own, so that a word can never land inside an entity or a tag
    QStringList alt;
    for (const QString &w : words)
        if (!w.isEmpty())
            alt << QRegularExpression::escape(w);
    std::sort(alt.begin(), alt.end(), [](const QString &x, const QString &y) { return x.size() > y.size(); });
    QString html;
    if (alt.isEmpty()) {
        html = escapeHtml(piece);
    } else {
        const QRegularExpression re(alt.join(QLatin1Char('|')), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        int pos = 0;
        for (auto it = re.globalMatch(piece); it.hasNext();) {
            const auto m = it.next();
            if (m.capturedLength() == 0)
                continue;
            html += escapeHtml(piece.mid(pos, m.capturedStart() - pos)) + QStringLiteral("<b>") + escapeHtml(m.captured()) + QStringLiteral("</b>");
            pos = m.capturedEnd();
        }
        html += escapeHtml(piece.mid(pos));
    }
    return (cutStart ? QStringLiteral("…") : QString()) + html + (cutEnd ? QStringLiteral("…") : QString());
}

} // namespace Knowledge
