#pragma once

#include "Chunker.h"

#include <QRegularExpression>
#include <QString>
#include <QStringList>

// Checks on what a model quotes: a quote that is mostly right can still carry a wrong number ("Rp 24.000.000.000" for "Rp 24.000.000"), and a wrong
// number is the one mistake that matters. Pure.
namespace Evidence {

// The numbers in a text as written ("24.000.000", "17.00", "14"), lower case, without a sentence's full stop at their end
inline QStringList numbersIn(const QString &text)
{
    static const QRegularExpression num(QStringLiteral(R"(\p{N}[\p{N}.,:/-]*)"));
    QStringList out;
    for (auto it = num.globalMatch(text); it.hasNext();) {
        QString n = it.next().captured(0).toLower();
        while (!n.isEmpty() && !n.back().isDigit())
            n.chop(1);
        if (!n.isEmpty() && !out.contains(n))
            out << n;
    }
    return out;
}

// Does every number of `claim` stand in `source` exactly as written?
inline bool numbersStand(const QString &claim, const QString &source)
{
    const QString s = source.toLower();
    for (const QString &n : numbersIn(claim))
        if (!s.contains(n))
            return false;
    return true;
}

// The sentence of `source` that has the most words of `quote` (for showing the real words when the model's quote slipped)
inline QString closestSentence(const QString &source, const QString &quote)
{
    static const QRegularExpression splitWords(QStringLiteral(R"([^\p{L}\p{N}]+)"));
    QSet<QString> want;
    for (const QString &w : quote.toLower().split(splitWords, Qt::SkipEmptyParts))
        if (w.size() >= 3)
            want.insert(w);
    QString best;
    int bestScore = -1;
    for (const QString &s : Knowledge::sentences(source)) {
        int score = 0;
        for (const QString &w : s.toLower().split(splitWords, Qt::SkipEmptyParts))
            if (want.contains(w))
                ++score;
        if (score > bestScore) {
            bestScore = score;
            best = s;
        }
    }
    return best;
}

} // namespace Evidence
