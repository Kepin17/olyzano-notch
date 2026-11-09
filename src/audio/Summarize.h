#pragma once

#include "SummaryModel.h"

#include <QJsonObject>

#include <atomic>
#include <functional>

// The summary of a whole recording through a language model that has a small window: the transcript goes in pieces, each piece is read
// for points, decisions and tasks, the pieces are merged, and when there were several the overall summary is written once more from the
// partial ones. The model is behind `chat` (system prompt, user text, JSON schema -> the answer, or "" when it failed), so this is tested
// with a fake. What the model says is checked against the transcript (SummaryModel.h) before anyone sees it.
namespace Audio {

using ChatFn = std::function<QString(const QString &system, const QString &user, const QJsonObject &schema)>;

struct SummarizeOptions {
    Template tmpl = Template::Meeting;
    QString language = QStringLiteral("auto");
    int chunkChars = 9000;
};

// "" on success, else why not (cancelled, or the model did not answer).
inline QString summarizeTranscript(const QList<Segment> &segments, const QStringList &speakers, const SummarizeOptions &opt, const ChatFn &chat,
                                   const std::function<void(double)> &progress, const std::atomic_bool *cancel, Summary *out)
{
    if (segments.isEmpty()) {
        *out = Summary();
        return {};
    }
    QStringList people; // who is in it, by the names a reader sees
    for (const QString &sp : speakers)
        if (const QString f = friendlyName(sp); !f.isEmpty() && f != QLatin1String("?") && !people.contains(f))
            people << f;
    const QString system = systemPrompt(opt.tmpl, opt.language, people);
    const QStringList pieces = chunk(segments, opt.chunkChars);
    QList<Summary> parts;
    for (int i = 0; i < pieces.size(); ++i) {
        if (cancel && cancel->load())
            return QStringLiteral("cancelled");
        const QString user = wrapTranscript(pieces[i])
                             + QStringLiteral("\n\nRead the transcript above%1 and answer with the JSON object.")
                                   .arg(pieces.size() > 1 ? QStringLiteral(" (part %1 of %2)").arg(i + 1).arg(pieces.size()) : QString());
        const QString answer = chat(system, user, extractionSchema(opt.tmpl));
        if (cancel && cancel->load())
            return QStringLiteral("cancelled");
        if (answer.isEmpty())
            return QStringLiteral("the language model did not answer");
        Summary part = parse(answer.toUtf8());
        if (part.isEmpty() && pieces.size() == 1)
            return QStringLiteral("the language model's answer could not be read");
        parts.append(validate(part, segments, speakers));
        if (progress)
            progress(double(i + 1) / (pieces.size() + (pieces.size() > 1 ? 1 : 0)));
    }
    Summary merged = merge(parts);
    if (pieces.size() > 1) { // the pieces were read one by one: one more pass writes the whole as one text (overview, the subjects in order, next steps)
        QStringList notes;
        for (int i = 0; i < parts.size(); ++i)
            if (!parts[i].executive.isEmpty())
                notes << QStringLiteral("Part %1: %2").arg(i + 1).arg(parts[i].executive);
        for (const Topic &t : std::as_const(merged.topics))
            notes << QStringLiteral("Topic%1 %2: %3").arg(t.timestamp >= 0 ? QStringLiteral(" [%1]").arg(clock(t.timestamp)) : QString(), t.title, t.points.join(QStringLiteral("; ")));
        for (const Decision &d : std::as_const(merged.decisions))
            notes << QStringLiteral("Decision: %1%2").arg(d.description, d.rationale.isEmpty() ? QString() : QStringLiteral(" (reason: %1)").arg(d.rationale));
        for (const ActionItem &a : std::as_const(merged.actions))
            notes << QStringLiteral("Action: %1%2%3").arg(a.task, a.assignee.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(a.assignee),
                                                           a.deadline.isEmpty() ? QString() : QStringLiteral(" by %1").arg(a.deadline));
        for (const QString &r : std::as_const(merged.risks))
            notes << QStringLiteral("Risk: %1").arg(r);
        for (const QString &q : std::as_const(merged.openQuestions))
            notes << QStringLiteral("Open question: %1").arg(q);
        const QJsonObject strArray{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}};
        const QJsonObject topic{{QStringLiteral("type"), QStringLiteral("object")},
                                {QStringLiteral("properties"), QJsonObject{{QStringLiteral("title"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                                             {QStringLiteral("points"), strArray},
                                                                             {QStringLiteral("t"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                                {QStringLiteral("required"), QJsonArray{QStringLiteral("title"), QStringLiteral("points"), QStringLiteral("t")}}};
        const QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{{QStringLiteral("summary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                                              {QStringLiteral("topics"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), topic}}},
                                                                              {QStringLiteral("risks"), strArray}, {QStringLiteral("next_steps"), strArray}}},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("summary"), QStringLiteral("topics")}}};
        const QString answer = chat(system, wrapTranscript(notes.join(u'\n'))
                                                + QStringLiteral("\n\nThe text above is notes taken from consecutive parts of one recording. Write the final notes of the whole recording from them only: "
                                                                 "\"summary\" is an overview of 3-5 sentences; \"topics\" are the subjects in the order they came up (at most 8; join repeats; keep each topic's "
                                                                 "\"t\" from the notes); \"risks\" and \"next_steps\" only from what the notes say. Answer with the JSON object."),
                                    schema);
        const Summary overall = parse(answer.toUtf8());
        if (!overall.executive.isEmpty())
            merged.executive = overall.executive;
        if (!overall.topics.isEmpty())
            merged.topics = validate(overall, segments, speakers).topics;
        if (!overall.risks.isEmpty())
            merged.risks = overall.risks;
        if (!overall.nextSteps.isEmpty())
            merged.nextSteps = overall.nextSteps;
    }
    merged.participants = people;
    if (progress)
        progress(1.0);
    *out = merged;
    return {};
}

} // namespace Audio
