#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>

// Pure helpers behind the prompt box of Dev → Agent: the `/` commands, the `@file` mentions and the ranking of files for the
// `@` list. No sockets, no QObject: unit-tested in tst_codingagent.
namespace AgentCommands {

struct SlashCommand {
    QString name;
    QString args;        // what may follow, for the list ("[id]", "<title>")
    QString description;
};

inline const QList<SlashCommand> &slashCommands()
{
    static const QList<SlashCommand> commands{
        {QStringLiteral("fork"), {}, QStringLiteral("Copy this task into a new one and carry on there")},
        {QStringLiteral("model"), QStringLiteral("[id]"), QStringLiteral("Choose the model")},
        {QStringLiteral("effort"), QStringLiteral("[low|medium|high|xhigh|max]"), QStringLiteral("How hard the model thinks")},
        {QStringLiteral("mode"), QStringLiteral("[plan|manual|auto]"), QStringLiteral("Switch the approval mode")},
        {QStringLiteral("plan"), QStringLiteral("[task]"), QStringLiteral("Discuss and write a plan first")},
        {QStringLiteral("compact"), QStringLiteral("[focus]"), QStringLiteral("Summarize older messages to free context")},
        {QStringLiteral("new"), {}, QStringLiteral("Start a new task")},
        {QStringLiteral("clear"), {}, QStringLiteral("Clear this task's conversation")},
        {QStringLiteral("rename"), QStringLiteral("<title>"), QStringLiteral("Rename this task")},
        {QStringLiteral("workspace"), {}, QStringLiteral("Choose the workspace folder")},
        {QStringLiteral("copy"), {}, QStringLiteral("Copy the last answer")},
        {QStringLiteral("export"), {}, QStringLiteral("Save this task as a Markdown file")},
        {QStringLiteral("details"), QStringLiteral("[headings|full]"), QStringLiteral("Show what the agent did as one line each, or in full")},
        {QStringLiteral("help"), {}, QStringLiteral("Show what @, ! and / do")},
    };
    return commands;
}

struct Slash {
    bool ok = false;
    QString name;
    QString args;
};

// "/model gpt-4.1" -> {model, "gpt-4.1"}. Only a known command name counts, so "/usr/bin/env" or "/tmp" stay ordinary text.
inline Slash parseSlash(const QString &text)
{
    const QString t = text.trimmed();
    if (!t.startsWith(QLatin1Char('/')) || t.size() < 2) return {};
    const int space = t.indexOf(QRegularExpression(QStringLiteral("\\s")));
    const QString name = (space < 0 ? t.mid(1) : t.mid(1, space - 1)).toLower();
    for (const SlashCommand &c : slashCommands())
        if (c.name == name) return {true, name, space < 0 ? QString() : t.mid(space + 1).trimmed()};
    return {};
}

// What the list shows for "/mo": names starting with it first, then names containing it.
inline QList<SlashCommand> completeSlash(const QString &prefix)
{
    const QString p = prefix.trimmed().toLower();
    QList<SlashCommand> starts, contains;
    for (const SlashCommand &c : slashCommands()) {
        if (c.name.startsWith(p)) starts.append(c);
        else if (c.name.contains(p)) contains.append(c);
    }
    return starts + contains;
}

struct Mention {
    int start = 0;   // index of the '@'
    int length = 0;  // '@' plus the path
    QString path;
};

// `@path` at the start of the text or after whitespace ("a@b.com" is not one). Punctuation closing a sentence is not part of the path.
inline QList<Mention> mentionTokens(const QString &text)
{
    QList<Mention> out;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('@') || (i > 0 && !text.at(i - 1).isSpace())) continue;
        int j = i + 1;
        while (j < text.size() && !text.at(j).isSpace()) ++j;
        QString path = text.mid(i + 1, j - i - 1);
        while (!path.isEmpty() && QStringLiteral(",;:)]}!?\"'").contains(path.back())) path.chop(1);
        while (path.endsWith(QLatin1Char('.')) && !path.endsWith(QStringLiteral(".."))) path.chop(1);
        if (path.isEmpty()) continue;
        out.append({i, int(path.size()) + 1, path});
        i = j - 1;
    }
    return out;
}

// The files the `@` list offers for a query: file name starts with it, then name contains it, then path contains it, then the
// letters appear in order. Shorter paths first inside each group. An empty query lists the shallowest files.
inline QStringList rankFiles(const QString &query, const QStringList &paths, int limit = 8)
{
    const QString q = query.trimmed().toLower();
    struct Hit { int group; int depth; QString path; };
    QList<Hit> hits;
    for (const QString &path : paths) {
        const QString lower = path.toLower();
        const QString base = lower.mid(lower.lastIndexOf(QLatin1Char('/')) + 1);
        const int depth = int(lower.count(QLatin1Char('/')));
        int group = -1;
        if (q.isEmpty()) group = 0;
        else if (base.startsWith(q)) group = 0;
        else if (base.contains(q)) group = 1;
        else if (lower.contains(q)) group = 2;
        else {
            int at = 0;
            for (const QChar c : q) { at = int(lower.indexOf(c, at)); if (at < 0) break; ++at; }
            if (at >= 0) group = 3;
        }
        if (group >= 0) hits.append({group, depth, path});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.path.size() != b.path.size() && a.depth == b.depth) return a.path.size() < b.path.size();
        if (a.depth != b.depth) return a.depth < b.depth;
        return a.path < b.path;
    });
    QStringList out;
    for (const Hit &h : hits) { if (out.size() >= limit) break; out.append(h.path); }
    return out;
}

} // namespace AgentCommands
