#pragma once

#include "SummaryModel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

// The files a processing job leaves in the meeting's folder (so it can go on after a failure or a restart). Written whole and atomically.
namespace Audio {
namespace Json {

inline QJsonArray segments(const QList<Segment> &list)
{
    QJsonArray a;
    for (const Segment &s : list)
        a.append(QJsonObject{{QStringLiteral("s"), s.start}, {QStringLiteral("e"), s.end}, {QStringLiteral("t"), s.text}, {QStringLiteral("p"), s.speaker},
                             {QStringLiteral("a"), s.approx}});
    return a;
}

inline QList<Segment> segments(const QJsonArray &a)
{
    QList<Segment> out;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        out.append({o.value(QStringLiteral("s")).toDouble(), o.value(QStringLiteral("e")).toDouble(), o.value(QStringLiteral("t")).toString(),
                    o.value(QStringLiteral("p")).toString(), o.value(QStringLiteral("a")).toBool()});
    }
    return out;
}

inline QJsonArray turns(const QList<Turn> &list)
{
    QJsonArray a;
    for (const Turn &t : list)
        a.append(QJsonObject{{QStringLiteral("s"), t.start}, {QStringLiteral("e"), t.end}, {QStringLiteral("p"), t.speaker}});
    return a;
}

inline QList<Turn> turns(const QJsonArray &a)
{
    QList<Turn> out;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        out.append({o.value(QStringLiteral("s")).toDouble(), o.value(QStringLiteral("e")).toDouble(), o.value(QStringLiteral("p")).toString()});
    }
    return out;
}

inline QJsonArray topicsJson(const QList<Topic> &l)
{
    QJsonArray out;
    for (const Topic &t : l)
        out.append(QJsonObject{{QStringLiteral("title"), t.title}, {QStringLiteral("points"), QJsonArray::fromStringList(t.points)}, {QStringLiteral("t"), t.timestamp}});
    return out;
}

inline QList<Topic> topicsFrom(const QJsonValue &v)
{
    QList<Topic> out;
    for (const QJsonValue &e : v.toArray()) {
        const QJsonObject o = e.toObject();
        Topic t;
        t.title = o.value(QStringLiteral("title")).toString();
        for (const QJsonValue &p : o.value(QStringLiteral("points")).toArray())
            t.points << p.toString();
        t.timestamp = o.value(QStringLiteral("t")).toDouble(-1);
        out.append(t);
    }
    return out;
}

// The parts of a summary that the older database columns do not hold (kept as one JSON text in summaries.extra).
inline QJsonObject summaryExtra(const Summary &s)
{
    return {{QStringLiteral("participants"), QJsonArray::fromStringList(s.participants)}, {QStringLiteral("topics"), topicsJson(s.topics)},
            {QStringLiteral("concepts"), topicsJson(s.concepts)}, {QStringLiteral("qa"), topicsJson(s.qa)}, {QStringLiteral("quotes"), QJsonArray::fromStringList(s.quotes)},
            {QStringLiteral("risks"), QJsonArray::fromStringList(s.risks)}, {QStringLiteral("next_steps"), QJsonArray::fromStringList(s.nextSteps)}};
}

inline void applySummaryExtra(Summary &s, const QJsonObject &o)
{
    auto list = [](const QJsonValue &v) {
        QStringList l;
        for (const QJsonValue &e : v.toArray())
            l << e.toString();
        return l;
    };
    s.participants = list(o.value(QStringLiteral("participants")));
    s.topics = topicsFrom(o.value(QStringLiteral("topics")));
    s.concepts = topicsFrom(o.value(QStringLiteral("concepts")));
    s.qa = topicsFrom(o.value(QStringLiteral("qa")));
    s.quotes = list(o.value(QStringLiteral("quotes")));
    s.risks = list(o.value(QStringLiteral("risks")));
    s.nextSteps = list(o.value(QStringLiteral("next_steps")));
}

inline QJsonObject summary(const Summary &s)
{
    auto list = [](const QStringList &l) { return QJsonArray::fromStringList(l); };
    QJsonArray decisions, actions;
    for (const Decision &d : s.decisions)
        decisions.append(QJsonObject{{QStringLiteral("description"), d.description}, {QStringLiteral("reason"), d.rationale}, {QStringLiteral("owner"), d.owner},
                                     {QStringLiteral("t"), d.timestamp}, {QStringLiteral("quote"), d.quote}, {QStringLiteral("review"), d.needsReview}});
    for (const ActionItem &a : s.actions)
        actions.append(QJsonObject{{QStringLiteral("task"), a.task}, {QStringLiteral("assignee"), a.assignee}, {QStringLiteral("deadline"), a.deadline},
                                   {QStringLiteral("priority"), a.priority}, {QStringLiteral("t"), a.timestamp}, {QStringLiteral("quote"), a.quote},
                                   {QStringLiteral("review"), a.needsReview}, {QStringLiteral("done"), a.done}});
    QJsonObject out{{QStringLiteral("summary"), s.executive}, {QStringLiteral("key_points"), list(s.keyPoints)}, {QStringLiteral("open_questions"), list(s.openQuestions)},
                    {QStringLiteral("notes"), list(s.notes)}, {QStringLiteral("decisions"), decisions}, {QStringLiteral("action_items"), actions}};
    const QJsonObject extra = summaryExtra(s);
    for (auto it = extra.begin(); it != extra.end(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

inline Summary summary(const QJsonObject &o)
{
    Summary s;
    auto list = [](const QJsonValue &v) {
        QStringList l;
        for (const QJsonValue &e : v.toArray())
            l << e.toString();
        return l;
    };
    s.executive = o.value(QStringLiteral("summary")).toString();
    s.keyPoints = list(o.value(QStringLiteral("key_points")));
    s.openQuestions = list(o.value(QStringLiteral("open_questions")));
    s.notes = list(o.value(QStringLiteral("notes")));
    applySummaryExtra(s, o);
    for (const QJsonValue &v : o.value(QStringLiteral("decisions")).toArray()) {
        const QJsonObject d = v.toObject();
        Decision x;
        x.description = d.value(QStringLiteral("description")).toString();
        x.rationale = d.value(QStringLiteral("reason")).toString();
        x.owner = d.value(QStringLiteral("owner")).toString();
        x.timestamp = d.value(QStringLiteral("t")).toDouble(-1);
        x.quote = d.value(QStringLiteral("quote")).toString();
        x.needsReview = d.value(QStringLiteral("review")).toBool();
        s.decisions.append(x);
    }
    for (const QJsonValue &v : o.value(QStringLiteral("action_items")).toArray()) {
        const QJsonObject a = v.toObject();
        s.actions.append({a.value(QStringLiteral("task")).toString(), a.value(QStringLiteral("assignee")).toString(), a.value(QStringLiteral("deadline")).toString(),
                          a.value(QStringLiteral("priority")).toString(), a.value(QStringLiteral("t")).toDouble(-1), a.value(QStringLiteral("quote")).toString(),
                          a.value(QStringLiteral("review")).toBool(), a.value(QStringLiteral("done")).toBool()});
    }
    return s;
}

// Atomic write; "" on success.
inline QString save(const QString &path, const QJsonObject &o)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) < 0 || !f.commit())
        return QStringLiteral("cannot write %1").arg(path);
    return {};
}

// An empty object when the file is missing or damaged (a stage whose file is not usable is simply done again).
inline QJsonObject load(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

} // namespace Json
} // namespace Audio
