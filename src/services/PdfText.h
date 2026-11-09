#pragma once

#include "Languages.h"

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

// The words of a PDF, made ready to be read by a language model: page by page, without the running header and footer, the page numbers, the
// hyphenated line ends and the hard-wrapped lines. Pure and tested.
namespace Pdf {

struct Page {
    int number = 0; // 1-based
    QString text;
    bool ocr = false; // the text came from recognizing a picture
};

struct Chunk {
    int firstPage = 0, lastPage = 0;
    QString text; // the pages' words, each page after a "[p. N]" line
};

// How many letters a text has (the test of "is there text on this page", whatever the language)
inline int letters(const QString &text)
{
    int n = 0;
    for (const QChar c : text)
        if (c.isLetter())
            ++n;
    return n;
}

// A page with (almost) no letters is a picture: it is read by recognizing it.
inline bool looksScanned(const QString &text, int minLetters = 25) { return letters(text) < minLetters; }

namespace detail {
inline QString shape(const QString &line) // a line with its numbers blanked: "Halaman 3 dari 20" and "Halaman 4 dari 20" are the same line
{
    QString s = line.simplified().toLower();
    s.replace(QRegularExpression(QStringLiteral("\\d+")), QStringLiteral("#"));
    return s;
}
inline bool pageNumberLine(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^\\s*[-–—]?\\s*(?:page|halaman|hal\\.?|p\\.)?\\s*\\d{1,4}\\s*(?:(?:of|dari|/)\\s*\\d{1,4})?\\s*[-–—]?\\s*$"),
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return re.match(line).hasMatch();
}
}

// Running headers and footers (the same line near the top or bottom of at least `share` of the pages) and lone page numbers are dropped, words
// split by a hyphen at the end of a line are joined, and lines that were only wrapped are joined into paragraphs.
inline QList<Page> clean(QList<Page> pages, double share = 0.4)
{
    QList<QStringList> lines;
    for (const Page &p : std::as_const(pages)) {
        QStringList l;
        for (const QString &raw : p.text.split(u'\n'))
            l << raw.simplified();
        lines << l;
    }
    // the lines that come back on page after page
    QSet<QString> running;
    if (pages.size() >= 3) {
        QHash<QString, int> seen;
        for (const QStringList &l : std::as_const(lines)) {
            QStringList edge;
            QStringList nonEmpty;
            for (const QString &x : l)
                if (!x.isEmpty())
                    nonEmpty << x;
            for (int i = 0; i < std::min<int>(2, int(nonEmpty.size())); ++i)
                edge << nonEmpty[i];
            for (int i = std::max(2, int(nonEmpty.size()) - 2); i < nonEmpty.size(); ++i)
                edge << nonEmpty[i];
            QSet<QString> once;
            for (const QString &x : edge)
                if (!detail::pageNumberLine(x))
                    once.insert(detail::shape(x));
            for (const QString &x : once)
                ++seen[x];
        }
        const int need = std::max(3, int(std::ceil(share * pages.size())));
        for (auto it = seen.cbegin(); it != seen.cend(); ++it)
            if (it.value() >= need && it.key().size() >= 3)
                running.insert(it.key());
    }
    static const QRegularExpression hyphenEnd(QStringLiteral("(\\p{L})-$"), QRegularExpression::UseUnicodePropertiesOption);
    for (int i = 0; i < pages.size(); ++i) {
        QStringList kept;
        for (const QString &x : std::as_const(lines[i])) {
            if (!x.isEmpty() && (detail::pageNumberLine(x) || running.contains(detail::shape(x))))
                continue;
            kept << x;
        }
        // join: a paragraph is what is between blank lines
        QStringList paragraphs;
        QString cur;
        auto flush = [&] {
            if (!cur.isEmpty())
                paragraphs << cur;
            cur.clear();
        };
        for (const QString &x : std::as_const(kept)) {
            if (x.isEmpty()) {
                flush();
                continue;
            }
            const bool bullet = QRegularExpression(QStringLiteral("^(?:[-•*▪●]|\\(?\\d{1,2}[.)]|[a-zA-Z][.)])\\s")).match(x).hasMatch();
            if (cur.isEmpty() || bullet) {
                flush();
                cur = x;
            } else if (hyphenEnd.match(cur).hasMatch() && x.at(0).isLower()) {
                cur.chop(1); // "pemba-" + "gian" = "pembagian"
                cur += x;
            } else if (!cur.endsWith(u'.') && !cur.endsWith(u'!') && !cur.endsWith(u'?') && !cur.endsWith(u':') && !cur.endsWith(u';') && x.at(0).isLower()) {
                cur += u' ' + x; // a line that was only wrapped
            } else {
                flush();
                cur = x;
            }
        }
        flush();
        pages[i].text = paragraphs.join(QStringLiteral("\n\n")).trimmed();
    }
    return pages;
}

// The language of a text among `enabled` (codes), or "" when it cannot tell (see Lang::detect).
inline QString detectLanguage(const QString &text, const QStringList &enabled = Lang::defaults()) { return Lang::detect(text, enabled); }

// Whole pages in pieces of at most about `maxChars`; one page that is longer on its own is cut at paragraph ends.
inline QList<Chunk> chunkPages(const QList<Page> &pages, int maxChars = 6000)
{
    QList<Chunk> out;
    Chunk cur;
    auto flush = [&] {
        if (!cur.text.isEmpty())
            out << cur;
        cur = Chunk();
    };
    for (const Page &p : pages) {
        if (p.text.trimmed().isEmpty())
            continue;
        const QString marker = QStringLiteral("[p. %1]\n").arg(p.number);
        if (p.text.size() + marker.size() > maxChars) { // a long page: by paragraphs, every piece after the page's marker
            flush();
            QString piece;
            for (const QString &para : p.text.split(QStringLiteral("\n\n"))) {
                if (!piece.isEmpty() && piece.size() + para.size() + 2 > maxChars) {
                    out << Chunk{p.number, p.number, marker + piece};
                    piece.clear();
                }
                piece += (piece.isEmpty() ? QString() : QStringLiteral("\n\n")) + para.left(maxChars);
            }
            if (!piece.isEmpty())
                out << Chunk{p.number, p.number, marker + piece};
            continue;
        }
        if (!cur.text.isEmpty() && cur.text.size() + p.text.size() + marker.size() + 2 > maxChars)
            flush();
        if (cur.text.isEmpty())
            cur.firstPage = p.number;
        cur.lastPage = p.number;
        cur.text += (cur.text.isEmpty() ? QString() : QStringLiteral("\n\n")) + marker + p.text;
    }
    flush();
    return out;
}

} // namespace Pdf
