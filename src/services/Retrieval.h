#pragma once

#include "KnowledgeStore.h"

#include <QtNumeric>
#include <qfloat16.h>
#include <QVector>
#include <QHash>
#include <QList>
#include <QPair>

#include <algorithm>
#include <cmath>

// Putting the passages that were found in order, and choosing the few that go to the model: the words (BM25) and, when the optional embedding
// model is there, the meaning (cosine of vectors) are joined by rank, not by score (the two scores are not on one scale). Pure.
namespace Knowledge {

// float32 -> float16 bytes (what is stored) and back
inline QByteArray toF16(const QVector<float> &v)
{
    QByteArray out(int(v.size()) * 2, '\0');
    auto *p = reinterpret_cast<qfloat16 *>(out.data());
    for (int i = 0; i < v.size(); ++i)
        p[i] = qfloat16(v[i]);
    return out;
}

inline QVector<float> fromF16(const QByteArray &b)
{
    QVector<float> out(int(b.size() / 2));
    const auto *p = reinterpret_cast<const qfloat16 *>(b.constData());
    for (int i = 0; i < out.size(); ++i)
        out[i] = float(p[i]);
    return out;
}

inline void normalize(QVector<float> &v)
{
    double s = 0;
    for (const float x : std::as_const(v))
        s += double(x) * x;
    s = std::sqrt(s);
    if (s > 0)
        for (float &x : v)
            x = float(x / s);
}

inline double dot(const QVector<float> &a, const QVector<float> &b)
{
    double s = 0;
    const int n = int(std::min(a.size(), b.size()));
    for (int i = 0; i < n; ++i)
        s += double(a[i]) * b[i];
    return s;
}

// The `top` nearest to `query` among the stored vectors (all normalised): {chunk id, cosine}, nearest first.
inline QList<QPair<qint64, double>> nearest(const QVector<float> &query, const QList<QPair<qint64, QByteArray>> &stored, int top)
{
    QList<QPair<qint64, double>> scored;
    scored.reserve(stored.size());
    for (const auto &s : stored)
        scored.append({s.first, dot(query, fromF16(s.second))});
    const int n = std::min<int>(top, int(scored.size()));
    std::partial_sort(scored.begin(), scored.begin() + n, scored.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    scored.resize(n);
    return scored;
}

// Reciprocal rank fusion of two orders of chunk ids: 1/(k+rank) summed. k = 60 as is usual.
inline QList<qint64> fuse(const QList<qint64> &byWords, const QList<qint64> &byMeaning, int k = 60)
{
    QHash<qint64, double> score;
    for (int i = 0; i < byWords.size(); ++i)
        score[byWords[i]] += 1.0 / (k + i + 1);
    for (int i = 0; i < byMeaning.size(); ++i)
        score[byMeaning[i]] += 1.0 / (k + i + 1);
    QList<qint64> ids = score.keys();
    std::sort(ids.begin(), ids.end(), [&](qint64 a, qint64 b) { return score[a] != score[b] ? score[a] > score[b] : a < b; });
    return ids;
}

// From the passages in order of relevance: at most `perDoc` of a document (so one long file does not fill the answer) and `maxTotal` in all,
// stopping when the text would pass `maxChars`. The relevance order is kept.
inline QList<KnowledgeStore::Hit> pickSources(const QList<KnowledgeStore::Hit> &ranked, int maxTotal = 8, int perDoc = 3, int maxChars = 6500)
{
    QList<KnowledgeStore::Hit> out;
    QHash<qint64, int> perDocCount;
    int chars = 0;
    for (const KnowledgeStore::Hit &h : ranked) {
        if (out.size() >= maxTotal)
            break;
        if (perDocCount[h.docId] >= perDoc || (chars + h.text.size() > maxChars && !out.isEmpty()))
            continue;
        ++perDocCount[h.docId];
        chars += h.text.size();
        out.append(h);
    }
    return out;
}

// The documents of a list of passages in order of their best passage, once each
inline QStringList docRefsInOrder(const QList<KnowledgeStore::Hit> &hits)
{
    QStringList refs;
    for (const KnowledgeStore::Hit &h : hits)
        if (!refs.contains(h.ref))
            refs << h.ref;
    return refs;
}

} // namespace Knowledge
