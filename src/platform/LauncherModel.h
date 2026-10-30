#pragma once

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>

// What the app launcher works out without asking the system: which group an app belongs to and how well it matches
// what was typed. Pure, for the tests.
namespace LauncherModel {

// The group shown for a desktop entry's Categories (the first that applies, most specific first).
inline QString groupOf(const QStringList &categories)
{
    static const QList<QPair<QString, QStringList>> groups{
        {QStringLiteral("Games"), {QStringLiteral("Game")}},
        {QStringLiteral("Development"), {QStringLiteral("Development"), QStringLiteral("IDE")}},
        {QStringLiteral("Graphics"), {QStringLiteral("Graphics"), QStringLiteral("Photography")}},
        {QStringLiteral("Media"), {QStringLiteral("AudioVideo"), QStringLiteral("Audio"), QStringLiteral("Video"), QStringLiteral("Player")}},
        {QStringLiteral("Internet"), {QStringLiteral("Network"), QStringLiteral("WebBrowser"), QStringLiteral("Email"), QStringLiteral("Chat")}},
        {QStringLiteral("Office"), {QStringLiteral("Office"), QStringLiteral("Education"), QStringLiteral("Science")}},
        {QStringLiteral("System"), {QStringLiteral("System"), QStringLiteral("Settings"), QStringLiteral("Monitor"), QStringLiteral("PackageManager")}},
        {QStringLiteral("Utilities"), {QStringLiteral("Utility"), QStringLiteral("Accessories")}},
    };
    for (const auto &g : groups)
        for (const QString &c : categories)
            if (g.second.contains(c))
                return g.first;
    return QStringLiteral("Other");
}

// How well one token matches an app (0 = not at all): the name beats a word of it beats a part of it beats its
// keywords beats the letters in order ("ffx" finds Firefox).
inline int scoreToken(const QString &token, const QString &name, const QString &keywords)
{
    const QString q = token.trimmed().toLower(), n = name.toLower();
    if (q.isEmpty())
        return 1;
    if (n == q)
        return 100;
    if (n.startsWith(q))
        return 80;
    for (const QString &w : n.split(u' ', Qt::SkipEmptyParts))
        if (w.startsWith(q))
            return 60;
    if (n.contains(q))
        return 40;
    if (keywords.toLower().contains(q))
        return 20;
    int at = 0;
    for (const QChar c : q) {
        at = n.indexOf(c, at);
        if (at < 0)
            return 0;
        ++at;
    }
    return 10;
}

// How well a query matches an app (0 = not at all). One word: the name beats a word of it beats a part of it beats
// its keywords beats the letters in order. Several words ("visual studio"): every word must match name or keywords
// (AND); the score is the average of the words, with a bonus when all of them hit the name itself.
inline int score(const QString &query, const QString &name, const QString &keywords)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return 1;
    const QStringList tokens = q.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return 1;
    if (tokens.size() == 1)
        return scoreToken(tokens.first(), name, keywords);

    int sum = 0;
    int nameOnly = 0;
    for (const QString &token : tokens) {
        const int s = scoreToken(token, name, keywords);
        if (s == 0)
            return 0; // every word must match somewhere
        sum += s;
        if (scoreToken(token, name, {}) > 0)
            ++nameOnly;
    }
    const int avg = sum / tokens.size();
    return nameOnly == tokens.size() ? avg + 15 : avg;
}

// The apps ([{id, name, keywords, category, ...}]) that match, best first: by score, then by how often they were used,
// then by name. Without a query: all of them, A-Z. `category` empty means any.
inline QVariantList search(const QVariantList &apps, const QString &query, const QString &category, const QHash<QString, int> &uses)
{
    struct Hit { QVariantMap app; double rank; };
    QList<Hit> hits;
    for (const QVariant &v : apps) {
        const QVariantMap a = v.toMap();
        if (!category.isEmpty() && a.value(QStringLiteral("category")).toString() != category)
            continue;
        const int s = score(query, a.value(QStringLiteral("name")).toString(), a.value(QStringLiteral("keywords")).toString());
        if (s == 0)
            continue;
        const bool typed = !query.trimmed().isEmpty();
        hits << Hit{a, typed ? s + std::min(uses.value(a.value(QStringLiteral("id")).toString()), 20) * 0.5 : 0};
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &x, const Hit &y) {
        return x.rank != y.rank ? x.rank > y.rank : x.app.value(QStringLiteral("name")).toString().compare(y.app.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    QVariantList out;
    for (const Hit &h : hits)
        out << h.app;
    return out;
}

}
