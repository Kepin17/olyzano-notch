#pragma once

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Pure data + protocol helpers for the Coding Assistant (`codingAgent` in QML).
// No sockets, no QObject — unit-testable in tst_core.
//
// Official API references:
// - Xiaomi MiMo: https://mimo.mi.com/docs/en-US/quick-start/summary/welcome
//   Base URL: https://api.xiaomimimo.com/v1 (OpenAI-compatible /chat/completions & /models)
// - DeepSeek: https://api-docs.deepseek.com/
//   Base URL: https://api.deepseek.com/v1 (OpenAI-compatible /chat/completions & /models;
//   supports configurable model IDs and `reasoning_content` stream deltas)
// - Anthropic Claude: https://api.anthropic.com/v1 (/messages with x-api-key + anthropic-version)
// - OpenAI / Custom OpenAI-compatible endpoints
namespace CodingAgentModel {

// Execution / approval mode:
// - "plan": read-only analysis and implementation planning; never modifies files or runs shell commands
// - "manual": read-only tools run automatically; file edits and shell commands wait for user Apply/Run
// - "auto": approved operations (reads, file edits, shell commands) execute automatically
inline QString normalizeMode(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String("plan") || m == QLatin1String("auto"))
        return m;
    return QStringLiteral("manual");
}

// A prompt that asks for a plan or a discussion starts Plan Mode by itself (lowering what the agent may do, never raising it).
// "plan.md" alone does not count: that is a file name.
inline bool asksForPlan(const QString &prompt)
{
    static const QRegularExpression re(
        QStringLiteral("\\b(?:plan(?!\\.md)|plans|planning|rencana|rencanakan|merencanakan|rancang|rancangan|discuss|diskusi|diskusikan|brainstorm)\\b"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return re.match(prompt).hasMatch();
}

// Told to the agent in Plan Mode while a plan waits for the person's decision, so a comment typed straight into the chat is
// understood in its context: a change request revises the plan, a question or a doubt is discussed first.
inline QString planContextPrompt(const QString &plan)
{
    return QStringLiteral(
        "A plan is waiting for the person's decision. Its current text:\n--- plan.md ---\n") + plan +
        QStringLiteral("\n--- end of plan.md ---\n"
        "Read the person's next message in that light:\n"
        "- If it asks for a change (add, remove, reorder, \"make it simpler\", \"also do X\"), call `write_plan` with the complete revised plan.\n"
        "- If it is a question, a doubt or an opinion, discuss it and answer; rewrite the plan only if the discussion changes it.\n"
        "- If you cannot tell which, ask one short question: discuss first, or revise the plan?\n");
}

// What the agent is told when the person accepts the plan (the plan travels in the message: the agent cannot read the plan.md file itself).
inline QString planApprovalPrompt(const QString &plan)
{
    return QStringLiteral("The plan below is approved. Implement it now, step by step, and follow it. "
                          "If something does not fit what you find in the code, say so before you deviate. "
                          "If it has three or more steps, start by writing them as a to-do list with `todo_write`. "
                          "Report briefly when you are done.\n\n--- plan.md ---\n") + plan;
}

inline QString normalizeProtocol(const QString &protocol)
{
    const QString p = protocol.trimmed().toLower();
    if (p == QLatin1String("anthropic") || p == QLatin1String("claude"))
        return QStringLiteral("anthropic");
    return QStringLiteral("openai");
}

struct ProviderConfig {
    QString id;
    QString name;
    QString protocol = QStringLiteral("openai"); // "openai" (MiMo, DeepSeek, OpenAI, Custom) | "anthropic" (Claude)
    QString baseUrl;
    QString modelId;
    QStringList modelPresets;
    double temperature = 0.2;
    int maxTokens = 4096;
    QString authHeader = QStringLiteral("Bearer"); // "Bearer" -> Authorization: Bearer <key>, "api-key" -> api-key: <key>, "x-api-key" -> x-api-key: <key>
    QString customHeadersJson;                     // optional JSON object of extra HTTP headers
    QString extraBodyJson;                         // optional JSON object merged into request payload
    QString docsUrl;
    bool builtIn = false;
    QString maxTokensParameter = QStringLiteral("max_tokens");
    int contextWindow = 0; // tokens the model can hold; 0 = guess from the model id (defaultContextWindow)
};

// How many tokens a model can hold, guessed from its id. It only drives the context meter and auto compact, so a wrong guess
// costs a compact too early or too late; Advanced lets the person set the real number.
inline int defaultContextWindow(const QString &modelId)
{
    const QString id = modelId.toLower();
    if (id.contains(QLatin1String("gpt-4.1"))) return 1000000;
    if (id.contains(QLatin1String("gpt-5")) || id.contains(QLatin1String("codex"))) return 400000;
    if (id.contains(QLatin1String("gpt-4o")) || id.contains(QLatin1String("gpt-4-turbo"))) return 128000;
    if (id.startsWith(QLatin1String("o3")) || id.startsWith(QLatin1String("o4")) || id.startsWith(QLatin1String("o1"))) return 200000;
    if (id.contains(QLatin1String("claude"))) return 200000;
    if (id.contains(QLatin1String("deepseek"))) return 128000;
    if (id.contains(QLatin1String("mimo"))) return 128000;
    return 128000;
}
inline int effectiveContextWindow(const ProviderConfig &p)
{
    return p.contextWindow >= 4096 ? p.contextWindow : defaultContextWindow(p.modelId);
}
// A rough token count from a size in bytes (about four per token), for when the provider does not report usage.
inline int estimateTokens(qint64 bytes) { return int((bytes + 3) / 4); }

struct ToolCall {
    QString id;
    QString name;
    QString argumentsJson;
    QString status = QStringLiteral("pending"); // pending | awaiting_approval | running | applied | completed | rejected | error
    QString summary;
    QString diffText;
    QString filePath;
    QString proposedContent;
    QByteArray originalHash;
    bool originalExists = false;
    QString command;
    QString output;
    int addedLines = 0;
    int removedLines = 0;
};

struct Attachment {
    QString path;       // workspace-relative
    qint64 bytes = 0;
};

struct TodoItem {
    QString content;
    QString status = QStringLiteral("pending"); // pending | in_progress | completed
};

struct ChatMessage {
    QString id;
    QString role; // "user" | "assistant" | "tool" | "shell" (a command the person ran with !) | "note" (a divider, never sent)
    QString content;
    QString shown;                   // what the transcript shows when it differs from `content` (attached files are in `content` only)
    QList<Attachment> attachments;   // files the person mentioned with @
    QString reasoning;
    QString toolCallId;
    QString toolName;
    QList<ToolCall> toolCalls;
    qint64 timestamp = 0;
};

struct Session {
    QString id;
    QString title;
    QString workspace;
    QString mode = QStringLiteral("manual"); // plan | manual | auto
    QString providerId = QStringLiteral("mimo");
    QString modelId;
    QList<ChatMessage> messages;
    qint64 updatedAt = 0;
    QString plan;       // the latest plan the agent wrote in Plan Mode (also saved as plan.md)
    QString planStatus; // "" | "pending" (waiting for the person) | "accepted" | "rejected"
    QList<TodoItem> todos; // the agent's own checklist for a complex task (todo_write)
    QString summary;       // what the compacted (older) messages said
    int compactedUpTo = 0; // messages before this index are summarized and no longer sent
    int lastPromptTokens = 0; // what the provider said the last request held (0 = not reported)
    bool evicted = false;     // the messages were let go to save memory; they are read from the task's file when it is needed again
};

inline QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Ids end up in file names (a task's file, its plan folder) and as keys of saved secrets, and they come from a file that can
// be edited or synced: only plain names pass, never "../..", a path, or an empty string.
inline bool isSafeId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return re.match(id).hasMatch() && !id.contains(QLatin1String(".."));
}

// A link in an answer that may be opened: the model writes the text, so only the web and mail, never file: or an app's scheme.
inline bool isSafeLink(const QString &url)
{
    const QUrl u(url.trimmed());
    const QString scheme = u.scheme().toLower();
    return u.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("mailto"));
}

// A command the person ran with `!`, as the agent sees it in its history (the output is cut to keep the context small).
inline QString shellContextText(const ChatMessage &m)
{
    const ToolCall *tc = m.toolCalls.isEmpty() ? nullptr : &m.toolCalls.first();
    QString out = tc ? tc->output : QString();
    if (out.size() > 8000) out = QStringLiteral("[output cut: the last 8000 characters]\n") + out.right(8000); // the end is where errors and results are
    return QStringLiteral("I ran `") + (tc ? tc->command : m.content) + QStringLiteral("` in the workspace. Output:\n```\n") + out + QStringLiteral("\n```");
}

// The end of a run, from what really happened (not from what the model says): which files were created or edited and by how much,
// how many commands ran and how many failed, how many proposals were declined. A run is everything after the last message from the
// person ("Continue" after a cut answer is part of the same run).
struct RunFile { QString path, verb; int added = 0, removed = 0; };
struct RunSummary {
    QList<RunFile> files;
    int commands = 0, failedCommands = 0, declined = 0, added = 0, removed = 0;
    QStringList failed;   // the commands that failed, as typed
    bool any() const { return !files.isEmpty() || commands > 0 || declined > 0; }
};
inline RunSummary summarizeRun(const QList<ChatMessage> &msgs)
{
    int from = 0;
    for (int i = int(msgs.size()) - 1; i >= 0; --i)
        if (msgs.at(i).role == QLatin1String("user") && msgs.at(i).shown != QLatin1String("Continue")) { from = i + 1; break; }
    RunSummary r;
    static const QRegularExpression exitCode(QStringLiteral("\\[exit (-?\\d+)\\]\\s*$"));
    for (int i = from; i < msgs.size(); ++i) {
        const ChatMessage &m = msgs.at(i);
        if (m.role != QLatin1String("assistant") && m.role != QLatin1String("shell")) continue;
        for (const ToolCall &tc : m.toolCalls) {
            if (tc.status == QLatin1String("rejected")) { if (tc.name != QLatin1String("list_files") && tc.name != QLatin1String("read_file") && tc.name != QLatin1String("search_files")) ++r.declined; continue; }
            if (tc.name == QLatin1String("propose_edit") && tc.status == QLatin1String("applied")) {
                const QString path = tc.filePath.isEmpty() ? QJsonDocument::fromJson(tc.argumentsJson.toUtf8()).object().value(QStringLiteral("path")).toString() : tc.filePath;
                RunFile *found = nullptr;
                for (RunFile &f : r.files) if (f.path == path) found = &f;
                if (!found) { r.files.append({path, tc.originalExists ? QStringLiteral("Edited") : QStringLiteral("Created"), 0, 0}); found = &r.files.last(); }
                found->added += tc.addedLines; found->removed += tc.removedLines;
                r.added += tc.addedLines; r.removed += tc.removedLines;
            } else if (tc.name == QLatin1String("run_command") && (tc.status == QLatin1String("completed") || tc.status == QLatin1String("error"))) {
                ++r.commands;
                const auto mt = exitCode.match(tc.output);
                if (tc.status == QLatin1String("error") || (mt.hasMatch() && mt.captured(1) != QLatin1String("0"))) { ++r.failedCommands; if (r.failed.size() < 3) r.failed.append(tc.command); }
            }
        }
    }
    return r;
}

// What a tool call looks like as one line in the chat: "Edited  src/loans/late-fee.ts  +12 −3". The details (diff, command, output)
// open on a click; only a call that waits for Apply / Run is open by itself.
struct ToolHeadline {
    QString verb, target, stat, tone, icon; // tone: neutral | accent | ok | warn | bad
};
inline QString elideMiddle(const QString &s, int max)
{
    const QString one = s.simplified();
    if (one.size() <= max) return one;
    const int head = (max - 1) / 2, tail = max - 1 - head;
    return one.left(head) + QStringLiteral("…") + one.right(tail);
}
inline ToolHeadline toolHeadline(const ToolCall &tc)
{
    const QJsonObject args = QJsonDocument::fromJson(tc.argumentsJson.toUtf8()).object();
    const QString st = tc.status;
    ToolHeadline h;
    h.tone = st == QLatin1String("error") ? QStringLiteral("bad") : st == QLatin1String("rejected") ? QStringLiteral("warn") : st == QLatin1String("awaiting_approval") || st == QLatin1String("running") ? QStringLiteral("accent") : QStringLiteral("neutral");
    const QString path = elideMiddle(args.value(QStringLiteral("path")).toString(QStringLiteral(".")), 56);
    if (tc.name == QLatin1String("list_files")) { h.verb = QStringLiteral("Listed"); h.target = path; h.icon = QStringLiteral("folder"); }
    else if (tc.name == QLatin1String("read_file")) {
        h.verb = QStringLiteral("Read"); h.target = elideMiddle(args.value(QStringLiteral("path")).toString(), 56); h.icon = QStringLiteral("eye");
        if (args.contains(QStringLiteral("start_line"))) h.stat = QStringLiteral("lines ") + QString::number(args.value(QStringLiteral("start_line")).toInt())
            + QStringLiteral("–") + (args.contains(QStringLiteral("end_line")) ? QString::number(args.value(QStringLiteral("end_line")).toInt()) : QStringLiteral("…"));
    } else if (tc.name == QLatin1String("search_files")) {
        h.verb = QStringLiteral("Searched"); h.target = QStringLiteral("“") + elideMiddle(args.value(QStringLiteral("pattern")).toString(), 40) + QStringLiteral("”"); h.icon = QStringLiteral("search");
        if (st == QLatin1String("completed")) { int hits = 0; for (const QString &l : tc.output.split(u'\n')) if (!l.trimmed().isEmpty() && !l.startsWith(QLatin1String("[…"))) ++hits; h.stat = hits == 1 ? QStringLiteral("1 hit") : QString::number(hits) + QStringLiteral(" hits"); }
    } else if (tc.name == QLatin1String("propose_edit")) {
        const QString file = elideMiddle(tc.filePath.isEmpty() ? args.value(QStringLiteral("path")).toString() : tc.filePath, 56);
        h.icon = QStringLiteral("pen"); h.target = file;
        if (st == QLatin1String("awaiting_approval") || st == QLatin1String("pending")) h.verb = QStringLiteral("Proposed edit to");
        else if (st == QLatin1String("applied")) { h.verb = tc.originalExists ? QStringLiteral("Edited") : QStringLiteral("Created"); h.tone = QStringLiteral("ok"); }
        else if (st == QLatin1String("rejected")) h.verb = QStringLiteral("Declined edit to");
        else h.verb = QStringLiteral("Could not edit");
        if (tc.addedLines || tc.removedLines) h.stat = QStringLiteral("+") + QString::number(tc.addedLines) + QStringLiteral(" −") + QString::number(tc.removedLines);
    } else if (tc.name == QLatin1String("run_command")) {
        h.icon = QStringLiteral("server"); h.target = QStringLiteral("`") + elideMiddle(tc.command.isEmpty() ? args.value(QStringLiteral("command")).toString() : tc.command, 60) + QStringLiteral("`");
        h.verb = st == QLatin1String("awaiting_approval") || st == QLatin1String("pending") ? QStringLiteral("Wants to run") : st == QLatin1String("running") ? QStringLiteral("Running") : st == QLatin1String("rejected") ? QStringLiteral("Skipped") : QStringLiteral("Ran");
        static const QRegularExpression exitCode(QStringLiteral("\\[exit (-?\\d+)\\]\\s*$"));
        const auto m = exitCode.match(tc.output);
        if (m.hasMatch()) { h.stat = QStringLiteral("exit ") + m.captured(1); h.tone = m.captured(1) == QLatin1String("0") ? QStringLiteral("ok") : QStringLiteral("bad"); }
    } else if (tc.name == QLatin1String("write_plan")) { h.verb = QStringLiteral("Wrote plan"); h.target = QStringLiteral("plan.md"); h.icon = QStringLiteral("pen"); if (st == QLatin1String("completed")) h.tone = QStringLiteral("ok"); }
    else if (tc.name == QLatin1String("todo_write")) {
        h.verb = QStringLiteral("To-do updated"); h.icon = QStringLiteral("check");
        static const QRegularExpression frac(QStringLiteral("(\\d+/\\d+)"));
        const auto m = frac.match(tc.summary); if (m.hasMatch()) h.stat = m.captured(1);
        if (st == QLatin1String("completed")) h.tone = QStringLiteral("ok");
    } else { h.verb = tc.name; h.icon = QStringLiteral("gear"); }
    if (st == QLatin1String("error") && h.stat.isEmpty()) h.stat = QStringLiteral("failed");
    return h;
}

// The agent's own checklist. todo_write sends the whole list each time; this checks it and cleans it up.
inline QList<TodoItem> parseTodos(const QJsonObject &args, QString *error = nullptr)
{
    QList<TodoItem> out;
    if (!args.value(QStringLiteral("todos")).isArray()) { if (error) *error = QStringLiteral("`todos` must be an array."); return out; }
    for (const QJsonValue &v : args.value(QStringLiteral("todos")).toArray()) {
        const QJsonObject o = v.toObject();
        TodoItem t;
        t.content = o.value(QStringLiteral("content")).toString().simplified().left(200);
        const QString st = o.value(QStringLiteral("status")).toString().trimmed().toLower();
        t.status = st == QLatin1String("completed") || st == QLatin1String("in_progress") ? st : QStringLiteral("pending");
        if (t.content.isEmpty()) continue;
        out.append(t);
        if (out.size() > 30) { if (error) *error = QStringLiteral("At most 30 to-do items."); return {}; }
    }
    if (error) error->clear();
    return out;
}
inline int todosDone(const QList<TodoItem> &todos)
{
    int n = 0;
    for (const TodoItem &t : todos) if (t.status == QLatin1String("completed")) ++n;
    return n;
}
// Part of the context of every turn, so the list survives a compact.
inline QString todoContextPrompt(const QList<TodoItem> &todos)
{
    if (todos.isEmpty()) return {};
    QString s = QStringLiteral("Your current to-do list (keep it up to date with `todo_write`):\n");
    for (const TodoItem &t : todos)
        s += (t.status == QLatin1String("completed") ? QStringLiteral("[x] ") : t.status == QLatin1String("in_progress") ? QStringLiteral("[~] ") : QStringLiteral("[ ] ")) + t.content + QLatin1Char('\n');
    return s;
}


inline QList<ProviderConfig> defaultProviders()
{
    QList<ProviderConfig> providers{
        {
            QStringLiteral("mimo"),
            QStringLiteral("Xiaomi MiMo"),
            QStringLiteral("openai"),
            QStringLiteral("https://api.xiaomimimo.com/v1"),
            QStringLiteral("mimo-v2.6-pro"),
            {QStringLiteral("mimo-v2.6-pro"), QStringLiteral("mimo-v2.6-flash")},
            0.2,
            8192,
            QStringLiteral("api-key"),
            {},
            {},
            QStringLiteral("https://mimo.mi.com/docs/en-US/quick-start/summary/welcome"),
            true
        },
        {
            QStringLiteral("deepseek"),
            QStringLiteral("DeepSeek"),
            QStringLiteral("openai"),
            QStringLiteral("https://api.deepseek.com"),
            QStringLiteral("deepseek-flash"),
            {QStringLiteral("deepseek-flash"), QStringLiteral("deepseek-v4-pro")},
            0.2,
            8192,
            QStringLiteral("Bearer"),
            {},
            {},
            QStringLiteral("https://api-docs.deepseek.com/"),
            true
        },
        {
            QStringLiteral("claude"),
            QStringLiteral("Claude (Anthropic)"),
            QStringLiteral("anthropic"),
            QStringLiteral("https://api.anthropic.com/v1"),
            QStringLiteral("claude-sonnet-4-20250514"),
            {QStringLiteral("claude-sonnet-4-20250514"), QStringLiteral("claude-3-7-sonnet-latest"), QStringLiteral("claude-3-5-haiku-latest")},
            0.2,
            8192,
            QStringLiteral("x-api-key"),
            {},
            {},
            QStringLiteral("https://docs.anthropic.com/"),
            true
        },
        {
            QStringLiteral("openai"),
            QStringLiteral("OpenAI"),
            QStringLiteral("openai"),
            QStringLiteral("https://api.openai.com/v1"),
            QStringLiteral("gpt-4.1"),
            {QStringLiteral("gpt-4.1"), QStringLiteral("gpt-4.1-mini"), QStringLiteral("gpt-4o"), QStringLiteral("o4-mini")},
            0.2,
            8192,
            QStringLiteral("Bearer"),
            {},
            {},
            QStringLiteral("https://platform.openai.com/docs"),
            true
        }
    };
    providers[0].maxTokensParameter = QStringLiteral("max_completion_tokens");
    return providers;
}

// Normalize base URL and build the chat/messages endpoint.
// Handles base URLs with or without trailing slash or `/v1` or full endpoint path.
inline QUrl chatEndpointUrl(const ProviderConfig &p)
{
    QString u = p.baseUrl.trimmed();
    while (u.endsWith(u'/'))
        u.chop(1);
    if (normalizeProtocol(p.protocol) == QLatin1String("anthropic")) {
        if (u.endsWith(QLatin1String("/messages")))
            return QUrl(u);
        if (!u.endsWith(QLatin1String("/v1")))
            u += QStringLiteral("/v1");
        return QUrl(u + QStringLiteral("/messages"));
    }
    if (u.endsWith(QLatin1String("/chat/completions")))
        return QUrl(u);
    return QUrl(u + QStringLiteral("/chat/completions"));
}

inline QUrl modelsEndpointUrl(const ProviderConfig &p)
{
    QString u = p.baseUrl.trimmed();
    while (u.endsWith(u'/'))
        u.chop(1);
    if (u.endsWith(QLatin1String("/chat/completions")))
        u.chop(17);
    else if (u.endsWith(QLatin1String("/messages")))
        u.chop(9);
    return QUrl(u + QStringLiteral("/models"));
}

// The ids a /models response lists ({"data":[{"id":...}]}), in the order the provider gave them.
inline QStringList parseModelIds(const QByteArray &json, int limit = 500)
{
    QStringList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toArray()) {
        const QString id = v.toObject().value(QStringLiteral("id")).toString().trimmed();
        if (!id.isEmpty() && !out.contains(id)) out.append(id);
        if (out.size() >= limit) break;
    }
    return out;
}

// The category a model id belongs to in the model menu. Guessed from the id alone, so it works for any provider;
// the order here is the order the menu shows them in.
inline QStringList modelGroups()
{
    return {QStringLiteral("Opus"), QStringLiteral("Sonnet"), QStringLiteral("Haiku"), QStringLiteral("GPT-5"), QStringLiteral("GPT-4.1"),
            QStringLiteral("GPT-4o"), QStringLiteral("GPT-4"), QStringLiteral("GPT-3.5"), QStringLiteral("Reasoning"), QStringLiteral("Pro"),
            QStringLiteral("Flash"), QStringLiteral("Chat"), QStringLiteral("Coding"), QStringLiteral("Embeddings"), QStringLiteral("Audio"),
            QStringLiteral("Images"), QStringLiteral("Moderation"), QStringLiteral("Other")};
}
inline QString modelGroup(const QString &modelId)
{
    const QString id = modelId.toLower();
    auto has = [&id](std::initializer_list<const char *> words) {
        for (const char *w : words) if (id.contains(QLatin1String(w))) return true;
        return false;
    };
    static const QRegularExpression oSeries(QStringLiteral("^(o\\d|.*[/:-]o\\d)(-|$)"));
    if (has({"embed"})) return QStringLiteral("Embeddings");
    if (has({"moderation"})) return QStringLiteral("Moderation");
    if (has({"whisper", "tts", "audio", "speech", "transcribe", "realtime"})) return QStringLiteral("Audio");
    if (has({"dall-e", "gpt-image", "image", "imagen"})) return QStringLiteral("Images");
    if (has({"opus"})) return QStringLiteral("Opus");
    if (has({"sonnet"})) return QStringLiteral("Sonnet");
    if (has({"haiku"})) return QStringLiteral("Haiku");
    if (has({"gpt-5"})) return QStringLiteral("GPT-5");
    if (has({"gpt-4.1"})) return QStringLiteral("GPT-4.1");
    if (has({"gpt-4o"})) return QStringLiteral("GPT-4o");
    if (has({"gpt-4"})) return QStringLiteral("GPT-4");
    if (has({"gpt-3.5"})) return QStringLiteral("GPT-3.5");
    if (has({"reason", "thinking", "-r1"}) || oSeries.match(id).hasMatch()) return QStringLiteral("Reasoning");
    if (has({"coder", "codex", "code"})) return QStringLiteral("Coding");
    if (has({"pro"})) return QStringLiteral("Pro");
    if (has({"flash", "mini", "lite", "turbo"})) return QStringLiteral("Flash");
    if (has({"chat", "instruct"})) return QStringLiteral("Chat");
    return QStringLiteral("Other");
}

// Workspace path sandboxing: resolves `relOrAbsPath` against `workspaceRoot` and ensures the
// target stays strictly inside `workspaceRoot` (prevents `../` traversal).
inline QString resolveWorkspacePath(const QString &workspaceRoot, const QString &relOrAbsPath, bool *ok = nullptr)
{
    if (ok)
        *ok = false;
    if (workspaceRoot.trimmed().isEmpty()) return {};
    const QString rootClean = QFileInfo(workspaceRoot).canonicalFilePath();
    if (rootClean.isEmpty() || !QFileInfo(rootClean).isDir())
        return {};
    const QString input = relOrAbsPath.trimmed();
    const QString candidate = QFileInfo(input).isAbsolute()
                                  ? QDir::cleanPath(input)
                                  : QDir::cleanPath(rootClean + QLatin1Char('/') + input);
    const QString rootWithSlash = rootClean.endsWith(u'/') ? rootClean : (rootClean + QLatin1Char('/'));
    if (candidate != rootClean && !candidate.startsWith(rootWithSlash))
        return {};
    // Reject symlinks, including dangling links, in every component. A lexical prefix
    // alone would allow reading/writing outside the selected workspace.
    QString current = rootClean;
    const QString rel = QDir(rootClean).relativeFilePath(candidate);
    if (rel != QLatin1String(".")) {
        for (const QString &part : rel.split(u'/')) {
            current += QLatin1Char('/') + part;
            if (QFileInfo(current).isSymLink()) return {};
        }
    }
    if (ok)
        *ok = true;
    return candidate;
}

// Unified line diff preview for proposed file edits (+added / -removed summary).
struct DiffResult {
    QString unified;
    int added = 0;
    int removed = 0;
};

inline DiffResult computeUnifiedDiff(const QString &filePath, const QString &oldText, const QString &newText)
{
    DiffResult res;
    const QStringList oldLines = oldText.isEmpty() ? QStringList() : oldText.split(u'\n');
    const QStringList newLines = newText.isEmpty() ? QStringList() : newText.split(u'\n');

    const int n = oldLines.size();
    const int m = newLines.size();

    QStringList out;
    out << QStringLiteral("--- a/%1").arg(filePath);
    out << QStringLiteral("+++ b/%1").arg(filePath);

    // Bounded LCS for readable diffs on typical source files; fallback line-by-line for huge files.
    if (qint64(n) * qint64(m) <= 250000) {
        QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));
        for (int i = n - 1; i >= 0; --i) {
            for (int j = m - 1; j >= 0; --j) {
                if (oldLines[i] == newLines[j])
                    dp[i][j] = 1 + dp[i + 1][j + 1];
                else
                    dp[i][j] = qMax(dp[i + 1][j], dp[i][j + 1]);
            }
        }
        int i = 0, j = 0;
        int skipped = 0;
        while (i < n || j < m) {
            if (i < n && j < m && oldLines[i] == newLines[j]) {
                if (skipped < 2 || (i + 1 < n && j + 1 < m && oldLines[i + 1] != newLines[j + 1])) {
                    out << (QStringLiteral("  ") + oldLines[i]);
                    skipped = 0;
                } else if (skipped == 2) {
                    out << QStringLiteral("  ...");
                    ++skipped;
                } else {
                    ++skipped;
                }
                ++i;
                ++j;
            } else if (j < m && (i == n || dp[i][j + 1] >= dp[i + 1][j])) {
                out << (QStringLiteral("+ ") + newLines[j]);
                ++res.added;
                skipped = 0;
                ++j;
            } else {
                out << (QStringLiteral("- ") + oldLines[i]);
                ++res.removed;
                skipped = 0;
                ++i;
            }
        }
    } else {
        for (const QString &l : oldLines) {
            out << (QStringLiteral("- ") + l);
            ++res.removed;
        }
        for (const QString &l : newLines) {
            out << (QStringLiteral("+ ") + l);
            ++res.added;
        }
    }
    res.unified = out.join(u'\n');
    return res;
}

// System prompt tailored to the active mode (Plan Mode vs Manual Approval vs Auto Approval).
inline QString systemPromptFor(const QString &mode, const QString &workspace, const QString &customInstructions = {})
{
    const QString m = normalizeMode(mode);
    QString prompt = QStringLiteral(
        "You are the Coding Assistant inside Olyzano Notch. "
        "Be concise, accurate, and ground your answers in the workspace files.\n"
        "Workspace root: %1\n")
        .arg(workspace.isEmpty() ? QStringLiteral("(none selected)") : workspace);
    // Credit: adapted from the "karpathy-guidelines" skill (multica-ai/andrej-karpathy-skills, MIT), which distills
    // Andrej Karpathy's observations on common LLM coding mistakes. Paraphrased and shortened here.
    prompt += QStringLiteral(
        "Working principles (they favour care over speed; use judgment on trivial tasks):\n"
        "1. Think before coding. State your assumptions. If the request has several readings, list them instead of choosing silently. "
        "Say so when a simpler approach exists. If something is unclear, stop and ask.\n"
        "2. Simplicity first. Write the least code that solves the stated problem: no extra features, no abstraction for one use, "
        "no configurability nobody asked for, no handling of cases that cannot happen.\n"
        "3. Surgical changes. Touch only what the request needs, match the existing style, and leave unrelated code alone (mention it instead of changing it). "
        "Remove only what your own change made unused.\n"
        "4. Goal-driven execution. Turn the task into checkable goals (reproduce a bug with a test, then fix it; run the tests before and after a refactor). "
        "For multi-step work give a short plan with a check for each step, and verify before you say it is done.\n");

    if (m == QLatin1String("plan")) {
        prompt += QStringLiteral(
            "CURRENT MODE: PLAN MODE (Read-Only).\n"
            "- You may ONLY inspect the project using `list_files`, `read_file`, and `search_files`.\n"
            "- Do NOT modify any files or run any shell commands.\n"
            "- Discuss the task with the person: ask what you need, cite relevant file paths and line numbers.\n"
            "- When you have a concrete plan, call `write_plan` once with the whole plan as Markdown laid out like a short document: `# Title`, "
            "`## Context` (why, in two or three sentences), `## Steps` (numbered; each step names the files it touches), `## Verification` (how it is checked). "
            "Put file paths, commands and identifiers in `backticks`. No filler. "
            "It is saved as plan.md and shown to the person, who can accept it, reject it or ask for changes. "
            "To change a plan, call `write_plan` again with the full updated plan. Do not call it for plain questions.\n"
            "- After `write_plan`, answer in at most two sentences and stop: do not start the work, the person decides.\n");
    } else if (m == QLatin1String("manual")) {
        prompt += QStringLiteral(
            "CURRENT MODE: MANUAL APPROVAL.\n"
            "- Read-only tools (`list_files`, `read_file`, `search_files`) run immediately.\n"
            "- Use `propose_edit` to create or modify files, and `run_command` for build/test commands; the user will review the diff or command and click Apply/Run.\n"
            "- For a task with three or more steps, call `todo_write` first with the steps, keep exactly one item `in_progress`, and update the list as you finish each step. Skip it for simple requests.\n"
            "- When you finish work that changed files or ran commands, end with a conclusion that is easy to scan, in the language the person used. "
            "First one bold line with the result. Then short labelled parts, each a bold label on its own line followed by two to four bullets, with a blank line between parts: "
            "**Changed** (what and why; file paths in `backticks`), **Checked** (what you ran or looked at and what it showed), and **Left** or **Good to know** (only if something is). "
            "No long paragraphs, no code blocks. For a plain question just answer; no summary.\n");
    } else {
        prompt += QStringLiteral(
            "CURRENT MODE: AUTO APPROVAL.\n"
            "- Tools (`list_files`, `read_file`, `search_files`, `propose_edit`, `run_command`) execute automatically in the workspace.\n"
            "- Make surgical, well-tested changes and verify your work.\n"
            "- For a task with three or more steps, call `todo_write` first with the steps, keep exactly one item `in_progress`, and update the list as you finish each step. Skip it for simple requests.\n"
            "- When you finish work that changed files or ran commands, end with a conclusion that is easy to scan, in the language the person used. "
            "First one bold line with the result. Then short labelled parts, each a bold label on its own line followed by two to four bullets, with a blank line between parts: "
            "**Changed** (what and why; file paths in `backticks`), **Checked** (what you ran or looked at and what it showed), and **Left** or **Good to know** (only if something is). "
            "No long paragraphs, no code blocks. For a plain question just answer; no summary.\n");
    }
    if (!customInstructions.trimmed().isEmpty())
        prompt += QStringLiteral("\nAdditional instructions:\n") + customInstructions.trimmed() + QLatin1Char('\n');
    return prompt;
}

// Tool schemas for OpenAI-compatible (MiMo / DeepSeek / OpenAI) and Anthropic protocols.
inline QJsonArray openAiToolsForMode(const QString &mode)
{
    const QString m = normalizeMode(mode);
    auto makeTool = [](const QString &name, const QString &desc, const QJsonObject &props, const QStringList &req) {
        QJsonArray reqArr;
        for (const QString &r : req)
            reqArr.append(r);
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{
                {QStringLiteral("name"), name},
                {QStringLiteral("description"), desc},
                {QStringLiteral("parameters"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("properties"), props},
                    {QStringLiteral("required"), reqArr}
                }}
            }}
        };
    };

    QJsonArray tools;
    tools.append(makeTool(
        QStringLiteral("list_files"),
        QStringLiteral("List files and folders in a relative workspace directory (default '.')."),
        QJsonObject{
            {QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Relative directory path inside workspace")}}}
        },
        {}));
    tools.append(makeTool(
        QStringLiteral("read_file"),
        QStringLiteral("Read the contents of a file in the workspace with line numbers."),
        QJsonObject{
            {QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Relative file path inside workspace")}}},
            {QStringLiteral("start_line"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Optional 1-based start line")}}},
            {QStringLiteral("end_line"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("description"), QStringLiteral("Optional 1-based end line")}}}
        },
        {QStringLiteral("path")}));
    tools.append(makeTool(
        QStringLiteral("search_files"),
        QStringLiteral("Search file contents in the workspace using a case-insensitive literal substring. Results are bounded."),
        QJsonObject{
            {QStringLiteral("pattern"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Literal text to search for")}}},
            {QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Optional relative subdirectory to narrow search")}}}
        },
        {QStringLiteral("pattern")}));

    if (m == QLatin1String("plan")) {
        tools.append(makeTool(
            QStringLiteral("write_plan"),
            QStringLiteral("Save the implementation plan (Markdown) as plan.md for the person to read and approve. Always send the complete plan, not a diff."),
            QJsonObject{
                {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("The whole plan in Markdown")}}}
            },
            {QStringLiteral("content")}));
    }
    if (m != QLatin1String("plan")) {
        tools.append(makeTool(
            QStringLiteral("propose_edit"),
            QStringLiteral("Create or edit a file in the workspace. Provide either `old_text` + `new_text` to replace a section, or `new_text` alone to write/overwrite the file."),
            QJsonObject{
                {QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Relative file path inside workspace")}}},
                {QStringLiteral("old_text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Exact existing text to replace (optional when writing full file)")}}},
                {QStringLiteral("new_text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Replacement text (or full file content if old_text is omitted)")}}},
                {QStringLiteral("description"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Short summary of the change")}}}
            },
            {QStringLiteral("path"), QStringLiteral("new_text")}));
        tools.append(makeTool(
            QStringLiteral("todo_write"),
            QStringLiteral("Write your to-do list for a multi-step task. Send the complete list every time (it replaces the old one). Each item has `content` and `status` (pending, in_progress or completed); keep one in_progress."),
            QJsonObject{
                {QStringLiteral("todos"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("array")},
                    {QStringLiteral("description"), QStringLiteral("The complete list")},
                    {QStringLiteral("items"), QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("object")},
                        {QStringLiteral("properties"), QJsonObject{
                            {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                            {QStringLiteral("status"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("pending"), QStringLiteral("in_progress"), QStringLiteral("completed")}}}}
                        }},
                        {QStringLiteral("required"), QJsonArray{QStringLiteral("content"), QStringLiteral("status")}}
                    }}
                }}
            },
            {QStringLiteral("todos")}));
        tools.append(makeTool(
            QStringLiteral("run_command"),
            QStringLiteral("Run a shell command in the workspace root (e.g. build, test, git status)."),
            QJsonObject{
                {QStringLiteral("command"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Shell command to execute")}}},
                {QStringLiteral("description"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("Short explanation of why the command is needed")}}}
            },
            {QStringLiteral("command")}));
    }
    return tools;
}

inline QJsonArray anthropicToolsForMode(const QString &mode)
{
    const QJsonArray oa = openAiToolsForMode(mode);
    QJsonArray out;
    for (const QJsonValue &v : oa) {
        const QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
        out.append(QJsonObject{
            {QStringLiteral("name"), fn.value(QStringLiteral("name"))},
            {QStringLiteral("description"), fn.value(QStringLiteral("description"))},
            {QStringLiteral("input_schema"), fn.value(QStringLiteral("parameters"))}
        });
    }
    return out;
}

// Reasoning effort the person picked: "" (send nothing, the provider's default), low, medium, high, xhigh ("Extra") or max.
// Sent as `reasoning_effort` (OpenAI-compatible, DeepSeek) or `output_config.effort` (Anthropic). Which levels a model
// accepts depends on the model, so nothing is sent until a level is chosen.
inline QString normalizeEffort(const QString &effort)
{
    const QString e = effort.trimmed().toLower();
    if (e == QLatin1String("extra")) return QStringLiteral("xhigh");
    for (const char *ok : {"low", "medium", "high", "xhigh", "max"})
        if (e == QLatin1String(ok)) return e;
    return {};
}

// Build request JSON payload for OpenAI-compatible (Xiaomi MiMo, DeepSeek, OpenAI, Custom) or Anthropic.
inline QByteArray buildRequestPayload(const ProviderConfig &provider,
                                      const QString &mode,
                                      const QString &workspace,
                                      const QList<ChatMessage> &history,
                                      bool stream = true,
                                      const QString &customSystemPrompt = {},
                                      const QString &effort = {})
{
    const QString level = normalizeEffort(effort);
    const QString sys = systemPromptFor(mode, workspace, customSystemPrompt);
    const QString proto = normalizeProtocol(provider.protocol);

    if (proto == QLatin1String("anthropic")) {
        QJsonArray msgs;
        for (const ChatMessage &m : history) {
            if (m.role == QLatin1String("user") || m.role == QLatin1String("shell")) {
                msgs.append(QJsonObject{
                    {QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), m.role == QLatin1String("shell") ? shellContextText(m) : m.content}
                });
            } else if (m.role == QLatin1String("assistant")) {
                QJsonArray blocks;
                if (!m.content.isEmpty()) {
                    blocks.append(QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"), m.content}
                    });
                }
                for (const ToolCall &tc : m.toolCalls) {
                    QJsonObject inputObj = QJsonDocument::fromJson(tc.argumentsJson.toUtf8()).object();
                    blocks.append(QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("tool_use")},
                        {QStringLiteral("id"), tc.id},
                        {QStringLiteral("name"), tc.name},
                        {QStringLiteral("input"), inputObj}
                    });
                }
                if (!blocks.isEmpty()) {
                    msgs.append(QJsonObject{
                        {QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), blocks}
                    });
                }
            } else if (m.role == QLatin1String("tool")) {
                msgs.append(QJsonObject{
                    {QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), QJsonArray{
                        QJsonObject{
                            {QStringLiteral("type"), QStringLiteral("tool_result")},
                            {QStringLiteral("tool_use_id"), m.toolCallId},
                            {QStringLiteral("content"), m.content}
                        }
                    }}
                });
            }
        }
        QJsonObject root{
            {QStringLiteral("model"), provider.modelId},
            {QStringLiteral("max_tokens"), qMax(256, provider.maxTokens)},
            {QStringLiteral("system"), sys},
            {QStringLiteral("messages"), msgs},
            {QStringLiteral("tools"), anthropicToolsForMode(mode)},
            {QStringLiteral("stream"), stream}
        };
        if (provider.temperature >= 0.0)
            root.insert(QStringLiteral("temperature"), provider.temperature);
        if (!provider.extraBodyJson.trimmed().isEmpty()) {
            const QJsonObject extra = QJsonDocument::fromJson(provider.extraBodyJson.toUtf8()).object();
            for (auto it = extra.begin(); it != extra.end(); ++it)
                if (it.key() != "model" && it.key() != "messages" && it.key() != "tools" && it.key() != "stream" && it.key() != "system")
                    root.insert(it.key(), it.value());
        }
        if (!level.isEmpty()) root.insert(QStringLiteral("output_config"), QJsonObject{{QStringLiteral("effort"), level}});
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    // OpenAI-compatible Chat Completions (Xiaomi MiMo, DeepSeek, OpenAI, Custom)
    QJsonArray msgs;
    msgs.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), sys}
    });
    const bool isDeepSeekReasoner = provider.modelId.contains(QLatin1String("reasoner"), Qt::CaseInsensitive);
    for (const ChatMessage &m : history) {
        if (m.role == QLatin1String("user") || m.role == QLatin1String("shell")) {
            msgs.append(QJsonObject{
                {QStringLiteral("role"), QStringLiteral("user")},
                {QStringLiteral("content"), m.role == QLatin1String("shell") ? shellContextText(m) : m.content}
            });
        } else if (m.role == QLatin1String("assistant")) {
            QJsonObject obj{
                {QStringLiteral("role"), QStringLiteral("assistant")},
                {QStringLiteral("content"), m.content}
            };
            if (isDeepSeekReasoner || !m.reasoning.isEmpty())
                obj.insert(QStringLiteral("reasoning_content"), m.reasoning);
            if (!m.toolCalls.isEmpty()) {
                QJsonArray tcArr;
                for (const ToolCall &tc : m.toolCalls) {
                    tcArr.append(QJsonObject{
                        {QStringLiteral("id"), tc.id},
                        {QStringLiteral("type"), QStringLiteral("function")},
                        {QStringLiteral("function"), QJsonObject{
                            {QStringLiteral("name"), tc.name},
                            {QStringLiteral("arguments"), tc.argumentsJson.isEmpty() ? QStringLiteral("{}") : tc.argumentsJson}
                        }}
                    });
                }
                obj.insert(QStringLiteral("tool_calls"), tcArr);
            }
            msgs.append(obj);
        } else if (m.role == QLatin1String("tool")) {
            msgs.append(QJsonObject{
                {QStringLiteral("role"), QStringLiteral("tool")},
                {QStringLiteral("tool_call_id"), m.toolCallId},
                {QStringLiteral("content"), m.content}
            });
        }
    }

    QJsonObject root{
        {QStringLiteral("model"), provider.modelId},
        {QStringLiteral("messages"), msgs},
        {QStringLiteral("tools"), openAiToolsForMode(mode)},
        {QStringLiteral("stream"), stream}
    };
    if (provider.maxTokens > 0)
        root.insert(provider.maxTokensParameter, provider.maxTokens);
    // These two report usage in the stream when asked; other OpenAI-compatible servers may reject the option, so they are not asked.
    if (stream) {
        const QString host = QUrl(provider.baseUrl).host();
        if (host == QLatin1String("api.openai.com") || host == QLatin1String("api.deepseek.com"))
            root.insert(QStringLiteral("stream_options"), QJsonObject{{QStringLiteral("include_usage"), true}});
    }
    // DeepSeek-Reasoner ignores/rejects temperature in some versions if non-standard; keep for standard models.
    if (!isDeepSeekReasoner && provider.temperature >= 0.0)
        root.insert(QStringLiteral("temperature"), provider.temperature);
    if (!provider.extraBodyJson.trimmed().isEmpty()) {
        const QJsonObject extra = QJsonDocument::fromJson(provider.extraBodyJson.toUtf8()).object();
        for (auto it = extra.begin(); it != extra.end(); ++it)
            if (it.key() != "model" && it.key() != "messages" && it.key() != "tools" && it.key() != "stream" && it.key() != "system")
                root.insert(it.key(), it.value());
    }
    if (!level.isEmpty()) root.insert(QStringLiteral("reasoning_effort"), level);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// A request that failed for a reason that may pass (a busy server, a rate limit, a dropped connection) is tried again, up to
// MaxRetries times, waiting longer each time. A wrong key, a bad request or an exhausted quota will not get better by itself.
constexpr int MaxRetries = 10;
inline bool isRetryableStatus(int status) { return status == 408 || status == 429 || (status >= 500 && status <= 599); } // 529 is Anthropic's "overloaded"
inline bool isRetryableMessage(const QString &message)
{
    static const QRegularExpression re(QStringLiteral("overload|rate.?limit|too many requests|try again|temporar|timed? ?out|timeout|capacity|busy|unavailable|server error|internal error|connection"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(message).hasMatch();
}
// attempt 1, 2, 3 ... -> 1 s, 2 s, 4 s, 8 s ... at most 30 s; a Retry-After from the server (seconds, at most 120) is respected.
inline int retryDelayMs(int attempt, int retryAfterSeconds = 0, int baseMs = 1000)
{
    qint64 d = qMax(1, baseMs);
    for (int i = 1; i < attempt && d < 30000; ++i) d *= 2;
    d = qMin<qint64>(d, 30000);
    if (retryAfterSeconds > 0) d = qMax<qint64>(d, qMin(retryAfterSeconds, 120) * 1000);
    return int(d);
}

// What a request carries of a session: the summary of what was compacted, then the messages that were not.
inline QList<ChatMessage> historyForRequest(const Session &s)
{
    QList<ChatMessage> out;
    if (!s.summary.isEmpty()) {
        ChatMessage m;
        m.id = QStringLiteral("summary");
        m.role = QStringLiteral("user");
        m.content = QStringLiteral("Summary of the earlier part of this conversation (those messages were compacted to save context):\n") + s.summary;
        out.append(m);
    }
    for (int i = qBound(0, s.compactedUpTo, int(s.messages.size())); i < s.messages.size(); ++i)
        out.append(s.messages.at(i));
    return out;
}

// Compact: where the kept part starts. Messages [from, cut) get summarized; the kept part begins at a message from the person,
// so a tool call is never separated from its result. -1: nothing worth compacting yet.
inline int compactCutIndex(const QList<ChatMessage> &msgs, int from, int keepLast = 6)
{
    const int n = int(msgs.size());
    const int target = n - keepLast; // the kept part would start here
    if (target - from < 2) return -1;
    auto starts = [&msgs](int i) { return msgs.at(i).role == QLatin1String("user") || msgs.at(i).role == QLatin1String("shell"); };
    for (int i = target; i - from >= 2; --i)
        if (starts(i)) return i;
    for (int i = target + 1; i < n; ++i)
        if (starts(i)) return i;
    return -1;
}

// The old messages as plain text for the summary request (no tool-call protocol, results cut).
inline QString flattenForSummary(const QList<ChatMessage> &msgs, int from, int to, int toolChars = 1200)
{
    QString out;
    for (int i = qMax(0, from); i < qMin(to, int(msgs.size())); ++i) {
        const ChatMessage &m = msgs.at(i);
        if (m.role == QLatin1String("user")) out += QStringLiteral("User: ") + m.content.left(4000) + QLatin1Char('\n');
        else if (m.role == QLatin1String("assistant")) {
            out += QStringLiteral("Assistant: ") + m.content.left(4000) + QLatin1Char('\n');
            for (const ToolCall &tc : m.toolCalls) out += QStringLiteral("  [called ") + tc.name + QLatin1Char(' ') + tc.argumentsJson.left(300) + QStringLiteral("]\n");
        } else if (m.role == QLatin1String("tool")) out += QStringLiteral("Tool result (") + m.toolName + QStringLiteral("): ") + m.content.left(toolChars) + QLatin1Char('\n');
        else if (m.role == QLatin1String("shell")) out += shellContextText(m).left(toolChars + 200) + QLatin1Char('\n');
    }
    return out;
}

// A separate request, without tools and without streaming, that asks for the summary.
inline QByteArray buildSummaryPayload(const ProviderConfig &provider, const QString &transcript, const QString &focus = {}, const QString &priorSummary = {})
{
    QString sys = QStringLiteral(
        "You compress a coding session so the work can go on after the older messages are dropped. "
        "Write a compact Markdown summary: the goal, decisions and why, files read or changed (with paths), commands run and what they showed, "
        "what is done, what is left, and anything the person asked you to remember. No preamble, under about 900 words.");
    if (!focus.trimmed().isEmpty()) sys += QStringLiteral(" Pay special attention to: ") + focus.trimmed() + QLatin1Char('.');
    const QString user = (priorSummary.isEmpty() ? QString() : QStringLiteral("Earlier summary:\n") + priorSummary + QStringLiteral("\n\n"))
        + QStringLiteral("Conversation to summarize:\n") + transcript;
    const int limit = qBound(1024, provider.maxTokens > 0 ? provider.maxTokens : 2048, 4096);
    QJsonObject root;
    if (normalizeProtocol(provider.protocol) == QLatin1String("anthropic")) {
        root = QJsonObject{{QStringLiteral("model"), provider.modelId}, {QStringLiteral("max_tokens"), limit}, {QStringLiteral("system"), sys},
            {QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), user}}}},
            {QStringLiteral("stream"), false}};
    } else {
        root = QJsonObject{{QStringLiteral("model"), provider.modelId}, {QStringLiteral("stream"), false},
            {QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), sys}},
                QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), user}}}}};
        root.insert(provider.maxTokensParameter, limit);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
inline QString summaryFromResponse(const QString &protocol, const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (normalizeProtocol(protocol) == QLatin1String("anthropic")) {
        QString text;
        for (const QJsonValue &b : o.value(QStringLiteral("content")).toArray())
            if (b.toObject().value(QStringLiteral("type")).toString() == QLatin1String("text")) text += b.toObject().value(QStringLiteral("text")).toString();
        return text.trimmed();
    }
    return o.value(QStringLiteral("choices")).toArray().first().toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString().trimmed();
}

// Extract `<think>...</think>` blocks emitted inline by reasoning models (e.g., Xiaomi MiMo-7B-RL).
inline void splitInlineThinkTags(const QString &rawContent, QString *cleanContent, QString *inlineReasoning)
{
    QString text = rawContent;
    QString thought;
    while (true) {
        const int start = text.indexOf(QLatin1String("<think>"));
        if (start < 0)
            break;
        const int end = text.indexOf(QLatin1String("</think>"), start + 7);
        if (end < 0) {
            // Still streaming inside <think>
            thought += text.mid(start + 7);
            text = text.left(start);
            break;
        }
        thought += text.mid(start + 7, end - (start + 7));
        text.remove(start, (end + 8) - start);
    }
    if (cleanContent)
        *cleanContent = text.trimmed().isEmpty() && !rawContent.isEmpty() && thought.isEmpty() ? rawContent : text;
    if (inlineReasoning)
        *inlineReasoning = thought.trimmed();
}

// Incremental SSE accumulator for OpenAI/MiMo/DeepSeek and Anthropic streams.
struct StreamAccumulator {
    QString content;
    QString reasoning;
    QList<ToolCall> toolCalls;
    QString finishReason;
    QString errorText;
    bool done = false;
    int activeAnthropicBlockToolIdx = -1;
    int promptTokens = 0;     // what the provider says the request held (0 = not reported)
    int completionTokens = 0;

    // Feed one SSE `data: ...` payload (already stripped of the leading `data:`).
    void feedDataLine(const QByteArray &jsonBytes, const QString &protocol)
    {
        const QByteArray trimmed = jsonBytes.trimmed();
        if (trimmed.isEmpty())
            return;
        if (trimmed == "[DONE]") {
            done = true;
            return;
        }
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return;
        const QJsonObject obj = doc.object();

        if (obj.contains(QStringLiteral("error"))) {
            const QJsonValue ev = obj.value(QStringLiteral("error"));
            errorText = ev.isObject() ? ev.toObject().value(QStringLiteral("message")).toString() : ev.toString();
            if (errorText.isEmpty())
                errorText = QString::fromUtf8(trimmed);
            return;
        }

        // Usage: OpenAI-style sends it at the top level of the last chunk; Anthropic in message_start and message_delta.
        auto takeUsage = [this](const QJsonObject &u) {
            const int in = u.value(QStringLiteral("prompt_tokens")).toInt(u.value(QStringLiteral("input_tokens")).toInt(0))
                           + u.value(QStringLiteral("cache_creation_input_tokens")).toInt(0) + u.value(QStringLiteral("cache_read_input_tokens")).toInt(0);
            const int out = u.value(QStringLiteral("completion_tokens")).toInt(u.value(QStringLiteral("output_tokens")).toInt(0));
            if (in > 0) promptTokens = in;
            if (out > 0) completionTokens = out;
        };
        takeUsage(obj.value(QStringLiteral("usage")).toObject());
        takeUsage(obj.value(QStringLiteral("message")).toObject().value(QStringLiteral("usage")).toObject());

        if (normalizeProtocol(protocol) == QLatin1String("anthropic")) {
            const QString type = obj.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("content_block_start")) {
                const QJsonObject cb = obj.value(QStringLiteral("content_block")).toObject();
                const QString cbType = cb.value(QStringLiteral("type")).toString();
                if (cbType == QLatin1String("tool_use")) {
                    if (toolCalls.size() >= 64) { errorText = QStringLiteral("Too many tool calls"); return; }
                    ToolCall tc;
                    tc.id = cb.value(QStringLiteral("id")).toString(newId());
                    tc.name = cb.value(QStringLiteral("name")).toString();
                    toolCalls.append(tc);
                    activeAnthropicBlockToolIdx = toolCalls.size() - 1;
                } else {
                    activeAnthropicBlockToolIdx = -1;
                }
            } else if (type == QLatin1String("content_block_delta")) {
                const QJsonObject delta = obj.value(QStringLiteral("delta")).toObject();
                const QString dType = delta.value(QStringLiteral("type")).toString();
                if (dType == QLatin1String("text_delta")) {
                    content += delta.value(QStringLiteral("text")).toString();
                } else if (dType == QLatin1String("thinking_delta")) {
                    reasoning += delta.value(QStringLiteral("thinking")).toString();
                } else if (dType == QLatin1String("input_json_delta") && activeAnthropicBlockToolIdx >= 0
                           && activeAnthropicBlockToolIdx < toolCalls.size()) {
                    toolCalls[activeAnthropicBlockToolIdx].argumentsJson += delta.value(QStringLiteral("partial_json")).toString();
                }
            } else if (type == QLatin1String("content_block_stop")) {
                if (activeAnthropicBlockToolIdx >= 0 && toolCalls[activeAnthropicBlockToolIdx].argumentsJson.isEmpty())
                    toolCalls[activeAnthropicBlockToolIdx].argumentsJson = QStringLiteral("{}");
                activeAnthropicBlockToolIdx = -1;
            } else if (type == QLatin1String("message_delta")) {
                const QJsonObject delta = obj.value(QStringLiteral("delta")).toObject();
                if (delta.contains(QStringLiteral("stop_reason")))
                    finishReason = delta.value(QStringLiteral("stop_reason")).toString();
            } else if (type == QLatin1String("message_stop")) {
                done = true;
            }
            return;
        }

        // OpenAI / Xiaomi MiMo / DeepSeek format
        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty())
            return;
        const QJsonObject choice = choices.first().toObject();
        if (choice.contains(QStringLiteral("finish_reason")) && !choice.value(QStringLiteral("finish_reason")).isNull())
            finishReason = choice.value(QStringLiteral("finish_reason")).toString();

        // Handle both streaming `delta` and non-streaming `message`
        const QJsonObject delta = choice.contains(QStringLiteral("delta"))
                                      ? choice.value(QStringLiteral("delta")).toObject()
                                      : choice.value(QStringLiteral("message")).toObject();
        if (delta.contains(QStringLiteral("reasoning_content")) && !delta.value(QStringLiteral("reasoning_content")).isNull())
            reasoning += delta.value(QStringLiteral("reasoning_content")).toString();
        if (delta.contains(QStringLiteral("content")) && !delta.value(QStringLiteral("content")).isNull())
            content += delta.value(QStringLiteral("content")).toString();

        const QJsonArray tcs = delta.value(QStringLiteral("tool_calls")).toArray();
        for (int i = 0; i < tcs.size(); ++i) {
            const QJsonObject tcObj = tcs.at(i).toObject();
            const int idx = tcObj.value(QStringLiteral("index")).toInt(i);
            if (idx < 0 || idx > 63) { errorText = QStringLiteral("Invalid tool index"); return; }
            while (toolCalls.size() <= idx)
                toolCalls.append(ToolCall{});
            ToolCall &target = toolCalls[idx];
            if (tcObj.contains(QStringLiteral("id")) && !tcObj.value(QStringLiteral("id")).toString().isEmpty())
                target.id = tcObj.value(QStringLiteral("id")).toString();
            const QJsonObject fn = tcObj.value(QStringLiteral("function")).toObject();
            if (fn.contains(QStringLiteral("name")) && !fn.value(QStringLiteral("name")).toString().isEmpty())
                target.name += fn.value(QStringLiteral("name")).toString();
            if (fn.contains(QStringLiteral("arguments")) && !fn.value(QStringLiteral("arguments")).isNull())
                target.argumentsJson += fn.value(QStringLiteral("arguments")).toString();
        }
    }
};

// JSON serialization for ProviderConfig
inline QJsonObject providerToJson(const ProviderConfig &p)
{
    QJsonArray presets;
    for (const QString &m : p.modelPresets)
        presets.append(m);
    return QJsonObject{
        {QStringLiteral("id"), p.id},
        {QStringLiteral("name"), p.name},
        {QStringLiteral("protocol"), normalizeProtocol(p.protocol)},
        {QStringLiteral("baseUrl"), p.baseUrl},
        {QStringLiteral("modelId"), p.modelId},
        {QStringLiteral("modelPresets"), presets},
        {QStringLiteral("temperature"), p.temperature},
        {QStringLiteral("maxTokens"), p.maxTokens},
        {QStringLiteral("maxTokensParameter"), p.maxTokensParameter},
        {QStringLiteral("authHeader"), p.authHeader},
        {QStringLiteral("customHeadersJson"), p.customHeadersJson},
        {QStringLiteral("extraBodyJson"), p.extraBodyJson},
        {QStringLiteral("docsUrl"), p.docsUrl},
        {QStringLiteral("builtIn"), p.builtIn},
        {QStringLiteral("contextWindow"), p.contextWindow}
    };
}

inline ProviderConfig providerFromJson(const QJsonObject &o)
{
    ProviderConfig p;
    p.id = o.value(QStringLiteral("id")).toString().trimmed();
    p.name = o.value(QStringLiteral("name")).toString().trimmed();
    p.protocol = normalizeProtocol(o.value(QStringLiteral("protocol")).toString(QStringLiteral("openai")));
    p.baseUrl = o.value(QStringLiteral("baseUrl")).toString().trimmed();
    p.modelId = o.value(QStringLiteral("modelId")).toString().trimmed();
    for (const QJsonValue &v : o.value(QStringLiteral("modelPresets")).toArray()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty() && !p.modelPresets.contains(s))
            p.modelPresets.append(s);
    }
    if (!p.modelId.isEmpty() && !p.modelPresets.contains(p.modelId))
        p.modelPresets.prepend(p.modelId);
    p.temperature = o.value(QStringLiteral("temperature")).toDouble(0.2);
    p.maxTokens = o.value(QStringLiteral("maxTokens")).toInt(4096);
    p.maxTokensParameter = o.value(QStringLiteral("maxTokensParameter")).toString(QStringLiteral("max_tokens"));
    p.authHeader = o.value(QStringLiteral("authHeader")).toString(QStringLiteral("Bearer"));
    p.customHeadersJson = o.value(QStringLiteral("customHeadersJson")).toString();
    p.extraBodyJson = o.value(QStringLiteral("extraBodyJson")).toString();
    p.docsUrl = o.value(QStringLiteral("docsUrl")).toString();
    p.builtIn = o.value(QStringLiteral("builtIn")).toBool(false);
    p.contextWindow = qMax(0, o.value(QStringLiteral("contextWindow")).toInt(0));
    return p;
}

inline QJsonObject toolCallToJson(const ToolCall &tc)
{
    return QJsonObject{
        {QStringLiteral("id"), tc.id},
        {QStringLiteral("name"), tc.name},
        {QStringLiteral("argumentsJson"), tc.argumentsJson},
        {QStringLiteral("status"), tc.status},
        {QStringLiteral("summary"), tc.summary},
        {QStringLiteral("diffText"), tc.diffText},
        {QStringLiteral("filePath"), tc.filePath},
        {QStringLiteral("proposedContent"), tc.proposedContent},
        {QStringLiteral("originalHash"), QString::fromLatin1(tc.originalHash)},
        {QStringLiteral("originalExists"), tc.originalExists},
        {QStringLiteral("command"), tc.command},
        {QStringLiteral("output"), tc.output},
        {QStringLiteral("addedLines"), tc.addedLines},
        {QStringLiteral("removedLines"), tc.removedLines}
    };
}

inline ToolCall toolCallFromJson(const QJsonObject &o)
{
    ToolCall tc;
    tc.id = o.value(QStringLiteral("id")).toString();
    tc.name = o.value(QStringLiteral("name")).toString();
    tc.argumentsJson = o.value(QStringLiteral("argumentsJson")).toString();
    tc.status = o.value(QStringLiteral("status")).toString(QStringLiteral("completed"));
    tc.summary = o.value(QStringLiteral("summary")).toString();
    tc.diffText = o.value(QStringLiteral("diffText")).toString();
    tc.filePath = o.value(QStringLiteral("filePath")).toString();
    tc.proposedContent = o.value(QStringLiteral("proposedContent")).toString();
    tc.originalHash = o.value(QStringLiteral("originalHash")).toString().toLatin1();
    tc.originalExists = o.value(QStringLiteral("originalExists")).toBool();
    tc.command = o.value(QStringLiteral("command")).toString();
    tc.output = o.value(QStringLiteral("output")).toString();
    tc.addedLines = o.value(QStringLiteral("addedLines")).toInt(0);
    tc.removedLines = o.value(QStringLiteral("removedLines")).toInt(0);
    return tc;
}

inline QJsonObject messageToJson(const ChatMessage &m)
{
    QJsonArray tcs, atts;
    for (const ToolCall &tc : m.toolCalls)
        tcs.append(toolCallToJson(tc));
    for (const Attachment &a : m.attachments)
        atts.append(QJsonObject{{QStringLiteral("path"), a.path}, {QStringLiteral("bytes"), a.bytes}});
    return QJsonObject{
        {QStringLiteral("id"), m.id},
        {QStringLiteral("role"), m.role},
        {QStringLiteral("content"), m.content},
        {QStringLiteral("shown"), m.shown},
        {QStringLiteral("attachments"), atts},
        {QStringLiteral("reasoning"), m.reasoning},
        {QStringLiteral("toolCallId"), m.toolCallId},
        {QStringLiteral("toolName"), m.toolName},
        {QStringLiteral("toolCalls"), tcs},
        {QStringLiteral("timestamp"), m.timestamp}
    };
}

inline ChatMessage messageFromJson(const QJsonObject &o)
{
    ChatMessage m;
    m.id = o.value(QStringLiteral("id")).toString(newId());
    m.role = o.value(QStringLiteral("role")).toString();
    m.content = o.value(QStringLiteral("content")).toString();
    m.shown = o.value(QStringLiteral("shown")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("attachments")).toArray())
        m.attachments.append({v.toObject().value(QStringLiteral("path")).toString(), v.toObject().value(QStringLiteral("bytes")).toInteger()});
    m.reasoning = o.value(QStringLiteral("reasoning")).toString();
    m.toolCallId = o.value(QStringLiteral("toolCallId")).toString();
    m.toolName = o.value(QStringLiteral("toolName")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("toolCalls")).toArray())
        m.toolCalls.append(toolCallFromJson(v.toObject()));
    m.timestamp = o.value(QStringLiteral("timestamp")).toInteger();
    return m;
}

inline QJsonObject sessionToJson(const Session &s)
{
    QJsonArray msgs, todos;
    for (const ChatMessage &m : s.messages)
        msgs.append(messageToJson(m));
    for (const TodoItem &t : s.todos)
        todos.append(QJsonObject{{QStringLiteral("content"), t.content}, {QStringLiteral("status"), t.status}});
    return QJsonObject{
        {QStringLiteral("id"), s.id},
        {QStringLiteral("title"), s.title},
        {QStringLiteral("workspace"), s.workspace},
        {QStringLiteral("mode"), normalizeMode(s.mode)},
        {QStringLiteral("providerId"), s.providerId},
        {QStringLiteral("modelId"), s.modelId},
        {QStringLiteral("messages"), msgs},
        {QStringLiteral("updatedAt"), s.updatedAt},
        {QStringLiteral("plan"), s.plan},
        {QStringLiteral("planStatus"), s.planStatus},
        {QStringLiteral("todos"), todos},
        {QStringLiteral("summary"), s.summary},
        {QStringLiteral("compactedUpTo"), s.compactedUpTo},
        {QStringLiteral("lastPromptTokens"), s.lastPromptTokens}
    };
}

inline Session sessionFromJson(const QJsonObject &o)
{
    Session s;
    s.id = o.value(QStringLiteral("id")).toString();
    if (!isSafeId(s.id)) s.id = newId();
    s.title = o.value(QStringLiteral("title")).toString(QStringLiteral("New task"));
    s.workspace = o.value(QStringLiteral("workspace")).toString();
    s.mode = normalizeMode(o.value(QStringLiteral("mode")).toString(QStringLiteral("manual")));
    s.providerId = o.value(QStringLiteral("providerId")).toString(QStringLiteral("mimo"));
    s.modelId = o.value(QStringLiteral("modelId")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("messages")).toArray())
        s.messages.append(messageFromJson(v.toObject()));
    s.updatedAt = o.value(QStringLiteral("updatedAt")).toInteger();
    for (const QJsonValue &v : o.value(QStringLiteral("todos")).toArray())
        s.todos.append({v.toObject().value(QStringLiteral("content")).toString(), v.toObject().value(QStringLiteral("status")).toString(QStringLiteral("pending"))});
    s.summary = o.value(QStringLiteral("summary")).toString();
    s.compactedUpTo = qBound(0, o.value(QStringLiteral("compactedUpTo")).toInt(), int(s.messages.size()));
    s.lastPromptTokens = qMax(0, o.value(QStringLiteral("lastPromptTokens")).toInt());
    s.plan = o.value(QStringLiteral("plan")).toString();
    s.planStatus = o.value(QStringLiteral("planStatus")).toString();
    if (s.planStatus != QLatin1String("pending") && s.planStatus != QLatin1String("accepted") && s.planStatus != QLatin1String("rejected")) s.planStatus.clear();
    if (s.plan.isEmpty()) s.planStatus.clear();
    return s;
}

// A provider the agent may talk to: a plain id, https (http only for this machine), sane limits, well-formed extra JSON and headers.
inline bool validConfig(const ProviderConfig &p)
{
    if (!isSafeId(p.id)) return false;
    const QUrl u(p.baseUrl);
    if (!u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty() || u.hasQuery() || u.hasFragment()
        || (u.scheme() != "https" && !(u.scheme() == "http" && (u.host() == "localhost" || u.host() == "127.0.0.1" || u.host() == "::1")))
        || p.name.isEmpty() || p.modelId.isEmpty() || p.maxTokens < 256 || p.maxTokens > 1000000
        || p.temperature < -1 || p.temperature > 2) return false;
    for (const QString &s : {p.extraBodyJson, p.customHeadersJson}) {
        if (!s.trimmed().isEmpty() && !QJsonDocument::fromJson(s.toUtf8()).isObject()) return false;
    }
    const QJsonObject headers = QJsonDocument::fromJson(p.customHeadersJson.toUtf8()).object();
    static const QRegularExpression headerName(QStringLiteral("^[A-Za-z0-9-]+$"));
    for (auto it = headers.begin(); it != headers.end(); ++it)
        if (!headerName.match(it.key()).hasMatch() || !it.value().isString() || it.value().toString().contains('\r') || it.value().toString().contains('\n')) return false;
    if (p.maxTokensParameter != "max_tokens" && p.maxTokensParameter != "max_completion_tokens") return false;
    return p.authHeader == "Bearer" || p.authHeader == "api-key" || p.authHeader == "x-api-key" || p.authHeader == "none";
}

} // namespace CodingAgentModel
