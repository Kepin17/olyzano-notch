#pragma once

#include "platform/CodingAgentModel.h"

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// The Coding Assistant (`codingAgent` in QML) inside Dev → Agent:
// - Supports Xiaomi MiMo, DeepSeek, Claude (Anthropic), OpenAI, and custom endpoints
// - Stores API keys in SecretsStore ("ai" section, owner-only 0600 permissions)
// - Three execution modes: Plan Mode ("plan"), Manual Approval ("manual"), Auto Approval ("auto")
class CodingAgent : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    Q_PROPERTY(QString activeProviderId READ activeProviderId WRITE setActiveProviderId NOTIFY providersChanged)
    Q_PROPERTY(QVariantMap activeProvider READ activeProvider NOTIFY providersChanged)
    Q_PROPERTY(QString activeModelId READ activeModelId WRITE setActiveModelId NOTIFY providersChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged) // [{id, group}] for the active provider
    Q_PROPERTY(bool modelsLoading READ modelsLoading NOTIFY modelsChanged)
    Q_PROPERTY(int messageCount READ messageCount NOTIFY messagesChanged) // cheap: no need to build the whole list to ask "is it empty?"
    Q_PROPERTY(QVariantList projects READ projects NOTIFY sessionsChanged) // folders with their tasks: [{path, name, custom, collapsed, exists, active, sessions:[{id, title, updatedAt, active}]}]
    Q_PROPERTY(QVariantMap lastRun READ lastRun NOTIFY messagesChanged) // what the last run changed: {files:[{path, verb, added, removed}], commands, failedCommands, declined, added, removed, any}
    Q_PROPERTY(QVariantList todos READ todos NOTIFY todosChanged)       // the agent's own checklist [{content, status}]
    Q_PROPERTY(int contextUsed READ contextUsed NOTIFY contextChanged)  // tokens the conversation holds (reported by the provider, else estimated)
    Q_PROPERTY(int contextWindow READ contextWindow NOTIFY contextChanged)
    Q_PROPERTY(bool contextExact READ contextExact NOTIFY contextChanged) // contextUsed came from the provider, not a guess
    Q_PROPERTY(bool compacting READ compacting NOTIFY contextChanged)
    Q_PROPERTY(QString plan READ plan NOTIFY planChanged)             // the active task's latest plan (Markdown)
    Q_PROPERTY(QString planStatus READ planStatus NOTIFY planChanged) // "" | pending | accepted | rejected
    Q_PROPERTY(QString planPath READ planPath NOTIFY planChanged)     // where plan.md is saved
    Q_PROPERTY(QString retryStatus READ retryStatus NOTIFY retryChanged) // "Provider problem (503). Retrying 2/10 in 4 s…" while waiting to try again
    Q_PROPERTY(QString detail READ detail WRITE setDetail NOTIFY detailChanged) // "headings" (one line per action) | "full"
    Q_PROPERTY(QString effort READ effort WRITE setEffort NOTIFY effortChanged) // "" | low | medium | high | xhigh | max
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QString workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged)
    Q_PROPERTY(QVariantList workspaceFiles READ workspaceFiles NOTIFY workspaceChanged)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QString activeSessionId READ activeSessionId WRITE selectSession NOTIFY sessionsChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(bool commandRunning READ commandRunning NOTIFY busyChanged)
    Q_PROPERTY(bool hasPendingApprovals READ hasPendingApprovals NOTIFY messagesChanged)
    Q_PROPERTY(QString streamingContent READ streamingContent NOTIFY streamChunkChanged)
    Q_PROPERTY(QString streamingReasoning READ streamingReasoning NOTIFY streamChunkChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)

public:
    explicit CodingAgent(QObject *parent = nullptr);
    ~CodingAgent() override;

    QVariantList providers() const;
    QString activeProviderId() const { return m_activeProviderId; }
    void setActiveProviderId(const QString &id);
    QVariantMap activeProvider() const;
    QString activeModelId() const;
    void setActiveModelId(const QString &modelId);

    QVariantList models() const;
    bool modelsLoading() const { return m_modelsReply != nullptr; }
    int messageCount() const;
    QVariantList projects() const;
    QVariantMap lastRun() const;
    QVariantList todos() const;
    int contextUsed() const;
    int contextWindow() const;
    bool contextExact() const;
    bool compacting() const { return m_compactReply != nullptr; }
    QString plan() const;
    QString planStatus() const;
    QString planPath() const;
    QString retryStatus() const { return m_retryStatus; }
    void setRetryBaseDelayForTest(int ms) { m_retryBaseMs = ms; }
    QString detail() const { return m_detail; }
    void setDetail(const QString &detail);
    QString effort() const { return m_effort; }
    void setEffort(const QString &effort);
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);

    QString workspace() const { return m_workspace; }
    void setWorkspace(const QString &path);
    QVariantList workspaceFiles() const;

    QVariantList sessions() const;
    QString activeSessionId() const { return m_activeSessionId; }
    QVariantList messages() const;

    bool busy() const { return m_resumeQueued || m_reply != nullptr || m_proc != nullptr || m_compactReply != nullptr || m_retryTimer.isActive(); }
    bool streaming() const { return m_reply != nullptr; }
    bool commandRunning() const { return m_proc != nullptr; }
    bool hasPendingApprovals() const;
    QString streamingContent() const { return m_streamContent; }
    QString streamingReasoning() const { return m_streamReasoning; }
    QString lastError() const { return m_lastError; }

    // Provider CRUD & API Key management (keys live only in SecretsStore("ai", id))
    Q_INVOKABLE QVariantMap providerById(const QString &id) const;
    Q_INVOKABLE QVariantList presets() const; // the known providers (MiMo, DeepSeek, Claude, OpenAI) with their endpoint and auth already filled in
    Q_INVOKABLE QString addProvider(const QVariantMap &fields);
    Q_INVOKABLE bool updateProvider(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool removeProvider(const QString &id);
    Q_INVOKABLE void refreshModels(); // asks the active provider which models this key can use (only when called: nothing is sent at startup)

    // Session CRUD
    Q_INVOKABLE QString newSession(const QString &title = {});
    Q_INVOKABLE void selectSession(const QString &id);
    Q_INVOKABLE void deleteSession(const QString &id);
    Q_INVOKABLE void clearSession(const QString &id = {});
    Q_INVOKABLE void renameSession(const QString &id, const QString &title);

    // Prompt & Agent Loop
    Q_INVOKABLE void sendPrompt(const QString &prompt, const QVariantList &attachedFiles = {});
    Q_INVOKABLE void stop();
    void flush();                          // write what changed now (also at quit and when the agent is destroyed)
    Q_INVOKABLE void releaseIdle();                   // let go of what the idle app does not need (other tasks' messages, caches)
    static void setSaveDelay(int ms);      // tests: 0 writes at every save()

    // Folders (projects): tasks are grouped by workspace folder, and a new task in a folder needs no picking again
    Q_INVOKABLE bool openFolder(const QString &path);                               // make it the workspace and start (or reuse) an empty task in it
    Q_INVOKABLE void renameFolder(const QString &path, const QString &name);       // a display name; empty = the folder's own name
    Q_INVOKABLE bool removeFolder(const QString &path);                             // forget an empty folder (never deletes files)
    Q_INVOKABLE void setFolderCollapsed(const QString &path, bool collapsed);
    // For the in-notch folder picker (a native dialog opens behind the notch)
    Q_INVOKABLE QVariantList listFolders(const QString &path) const;                // [{name, path}] visible sub-folders
    Q_INVOKABLE bool folderExists(const QString &path) const;
    Q_INVOKABLE QString parentFolder(const QString &path) const;
    Q_INVOKABLE QString homeFolder() const;
    Q_INVOKABLE QString makeFolder(const QString &parent, const QString &name);     // the new folder's path, or empty
    Q_INVOKABLE QString exportSessionTo(const QString &folder);                     // writes <title>.md there; returns the file

    // The prompt box: @files, !commands, /commands
    Q_INVOKABLE QVariantList findFiles(const QString &query, int limit = 8);        // [{path, name, dir}] in the workspace, best first
    Q_INVOKABLE void runShell(const QString &command);                               // `!command`: runs now, output streams into the chat
    Q_INVOKABLE QVariantMap parseSlash(const QString &text) const;                   // {ok, name, args}
    Q_INVOKABLE QVariantList slashCompletions(const QString &prefix) const;          // [{name, args, description}]
    Q_INVOKABLE QString forkSession();                                               // copy of this task, now active
    Q_INVOKABLE bool exportSession(const QString &path) const;                       // Markdown file
    Q_INVOKABLE QString lastAnswer() const;
    Q_INVOKABLE bool openLink(const QString &url) const; // opens a link from an answer, but only web and mail ones
    Q_INVOKABLE QString fullText(const QString &messageId, const QString &toolCallId = {}) const; // what the chat only shows the end of (long command output)
    Q_INVOKABLE QString renderMarkdown(const QString &markdown, const QString &family, int px, const QString &code, const QString &codeBg, const QString &link, const QString &border) const; // HTML for the plan reader
    Q_INVOKABLE void addNote(const QString &text);                                   // a divider line in the chat (never sent)
    Q_INVOKABLE void compactNow(const QString &focus = {});                          // summarize older messages

    // Plan Mode: the person decides about the plan the agent wrote
    Q_INVOKABLE void acceptPlan(const QString &mode); // "manual" or "auto" (Auto needs the consent dialog first, in QML)
    Q_INVOKABLE void rejectPlan();
    Q_INVOKABLE void revisePlan(const QString &feedback); // "Revise" + what to change: sent as an explicit revision request
    Q_INVOKABLE void reopenPlan();                        // a rejected plan is pending again (the person changed their mind)

    // Manual Approval actions for proposed file edits & shell commands
    Q_INVOKABLE void approveToolCall(const QString &messageId, const QString &toolCallId);
    Q_INVOKABLE void rejectToolCall(const QString &messageId, const QString &toolCallId);
    Q_INVOKABLE void approveAllPending();

signals:
    void providersChanged();
    void modelsChanged();
    void todosChanged();
    void contextChanged();
    void shellFinished(const QString &command, int exitCode);
    // Something the person may want to know while the panel is closed: kind is done | approval | plan | error; icon is a line-icon name.
    void agentEvent(const QString &kind, const QString &title, const QString &detail, const QString &icon);
    void effortChanged();
    void detailChanged();
    void retryChanged();
    void planChanged();
    void modeChanged();
    void workspaceChanged();
    void sessionsChanged();
    void messagesChanged();
    void busyChanged();
    void streamingChanged();
    void streamChunkChanged();
    void errorChanged();
    void statusMessage(const QString &text);

private:
    void load();
    static void settleLoaded(CodingAgentModel::Session &s);
    void restore(CodingAgentModel::Session &s) const;
    void clearError();
    void save(const QString &id = {});

    CodingAgentModel::ProviderConfig *findProvider(const QString &id);
    const CodingAgentModel::ProviderConfig *findProvider(const QString &id) const;
    CodingAgentModel::Session *findSession(const QString &id);
    const CodingAgentModel::Session *findSession(const QString &id) const;
    CodingAgentModel::Session *ensureActiveSession();
    QVariantMap providerToMap(const CodingAgentModel::ProviderConfig &p) const;

    QString apiKeyFor(const QString &providerId) const;
    void setApiKeyFor(const QString &providerId, const QString &apiKey);
    void applyAuthHeaders(QNetworkRequest &req, const CodingAgentModel::ProviderConfig &p, const QString &apiKey) const;

    void startPrompt(const QString &prompt, bool mayStartPlanMode, const QStringList &files = {});
    void startCompaction(const QString &focus, bool thenContinue);
    bool shouldCompact(const CodingAgentModel::Session &s, const CodingAgentModel::ProviderConfig &p, qint64 payloadBytes) const;
    void rebuildFileIndex();
    void clearRetry();
    void removePlanDir(const QString &sessionId) const;
    void scheduleSoon(const QString &status, int delayMs = 50); // the next try goes out in a moment, with this line shown meanwhile
    int boostedMaxTokens(const CodingAgentModel::ProviderConfig &p, int level) const;
    void rejectPendingApprovals(const QString &reason);
    int cachedEstimate(const CodingAgentModel::Session &s) const;
    void messagesSoon(); // a burst of output (a running command) tells the screen at most about eight times a second
    QString planFileFor(const QString &sessionId) const;
    void storePlan(const CodingAgentModel::Session &s) const;
    void startAgentTurn();
    void onStreamReadyRead();
    void onStreamFinished();
    void processToolCalls(const QString &assistantMsgId);
    void resumeAfterToolCompletion();

    // Workspace tools
    QString applyProposedEdit(CodingAgentModel::ToolCall &tc);
    void startShellCommand(const QString &messageId, const QString &toolCallId, const QString &command, bool userInitiated = false);
    void appendToolResultMessage(CodingAgentModel::Session &s, const QString &toolCallId, const QString &toolName, const QString &resultText);

    QList<CodingAgentModel::ProviderConfig> m_providers;
    mutable QList<CodingAgentModel::Session> m_sessions; // mutable: a task that was let go is read back by a const lookup
    QString m_activeProviderId = QStringLiteral("mimo");
    QString m_activeSessionId;
    QStringList m_folders;                   // folders the person added, so one with no task yet stays listed
    QHash<QString, QString> m_folderNames;   // display names
    QSet<QString> m_collapsed;               // folders folded in the sidebar
    QString m_mode = QStringLiteral("manual");
    QString m_effort;
    QString m_detail = QStringLiteral("headings");
    QString m_workspace;
    QString m_systemPrompt;

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QNetworkReply *m_modelsReply = nullptr;
    QNetworkReply *m_compactReply = nullptr;
    mutable QHash<QString, QString> m_keys; // API keys, read once from the secrets file
    mutable bool m_keysLoaded = false;
    QTimer m_saveTimer;
    QSet<QString> m_dirty;                        // tasks changed since the last write
    static int s_saveDelay;
    QTimer m_messagesTimer;
    QTimer m_chunkTimer;                          // the streaming text is redrawn about sixteen times a second, not at every network read
    mutable QVariantList m_messagesCache;         // the chat as QML wants it, rebuilt only when something in it changed
    mutable QString m_messagesCacheSession;
    mutable quint64 m_messagesCacheStamp = 0;
    mutable int m_estimateCache = -1;
    mutable quint64 m_estimateStamp = 0;
    mutable QString m_estimateSession;
    QTimer m_retryTimer;       // waiting to ask the provider again
    int m_retryCount = 0;
    int m_tokenBoost = 0;        // the answer was cut by the token limit: 0 = as configured, 1 = twice that, 2 = four times
    bool m_boostBlocked = false; // the provider refused a bigger limit, so none is tried again
    int m_continueCount = 0;     // "Continue" requests made for one prompt
    QByteArray m_errorBody;      // what a failed request said, for a readable message
    int m_retryBaseMs = 1000;
    QString m_retryStatus;
    bool m_autoCompacted = false; // one automatic compact per prompt, so a tiny window cannot loop
    QStringList m_fileIndex;      // workspace files for the @ list
    QString m_fileIndexWorkspace;
    QElapsedTimer m_fileIndexAge;
    QHash<QString, QStringList> m_remoteModels; // provider id -> ids from its /models
    QByteArray m_sseBuffer;
    qint64 m_responseBytes = 0;
    quint64 m_epoch = 0;
    CodingAgentModel::StreamAccumulator m_streamAcc;
    QString m_streamContent;
    QString m_streamReasoning;
    QString m_lastError;

    QProcess *m_proc = nullptr;
    QString m_procMsgId;
    QString m_procToolCallId;
    bool m_resumeQueued = false;
    int m_turnDepth = 0;
};
