#include "CodingAgent.h"
#include "platform/AgentCommands.h"
#include "platform/MarkdownHtml.h"
#include "platform/NotchPaths.h"
#include "platform/SecretsStore.h"
#include "platform/SecureFiles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

using namespace CodingAgentModel;
namespace {
constexpr qint64 FileLimit = 256 * 1024;
void killCommand(QProcess *process)
{
#ifdef Q_OS_UNIX
    if (process->processId() > 0) ::kill(-pid_t(process->processId()), SIGKILL);
#elif defined(Q_OS_WIN)
    // taskkill /T also terminates the shell's descendants.
    QProcess killer;
    killer.start("taskkill.exe", {"/PID", QString::number(process->processId()), "/T", "/F"});
    killer.waitForFinished(3000);
#endif
    process->kill();
}
QString readText(const QString &path, bool *ok = nullptr)
{
    QFile f(path);
    const bool good = QFileInfo(path).isFile() && f.open(QIODevice::ReadOnly) && f.size() <= FileLimit;
    const QByteArray bytes = good ? f.read(FileLimit + 1) : QByteArray();
    if (ok) *ok = good && bytes.size() <= FileLimit && !bytes.contains('\0');
    return good ? QString::fromUtf8(bytes) : QString();
}
bool validConfig(const ProviderConfig &p)
{
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
bool privatePath(const QString &relative)
{
    const QStringList parts = relative.split('/');
    for (const QString &p : parts)
        if (p == ".git" || p == "node_modules" || p == ".ssh" || p == ".env" || p.startsWith(".env.") || p == "secrets.json") return true;
    return false;
}
ToolCall *toolIn(Session *s, const QString &msg, const QString &id)
{
    if (s) for (ChatMessage &m : s->messages) if (m.id == msg)
        for (ToolCall &tc : m.toolCalls) if (tc.id == id) return &tc;
    return nullptr;
}
}

CodingAgent::CodingAgent(QObject *parent) : QObject(parent)
{
    load();
    m_chunkTimer.setSingleShot(true);
    m_chunkTimer.setInterval(60);
    connect(&m_chunkTimer, &QTimer::timeout, this, [this] { emit streamChunkChanged(); });
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] { m_retryStatus.clear(); emit retryChanged(); startAgentTurn(); });
    m_messagesTimer.setSingleShot(true);
    m_messagesTimer.setInterval(120);
    connect(&m_messagesTimer, &QTimer::timeout, this, [this] { emit messagesChanged(); });
    connect(this, &CodingAgent::messagesChanged, this, &CodingAgent::planChanged); // another task: another plan
    connect(this, &CodingAgent::messagesChanged, this, &CodingAgent::todosChanged);
    connect(this, &CodingAgent::messagesChanged, this, &CodingAgent::contextChanged);
    connect(this, &CodingAgent::providersChanged, this, &CodingAgent::contextChanged);
    connect(this, &CodingAgent::providersChanged, this, &CodingAgent::modelsChanged); // another provider or model: another list
}
CodingAgent::~CodingAgent() { stop(); }
QString CodingAgent::configPath() const { return NotchPaths::devDir() + "/agent.json"; }
void CodingAgent::load()
{
    const QJsonObject root = SecureFiles::readJsonObject(configPath());
    for (const auto &v : root.value("providers").toArray()) {
        auto p = providerFromJson(v.toObject());
        if (validConfig(p) && !findProvider(p.id)) m_providers.append(p);
    }
    if (m_providers.isEmpty()) m_providers = defaultProviders();
    for (const auto &v : root.value("sessions").toArray()) {
        auto s = sessionFromJson(v.toObject());
        s.mode = "manual"; // Elevated consent never survives an application restart.
        // Interrupted tool turns are discarded rather than replaying approved operations.
        int cut = -1;
        for (int i = 0; i < s.messages.size(); ++i)
            for (const auto &tc : s.messages[i].toolCalls)
                if (tc.status == "pending" || tc.status == "running" || tc.status == "awaiting_approval") { cut = i; break; }
        if (cut >= 0) s.messages = s.messages.mid(0, cut);
        s.compactedUpTo = qMin(s.compactedUpTo, int(s.messages.size()));
        m_sessions.append(s);
    }
    m_activeProviderId = root.value("providerId").toString(m_providers.first().id);
    if (!findProvider(m_activeProviderId)) m_activeProviderId = m_providers.first().id;
    m_workspace = root.value("workspace").toString();
    m_effort = normalizeEffort(root.value("effort").toString());
    m_detail = root.value("detail").toString() == "full" ? "full" : "headings";
    for (const auto &v : root.value("folders").toArray()) {
        const QString path = v.toObject().value("path").toString();
        if (path.isEmpty() || m_folders.contains(path)) continue;
        m_folders.append(path);
        if (const QString name = v.toObject().value("name").toString(); !name.isEmpty()) m_folderNames.insert(path, name);
    }
    for (const auto &v : root.value("collapsed").toArray()) m_collapsed.insert(v.toString());
    m_activeSessionId = root.value("sessionId").toString();
    if (auto *s = findSession(m_activeSessionId)) {
        m_workspace = s->workspace;
        m_mode = s->mode; // Auto was already lowered to Manual above
        if (findProvider(s->providerId)) m_activeProviderId = s->providerId;
    }
}
void CodingAgent::save() const
{
    QJsonArray ps, ss, fs, cs;
    for (const QString &path : m_folders) fs.append(QJsonObject{{"path", path}, {"name", m_folderNames.value(path)}});
    for (const QString &path : m_collapsed) cs.append(path);
    for (const auto &p : m_providers) ps.append(providerToJson(p));
    for (const auto &s : m_sessions) ss.append(sessionToJson(s));
    SecureFiles::ownerOnlyPath(NotchPaths::devDir(), "agent.json");
    SecureFiles::writePrivateJson(configPath(), QJsonDocument(QJsonObject{{"version", 1}, {"providers", ps}, {"sessions", ss},
        {"providerId", m_activeProviderId}, {"sessionId", m_activeSessionId}, {"workspace", m_workspace}, {"effort", m_effort}, {"detail", m_detail},
        {"folders", fs}, {"collapsed", cs}}));
}
ProviderConfig *CodingAgent::findProvider(const QString &id) { for (auto &p : m_providers) if (p.id == id) return &p; return nullptr; }
const ProviderConfig *CodingAgent::findProvider(const QString &id) const { for (const auto &p : m_providers) if (p.id == id) return &p; return nullptr; }
Session *CodingAgent::findSession(const QString &id) { for (auto &s : m_sessions) if (s.id == id) return &s; return nullptr; }
const Session *CodingAgent::findSession(const QString &id) const { for (const auto &s : m_sessions) if (s.id == id) return &s; return nullptr; }
Session *CodingAgent::ensureActiveSession() { if (!findSession(m_activeSessionId)) newSession(); return findSession(m_activeSessionId); }
QString CodingAgent::apiKeyFor(const QString &id) const { return SecretsStore::get("ai", id); }
void CodingAgent::setApiKeyFor(const QString &id, const QString &key) { SecretsStore::set("ai", id, key); }
QVariantMap CodingAgent::providerToMap(const ProviderConfig &p) const
{
    auto map = providerToJson(p).toVariantMap(); map.insert("hasApiKey", !apiKeyFor(p.id).isEmpty()); return map;
}
QVariantList CodingAgent::providers() const { QVariantList out; for (const auto &p : m_providers) out.append(providerToMap(p)); return out; }
QVariantMap CodingAgent::providerById(const QString &id) const { const auto *p = findProvider(id); return p ? providerToMap(*p) : QVariantMap(); }
QVariantList CodingAgent::presets() const { QVariantList out; for (const auto &p : defaultProviders()) out.append(providerToJson(p).toVariantMap()); return out; }
void CodingAgent::clearError() { if (m_lastError.isEmpty()) return; m_lastError.clear(); emit errorChanged(); }
QVariantMap CodingAgent::activeProvider() const { return providerById(m_activeProviderId); }
QString CodingAgent::activeModelId() const { auto *p = findProvider(m_activeProviderId); return p ? p->modelId : QString(); }
void CodingAgent::setActiveProviderId(const QString &id)
{
    if (busy() || hasPendingApprovals() || !findProvider(id)) return;
    m_activeProviderId = id; m_tokenBoost = 0; m_boostBlocked = false;
    if (auto *s = findSession(m_activeSessionId)) { s->providerId = id; s->modelId = activeModelId(); }
    clearError(); save(); emit providersChanged();
}
void CodingAgent::setActiveModelId(const QString &id)
{
    auto *p = findProvider(m_activeProviderId);
    const QString model = id.trimmed();
    if (!p || model.isEmpty() || model == p->modelId) return;
    const ProviderConfig before = *p;
    p->modelId = model;
    if (!p->modelPresets.contains(model)) p->modelPresets.prepend(model);
    if (!validConfig(*p)) { *p = before; emit statusMessage("That model id is not valid."); return; }
    m_tokenBoost = 0; m_boostBlocked = false;
    if (auto *s = findSession(m_activeSessionId)) s->modelId = model;
    save(); emit providersChanged(); // the request in flight keeps its model; the next step uses this one
}
QString CodingAgent::addProvider(const QVariantMap &fields)
{
    if (busy() || hasPendingApprovals()) return {};
    auto obj = QJsonObject::fromVariantMap(fields); obj.insert("id", newId()); obj.insert("builtIn", false);
    auto p = providerFromJson(obj);
    if (!validConfig(p)) { emit statusMessage("Invalid provider URL, model, parameters or JSON settings (HTTPS required except localhost)."); return {}; }
    m_providers.append(p);
    if (fields.contains("apiKey")) setApiKeyFor(p.id, fields.value("apiKey").toString().trimmed()); // a pasted key often carries a newline or space
    clearError(); save(); emit providersChanged(); return p.id;
}
bool CodingAgent::updateProvider(const QString &id, const QVariantMap &fields)
{
    auto *p = findProvider(id);
    if (!p || busy() || hasPendingApprovals()) return false;
    QJsonObject obj = providerToJson(*p);
    for (auto it = fields.begin(); it != fields.end(); ++it)
        if (obj.contains(it.key()) && it.key() != "id" && it.key() != "builtIn") obj.insert(it.key(), QJsonValue::fromVariant(it.value()));
    auto candidate = providerFromJson(obj);
    if (!validConfig(candidate)) { emit statusMessage("Invalid provider settings. Check HTTPS URL, model and JSON parameters."); return false; }
    *p = candidate; m_tokenBoost = 0; m_boostBlocked = false;
    if (fields.contains("apiKey")) setApiKeyFor(id, fields.value("apiKey").toString().trimmed());
    clearError(); save(); emit providersChanged(); emit modelsChanged();
    if (fields.contains("apiKey")) { m_remoteModels.remove(id); emit modelsChanged(); if (id == m_activeProviderId) refreshModels(); }
    return true;
}
bool CodingAgent::removeProvider(const QString &id)
{
    if (busy() || hasPendingApprovals() || m_providers.size() <= 1) return false;
    for (int i = 0; i < m_providers.size(); ++i) if (m_providers[i].id == id) {
        m_providers.removeAt(i); setApiKeyFor(id, {});
        if (m_activeProviderId == id) m_activeProviderId = m_providers.first().id;
        save(); emit providersChanged(); return true;
    }
    return false;
}
void CodingAgent::resetProviderDefaults(const QString &id)
{
    if (busy() || hasPendingApprovals()) return;
    for (const auto &p : defaultProviders()) if (p.id == id) if (auto *target = findProvider(id)) { *target = p; save(); emit providersChanged(); }
}
void CodingAgent::applyAuthHeaders(QNetworkRequest &req, const ProviderConfig &p, const QString &key) const
{
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(120000);
    if (!key.isEmpty() && p.authHeader != "none") {
        if (p.authHeader == "Bearer") req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
        else req.setRawHeader(p.authHeader.toUtf8(), key.toUtf8());
    }
    if (p.protocol == "anthropic") req.setRawHeader("anthropic-version", "2023-06-01");
    const auto headers = QJsonDocument::fromJson(p.customHeadersJson.toUtf8()).object();
    for (auto it = headers.begin(); it != headers.end(); ++it) req.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
}
QVariantList CodingAgent::models() const
{
    const auto *p = findProvider(m_activeProviderId);
    if (!p) return {};
    QStringList ids = m_remoteModels.value(p->id, p->modelPresets);
    if (!p->modelId.isEmpty() && !ids.contains(p->modelId)) ids.prepend(p->modelId); // a model typed by hand stays in the list
    const QStringList order = modelGroups();
    QList<QPair<int, QVariantMap>> rows;
    for (const QString &id : ids) {
        const QString group = modelGroup(id);
        rows.append({int(order.indexOf(group)), QVariantMap{{"id", id}, {"group", group}}});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QVariantList out;
    for (const auto &r : rows) out.append(r.second);
    return out;
}
void CodingAgent::refreshModels()
{
    const auto *p = findProvider(m_activeProviderId);
    if (!p || m_modelsReply) return;
    const QString key = apiKeyFor(p->id);
    if (key.isEmpty() && p->authHeader != "none") return;
    QUrl url = modelsEndpointUrl(*p);
    if (p->protocol == "anthropic") url.setQuery(QStringLiteral("limit=1000"));
    QNetworkRequest req(url); applyAuthHeaders(req, *p, key); req.setTransferTimeout(15000);
    const QString id = p->id;
    m_modelsReply = m_nam.get(req); emit modelsChanged();
    connect(m_modelsReply, &QNetworkReply::finished, this, [this, id] {
        auto *r = m_modelsReply; m_modelsReply = nullptr;
        const bool ok = r->error() == QNetworkReply::NoError && r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        const QStringList ids = ok ? parseModelIds(r->read(1024 * 1024)) : QStringList();
        if (ok && !ids.isEmpty() && findProvider(id)) m_remoteModels.insert(id, ids);
        else if (!ok) emit statusMessage("Could not load the model list; showing the built-in choices.");
        r->deleteLater(); emit modelsChanged();
    });
}
void CodingAgent::testProvider(const QString &id, const QVariantMap &overrides)
{
    if (busy()) return;
    auto *saved = findProvider(id); if (!saved) return;
    auto obj = providerToJson(*saved);
    for (auto it = overrides.begin(); it != overrides.end(); ++it) if (obj.contains(it.key())) obj.insert(it.key(), QJsonValue::fromVariant(it.value()));
    auto p = providerFromJson(obj); if (!validConfig(p)) return;
    QNetworkRequest req(modelsEndpointUrl(p));
    applyAuthHeaders(req, p, overrides.contains("apiKey") ? overrides.value("apiKey").toString() : apiKeyFor(id));
    auto clock = QElapsedTimer(); clock.start();
    m_testReply = m_nam.get(req); emit busyChanged();
    connect(m_testReply, &QNetworkReply::finished, this, [this, id, clock] {
        auto *r = m_testReply; m_testReply = nullptr;
        const auto bytes = r->read(1024 * 1024);
        QStringList models;
        for (const auto &v : QJsonDocument::fromJson(bytes).object().value("data").toArray()) models.append(v.toObject().value("id").toString());
        const bool ok = r->error() == QNetworkReply::NoError && r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        emit testFinished({{"id", id}, {"ok", ok}, {"message", ok ? "Connected (models endpoint)" : r->errorString()}, {"models", models}, {"latencyMs", clock.elapsed()}});
        r->deleteLater(); emit busyChanged();
    });
}
QString CodingAgent::plan() const { const auto *s = findSession(m_activeSessionId); return s ? s->plan : QString(); }
QString CodingAgent::planStatus() const { const auto *s = findSession(m_activeSessionId); return s ? s->planStatus : QString(); }
QString CodingAgent::planPath() const { return m_activeSessionId.isEmpty() || plan().isEmpty() ? QString() : planFileFor(m_activeSessionId); }
QString CodingAgent::planFileFor(const QString &sessionId) const { return NotchPaths::devDir() + "/plans/" + sessionId + "/plan.md"; }
void CodingAgent::storePlan(const Session &s) const
{
    const QString path = planFileFor(s.id);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) { f.write(s.plan.toUtf8()); f.commit(); }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
void CodingAgent::acceptPlan(const QString &mode)
{
    auto *s = findSession(m_activeSessionId);
    if (!s || s->planStatus != "pending" || busy() || hasPendingApprovals()) return;
    const QString plan = s->plan;
    s->planStatus = "accepted";
    setMode(mode == "auto" ? "auto" : "manual"); // Plan Mode ends here; Auto only comes from the consent dialog in QML
    emit planChanged();
    startPrompt(planApprovalPrompt(plan), false);
}
void CodingAgent::rejectPlan()
{
    auto *s = findSession(m_activeSessionId);
    if (!s || s->planStatus != "pending" || busy() || hasPendingApprovals()) return;
    s->planStatus = "rejected";
    ChatMessage m; m.id = newId(); m.role = "user"; m.timestamp = QDateTime::currentMSecsSinceEpoch();
    m.content = "I reject this plan. Do not implement it. Wait for my next instruction.";
    s->messages.append(m);
    save(); emit planChanged(); emit messagesChanged();
}
void CodingAgent::revisePlan(const QString &feedback)
{
    auto *s = findSession(m_activeSessionId);
    if (!s || s->planStatus != "pending" || feedback.trimmed().isEmpty() || busy() || hasPendingApprovals()) return;
    if (m_mode != "plan") setMode("plan");
    startPrompt("Please revise the plan: " + feedback.trimmed(), false);
}
void CodingAgent::reopenPlan()
{
    auto *s = findSession(m_activeSessionId);
    if (!s || s->planStatus != "rejected" || busy() || hasPendingApprovals()) return;
    s->planStatus = "pending";
    if (m_mode != "plan") setMode("plan");
    save(); emit planChanged();
}
QVariantList CodingAgent::projects() const
{
    struct Group { QString path; QVariantList sessions; qint64 newest = 0; };
    QList<Group> groups;
    auto groupFor = [&groups](const QString &path) -> Group & {
        for (Group &g : groups) if (g.path == path) return g;
        groups.append({path, {}, 0}); return groups.last();
    };
    for (const QString &path : m_folders) groupFor(path);
    for (const Session &s : m_sessions) {
        Group &g = groupFor(s.workspace);
        g.sessions.append(QVariantMap{{"id", s.id}, {"title", s.title}, {"updatedAt", s.updatedAt}, {"active", s.id == m_activeSessionId}});
        g.newest = qMax(g.newest, s.updatedAt);
    }
    std::stable_sort(groups.begin(), groups.end(), [this](const Group &a, const Group &b) { // the newest first; the folder you are in wins a tie
        return a.newest != b.newest ? a.newest > b.newest : (a.path == m_workspace) && (b.path != m_workspace);
    });
    QVariantList out;
    for (Group &g : groups) {
        std::stable_sort(g.sessions.begin(), g.sessions.end(), [](const QVariant &a, const QVariant &b) { return a.toMap().value("updatedAt").toLongLong() > b.toMap().value("updatedAt").toLongLong(); });
        const QString custom = m_folderNames.value(g.path);
        const QString base = g.path.isEmpty() ? QStringLiteral("No folder") : g.path.mid(g.path.lastIndexOf('/') + 1);
        out.append(QVariantMap{{"path", g.path}, {"name", custom.isEmpty() ? base : custom}, {"custom", !custom.isEmpty()}, {"collapsed", m_collapsed.contains(g.path)},
                               {"exists", g.path.isEmpty() || QFileInfo(g.path).isDir()}, {"active", g.path == m_workspace}, {"sessions", g.sessions}});
    }
    return out;
}
bool CodingAgent::openFolder(const QString &path)
{
    if (busy() || hasPendingApprovals()) return false;
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    const QFileInfo info(local);
    if (!info.isDir()) { emit statusMessage("That folder does not exist."); return false; }
    const QString dir = info.canonicalFilePath();
    if (!m_folders.contains(dir)) m_folders.append(dir);
    m_collapsed.remove(dir);
    if (dir != m_workspace) setWorkspace(dir); // the task in front belongs to the other folder: leave it as it is
    const auto *active = findSession(m_activeSessionId);
    if (!(active && active->messages.isEmpty() && active->workspace == dir)) newSession(); // an empty task here is reused, not doubled
    save(); emit sessionsChanged();
    return true;
}
void CodingAgent::renameFolder(const QString &path, const QString &name)
{
    const QString n = name.simplified().left(80);
    if (n.isEmpty()) m_folderNames.remove(path); else m_folderNames.insert(path, n);
    if (!path.isEmpty() && !m_folders.contains(path)) m_folders.append(path);
    save(); emit sessionsChanged();
}
bool CodingAgent::removeFolder(const QString &path)
{
    for (const Session &s : m_sessions) if (s.workspace == path) { emit statusMessage("Delete this folder's tasks first; the folder itself is never deleted."); return false; }
    const bool had = m_folders.removeAll(path) > 0;
    m_folderNames.remove(path); m_collapsed.remove(path);
    save(); emit sessionsChanged();
    return had;
}
void CodingAgent::setFolderCollapsed(const QString &path, bool collapsed)
{
    if (collapsed) m_collapsed.insert(path); else m_collapsed.remove(path);
    save(); emit sessionsChanged();
}
QVariantList CodingAgent::listFolders(const QString &path) const
{
    QVariantList out;
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    const QDir dir(local.isEmpty() ? QDir::homePath() : local);
    if (!dir.exists()) return out;
    for (const auto &f : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase)) {
        if (f.fileName().startsWith('.')) continue; // hidden folders stay out of the list (type the path to reach one)
        out.append(QVariantMap{{"name", f.fileName()}, {"path", f.absoluteFilePath()}});
        if (out.size() >= 500) break;
    }
    return out;
}
bool CodingAgent::folderExists(const QString &path) const { return QFileInfo(QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path).isDir(); }
QString CodingAgent::parentFolder(const QString &path) const
{
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    QDir d(local.isEmpty() ? QDir::homePath() : local);
    d.cdUp();
    return d.absolutePath();
}
QString CodingAgent::homeFolder() const { return QDir::homePath(); }
QString CodingAgent::makeFolder(const QString &parent, const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || n == "." || n == ".." || n.contains('/') || n.contains('\\') || n.size() > 120) { emit statusMessage("Use a plain folder name (no slashes)."); return {}; }
    QDir d(parent);
    if (!d.exists()) { emit statusMessage("The parent folder is gone."); return {}; }
    if (d.exists(n)) { emit statusMessage("A folder with that name already exists."); return {}; }
    if (!d.mkdir(n)) { emit statusMessage("Could not create the folder (permission?)."); return {}; }
    return d.absoluteFilePath(n);
}
QString CodingAgent::exportSessionTo(const QString &folder)
{
    const auto *s = findSession(m_activeSessionId);
    const QDir dir(QUrl(folder).isLocalFile() ? QUrl(folder).toLocalFile() : folder);
    if (!s || !dir.exists()) return {};
    QString base = s->title; base.replace(QRegularExpression("[^\\w .-]"), "_"); base = base.simplified().left(60); if (base.isEmpty()) base = "task";
    QString file = dir.absoluteFilePath(base + ".md");
    for (int i = 2; QFile::exists(file) && i < 100; ++i) file = dir.absoluteFilePath(base + " (" + QString::number(i) + ").md");
    return exportSession(file) ? file : QString();
}
QVariantMap CodingAgent::lastRun() const
{
    const auto *s = findSession(m_activeSessionId);
    if (!s) return {{"any", false}};
    const RunSummary r = summarizeRun(s->messages);
    QVariantList files;
    for (const RunFile &f : r.files) files.append(QVariantMap{{"path", f.path}, {"verb", f.verb}, {"added", f.added}, {"removed", f.removed}});
    return {{"files", files}, {"commands", r.commands}, {"failedCommands", r.failedCommands}, {"declined", r.declined}, {"added", r.added}, {"removed", r.removed},
            {"failed", QVariant(r.failed)}, {"any", r.any()}};
}
QVariantList CodingAgent::todos() const
{
    QVariantList out;
    if (const auto *s = findSession(m_activeSessionId))
        for (const TodoItem &t : s->todos) out.append(QVariantMap{{"content", t.content}, {"status", t.status}});
    return out;
}
int CodingAgent::contextWindow() const { const auto *p = findProvider(m_activeProviderId); return p ? effectiveContextWindow(*p) : 0; }
namespace {
// What the conversation would hold, by size: the provider's own count is better and wins when it is larger.
int estimatedContext(const Session &s)
{
    qint64 bytes = s.summary.toUtf8().size();
    for (int i = qBound(0, s.compactedUpTo, int(s.messages.size())); i < s.messages.size(); ++i) {
        const ChatMessage &m = s.messages.at(i);
        bytes += m.content.toUtf8().size();
        for (const ToolCall &tc : m.toolCalls) bytes += tc.argumentsJson.toUtf8().size() + (m.role == "shell" ? qMin<qsizetype>(tc.output.size(), 8000) : 0);
    }
    return 1500 + estimateTokens(bytes); // the system prompt and tool list are about 1500 tokens
}
}
int CodingAgent::contextUsed() const
{
    const auto *s = findSession(m_activeSessionId);
    if (!s) return 0;
    return qMax(cachedEstimate(*s), s->lastPromptTokens);
}
namespace { quint64 stampOf(const Session &s); }
int CodingAgent::cachedEstimate(const Session &s) const
{
    const quint64 stamp = stampOf(s);
    if (m_estimateCache < 0 || m_estimateSession != s.id || m_estimateStamp != stamp) { m_estimateCache = estimatedContext(s); m_estimateSession = s.id; m_estimateStamp = stamp; }
    return m_estimateCache;
}
bool CodingAgent::contextExact() const
{
    const auto *s = findSession(m_activeSessionId);
    return s && s->lastPromptTokens > 0 && s->lastPromptTokens >= cachedEstimate(*s);
}
void CodingAgent::rebuildFileIndex()
{
    m_fileIndex.clear(); m_fileIndexWorkspace = m_workspace; m_fileIndexAge.restart();
    if (m_workspace.isEmpty() || !QFileInfo(m_workspace).isDir()) return;
    static const QStringList skip{".git", "node_modules", "build", "dist", "target", ".next", "__pycache__", ".venv", "venv", ".cache", ".gradle", ".idea"};
    QElapsedTimer clock; clock.start();
    QStringList dirs{m_workspace};
    while (!dirs.isEmpty() && m_fileIndex.size() < 5000 && clock.elapsed() < 300) {
        const QString dir = dirs.takeFirst(); // nearest folders first
        for (const auto &f : QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
            if (f.isSymLink()) continue;
            const QString rel = QDir(m_workspace).relativeFilePath(f.filePath());
            if (privatePath(rel)) continue;
            if (f.isDir()) { if (!skip.contains(f.fileName())) dirs.append(f.filePath()); }
            else if (m_fileIndex.size() < 5000) m_fileIndex.append(rel);
        }
    }
}
QVariantList CodingAgent::findFiles(const QString &query, int limit)
{
    if (m_fileIndexWorkspace != m_workspace || !m_fileIndexAge.isValid() || m_fileIndexAge.elapsed() > 5000) rebuildFileIndex();
    QVariantList out;
    for (const QString &path : AgentCommands::rankFiles(query, m_fileIndex, qBound(1, limit, 50))) {
        const int slash = path.lastIndexOf('/');
        out.append(QVariantMap{{"path", path}, {"name", path.mid(slash + 1)}, {"dir", slash < 0 ? QString() : path.left(slash)}});
    }
    return out;
}
QVariantMap CodingAgent::parseSlash(const QString &text) const
{
    const auto r = AgentCommands::parseSlash(text);
    return {{"ok", r.ok}, {"name", r.name}, {"args", r.args}};
}
QVariantList CodingAgent::slashCompletions(const QString &prefix) const
{
    QVariantList out;
    for (const auto &c : AgentCommands::completeSlash(prefix)) out.append(QVariantMap{{"name", c.name}, {"args", c.args}, {"description", c.description}});
    return out;
}
void CodingAgent::runShell(const QString &command)
{
    const QString cmd = command.trimmed();
    if (cmd.isEmpty() || busy() || m_resumeQueued || hasPendingApprovals()) return;
    if (m_workspace.isEmpty() || !QFileInfo(m_workspace).isDir()) { emit statusMessage("Choose a workspace first."); return; }
    auto *s = ensureActiveSession(); if (!s) return;
    ToolCall tc; tc.id = newId(); tc.name = "run_command"; tc.command = cmd; tc.status = "running"; tc.summary = "You ran this";
    ChatMessage m; m.id = newId(); m.role = "shell"; m.content = cmd; m.timestamp = QDateTime::currentMSecsSinceEpoch(); m.toolCalls.append(tc);
    s->messages.append(m);
    if (s->title == "New task") s->title = ("!" + cmd).simplified().left(60);
    s->updatedAt = QDateTime::currentMSecsSinceEpoch();
    m_lastError.clear(); emit errorChanged(); save(); emit messagesChanged(); emit sessionsChanged();
    startShellCommand(m.id, tc.id, cmd, true); // the person typed it: no approval, and no model turn afterwards
}
void CodingAgent::addNote(const QString &text)
{
    auto *s = ensureActiveSession(); if (!s) return;
    ChatMessage m; m.id = newId(); m.role = "note"; m.content = text; m.timestamp = QDateTime::currentMSecsSinceEpoch(); s->messages.append(m);
    save(); emit messagesChanged();
}
QString CodingAgent::renderMarkdown(const QString &markdown, const QString &family, int px, const QString &code, const QString &codeBg, const QString &link, const QString &border) const
{
    MarkdownHtml::Style st;
    st.family = family; st.px = qBound(10, px, 30);
    if (QColor c(code); c.isValid()) st.code = c;
    if (QColor c(codeBg); c.isValid()) st.codeBg = c;
    if (QColor c(link); c.isValid()) st.link = c;
    if (QColor c(border); c.isValid()) st.border = c;
    return MarkdownHtml::render(markdown, st);
}
QString CodingAgent::fullText(const QString &messageId, const QString &toolCallId) const
{
    if (const auto *s = findSession(m_activeSessionId))
        for (const ChatMessage &m : s->messages) if (m.id == messageId) {
            if (toolCallId.isEmpty()) return m.content;
            for (const ToolCall &tc : m.toolCalls) if (tc.id == toolCallId) return tc.output;
        }
    return {};
}
QString CodingAgent::lastAnswer() const
{
    if (const auto *s = findSession(m_activeSessionId))
        for (int i = int(s->messages.size()) - 1; i >= 0; --i)
            if (s->messages.at(i).role == "assistant" && !s->messages.at(i).content.trimmed().isEmpty()) return s->messages.at(i).content;
    return {};
}
QString CodingAgent::forkSession()
{
    if (busy() || hasPendingApprovals()) return {};
    const auto *src = findSession(m_activeSessionId); if (!src) return {};
    Session copy = *src;
    copy.id = newId(); copy.title = ("Fork of " + src->title).left(120); copy.updatedAt = QDateTime::currentMSecsSinceEpoch();
    m_sessions.prepend(copy); m_activeSessionId = copy.id;
    if (!copy.plan.isEmpty()) storePlan(copy);
    save(); emit sessionsChanged(); emit messagesChanged(); emit planChanged(); emit statusMessage("Forked: you are now in the copy.");
    return copy.id;
}
bool CodingAgent::exportSession(const QString &path) const
{
    const auto *s = findSession(m_activeSessionId); if (!s) return false;
    const QUrl url(path); const QString file = url.isLocalFile() ? url.toLocalFile() : path;
    QString md = "# " + s->title + "\n\nWorkspace: `" + s->workspace + "`\n\n";
    for (const ChatMessage &m : s->messages) {
        if (m.role == "user") {
            md += "## You\n\n" + (m.shown.isEmpty() ? m.content : m.shown) + "\n\n";
            for (const Attachment &a : m.attachments) md += "- attached `" + a.path + "`\n";
            if (!m.attachments.isEmpty()) md += "\n";
        } else if (m.role == "assistant") {
            md += "## Agent\n\n" + m.content + "\n\n";
            for (const ToolCall &tc : m.toolCalls) md += "- " + tc.name + " (" + tc.status + ")" + (tc.summary.isEmpty() ? QString() : " " + tc.summary) + "\n";
            if (!m.toolCalls.isEmpty()) md += "\n";
        } else if (m.role == "shell" && !m.toolCalls.isEmpty()) md += "## You ran `" + m.content + "`\n\n```\n" + m.toolCalls.first().output + "\n```\n\n";
        else if (m.role == "note") md += "---\n_" + m.content + "_\n\n";
    }
    if (!s->plan.isEmpty()) md += "---\n\n## plan.md\n\n" + s->plan + "\n";
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(md.toUtf8());
    return f.commit();
}
bool CodingAgent::shouldCompact(const Session &s, const ProviderConfig &p, qint64 payloadBytes) const
{
    if (int(s.messages.size()) - s.compactedUpTo < 8) return false;
    return estimateTokens(payloadBytes) >= qint64(effectiveContextWindow(p)) * 85 / 100 && compactCutIndex(s.messages, s.compactedUpTo, 2) >= 0;
}
void CodingAgent::compactNow(const QString &focus)
{
    if (busy() || hasPendingApprovals()) { emit statusMessage("Finish or stop the current task before compacting."); return; }
    startCompaction(focus, false);
}
void CodingAgent::startCompaction(const QString &focus, bool thenContinue)
{
    auto *s = findSession(m_activeSessionId); auto *p = findProvider(m_activeProviderId);
    auto carryOn = [this, thenContinue] { if (thenContinue) QTimer::singleShot(0, this, [this] { if (!busy()) startAgentTurn(); }); };
    if (!s || !p || m_compactReply) return;
    // Keep the last few messages; if they alone are still a big part of the window (huge pastes, long outputs), keep fewer.
    int cut = -1;
    for (int keep : {6, 4, 2}) {
        const int c = compactCutIndex(s->messages, s->compactedUpTo, keep);
        if (c < 0) continue;
        cut = c;
        qint64 kept = 0;
        for (int i = c; i < s->messages.size(); ++i) kept += s->messages.at(i).content.toUtf8().size();
        if (estimateTokens(kept) <= effectiveContextWindow(*p) * 4 / 10) break;
    }
    if (cut < 0) { emit statusMessage("Nothing to compact yet."); carryOn(); return; }
    QNetworkRequest req(chatEndpointUrl(*p)); applyAuthHeaders(req, *p, apiKeyFor(p->id)); req.setTransferTimeout(120000);
    const QByteArray payload = buildSummaryPayload(*p, flattenForSummary(s->messages, s->compactedUpTo, cut), focus, s->summary);
    const QString sessionId = s->id, protocol = p->protocol;
    m_compactReply = m_nam.post(req, payload);
    emit busyChanged(); emit contextChanged();
    connect(m_compactReply, &QNetworkReply::finished, this, [this, sessionId, protocol, cut, thenContinue, carryOn] {
        auto *r = m_compactReply; m_compactReply = nullptr;
        const bool ok = r->error() == QNetworkReply::NoError && r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        const QString summary = ok ? summaryFromResponse(protocol, r->read(2 * 1024 * 1024)) : QString();
        const QString why = r->errorString();
        r->deleteLater();
        auto *s = findSession(sessionId);
        if (s && !summary.isEmpty() && cut <= s->messages.size()) {
            s->summary = summary;
            ChatMessage note; note.id = newId(); note.role = "note"; note.timestamp = QDateTime::currentMSecsSinceEpoch();
            note.content = "Compacted · earlier messages were summarized to save context";
            s->messages.insert(cut, note); s->compactedUpTo = cut; s->lastPromptTokens = 0;
            save(); emit statusMessage(thenContinue ? "The context was nearly full: older messages were summarized." : "Compacted: older messages were summarized.");
        } else emit statusMessage(ok ? "The model returned no summary; nothing was compacted." : "Could not compact: " + why);
        emit busyChanged(); emit messagesChanged(); emit contextChanged();
        carryOn();
    });
}
void CodingAgent::setDetail(const QString &detail)
{
    const QString d = detail.trimmed().toLower() == "full" ? "full" : "headings";
    if (d == m_detail) return;
    m_detail = d; save(); emit detailChanged();
}
void CodingAgent::setEffort(const QString &effort)
{
    const QString e = normalizeEffort(effort);
    if (e == m_effort) return;
    m_effort = e; save(); emit effortChanged();
}
// Mode, model and effort can change while the agent works: the request in flight keeps what it was sent with, and the next one
// (the next step of the same task) uses the new setting.
void CodingAgent::rejectPendingApprovals(const QString &reason)
{
    auto *s = findSession(m_activeSessionId); if (!s) return;
    QList<QPair<QString, QString>> pending;
    for (auto &m : s->messages) for (auto &tc : m.toolCalls) if (tc.status == "awaiting_approval") { tc.status = "rejected"; pending.append({tc.id, tc.name}); }
    for (const auto &p : pending) appendToolResultMessage(*s, p.first, p.second, reason);
    if (!pending.isEmpty()) { save(); emit messagesChanged(); resumeAfterToolCompletion(); }
}
void CodingAgent::setMode(const QString &mode)
{
    const QString next = normalizeMode(mode);
    if (next == m_mode) return;
    m_mode = next;
    if (auto *s = findSession(m_activeSessionId)) s->mode = m_mode;
    save(); emit modeChanged();
    // Plan Mode changes nothing: what was waiting for Apply / Run is dropped. Auto (already agreed to in the dialog) takes what waits.
    if (m_mode == "plan") rejectPendingApprovals("The person switched to Plan Mode, so this was not applied. Do not retry it; plan instead.");
    else if (m_mode == "auto" && hasPendingApprovals()) QTimer::singleShot(0, this, [this] { if (m_mode == "auto") approveAllPending(); });
}
void CodingAgent::setWorkspace(const QString &path)
{
    if (busy() || hasPendingApprovals()) return;
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    const QFileInfo info(local);
    if (!info.isDir()) { emit statusMessage("Select an existing workspace folder."); return; }
    if (info.canonicalFilePath() == m_workspace) return;
    m_workspace = info.canonicalFilePath();
    if (!m_folders.contains(m_workspace)) m_folders.append(m_workspace);
    // A different workspace must not reuse tools or context from a previous project.
    m_activeSessionId.clear(); save(); emit workspaceChanged(); emit sessionsChanged(); emit messagesChanged();
}
QVariantList CodingAgent::workspaceFiles() const
{
    QVariantList out;
    if (m_workspace.isEmpty()) return out;
    for (const auto &f : QDir(m_workspace).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name)) {
        if (out.size() >= 200) break;
        out.append(QVariantMap{{"name", f.fileName()}, {"path", f.fileName()}, {"directory", f.isDir()}});
    }
    return out;
}
QVariantList CodingAgent::sessions() const
{
    QVariantList out; for (const auto &s : m_sessions) out.append(QVariantMap{{"id", s.id}, {"title", s.title}, {"workspace", s.workspace}}); return out;
}
namespace {
// A cheap fingerprint of a message (sizes and statuses, not the text), so "did it change?" never needs the text.
quint64 fingerprint(const ChatMessage &m)
{
    quint64 h = qHash(m.id);
    auto mix = [&h](quint64 v) { h = (h ^ v) * 1099511628211ULL; };
    mix(quint64(m.content.size())); mix(quint64(m.shown.size())); mix(quint64(m.reasoning.size())); mix(quint64(m.attachments.size()));
    for (const ToolCall &tc : m.toolCalls) {
        mix(qHash(tc.status)); mix(quint64(tc.output.size())); mix(quint64(tc.summary.size())); mix(quint64(tc.diffText.size())); mix(quint64(tc.command.size()));
        mix(quint64(tc.argumentsJson.size()));
    }
    return h;
}
quint64 stampOf(const Session &s)
{
    quint64 h = quint64(s.messages.size()) * 31 + quint64(s.compactedUpTo) * 7 + quint64(s.summary.size());
    for (const ChatMessage &m : s.messages) h = (h ^ fingerprint(m)) * 1099511628211ULL;
    return h;
}
// The chat shows the end of a long command output (150 lines, 12,000 characters): laying out tens of thousands of lines on every
// update froze the panel. The agent itself always gets the whole output; "Copy all" gives it to you.
QString tailForDisplay(const QString &text)
{
    if (text.size() <= 4000 && text.count('\n') <= 150) return text;
    const QStringList lines = text.split('\n');
    const int keep = int(qMin<qsizetype>(lines.size(), 150));
    QString tail = lines.mid(lines.size() - keep).join('\n');
    if (tail.size() > 12000) { tail = tail.right(12000); tail.remove(0, tail.indexOf('\n') + 1); }
    const int hidden = int(lines.size()) - int(tail.count('\n') + 1);
    return hidden > 0 ? "[… " + QString::number(hidden) + " earlier lines hidden]\n" + tail : tail;
}
QString headForDisplay(const QString &text) // a diff: the start, 400 lines
{
    if (text.size() <= 20000 && text.count('\n') <= 400) return text;
    const QStringList lines = text.split('\n');
    const QString head = lines.mid(0, 400).join('\n').left(20000);
    return head + "\n[… the rest of the diff is not shown]";
}
} // namespace
int CodingAgent::messageCount() const { const auto *s = findSession(m_activeSessionId); return s ? int(s->messages.size()) : 0; }
QVariantList CodingAgent::messages() const
{
    const auto *s = findSession(m_activeSessionId);
    if (!s) return {};
    const quint64 stamp = stampOf(*s); // a pass over sizes: far cheaper than building the list, which every reader used to do
    if (m_messagesCacheSession == s->id && m_messagesCacheStamp == stamp) return m_messagesCache;
    QVariantList out;
    for (const auto &m : s->messages) {
        QVariantMap map = messageToJson(m).toVariantMap();
        map.insert("fp", QString::number(fingerprint(m))); // QML compares this to patch only the messages that changed
        if (m.role == "tool") map.insert("content", tailForDisplay(m.content));
        QVariantList calls = map.value("toolCalls").toList();
        for (QVariant &c : calls) {
            QVariantMap call = c.toMap();
            call.insert("output", tailForDisplay(call.value("output").toString()));
            const ToolHeadline hl = toolHeadline(m.toolCalls.at(&c - &calls.first()));
            call.insert("verb", hl.verb); call.insert("target", hl.target); call.insert("stat", hl.stat); call.insert("tone", hl.tone); call.insert("icon", hl.icon);
            call.insert("diffText", headForDisplay(call.value("diffText").toString()));
            c = call;
        }
        map.insert("toolCalls", calls);
        out.append(map);
    }
    m_messagesCache = out; m_messagesCacheSession = s->id; m_messagesCacheStamp = stamp;
    return out;
}
bool CodingAgent::hasPendingApprovals() const
{
    if (const auto *s = findSession(m_activeSessionId)) for (const auto &m : s->messages) for (const auto &tc : m.toolCalls)
        if (tc.status == "awaiting_approval") return true;
    return false;
}
QString CodingAgent::newSession(const QString &title)
{
    if (busy() || hasPendingApprovals()) return {};
    Session s; s.id = newId(); s.title = title.isEmpty() ? "New task" : title; s.workspace = m_workspace;
    s.mode = m_mode; s.providerId = m_activeProviderId; s.modelId = activeModelId(); s.updatedAt = QDateTime::currentMSecsSinceEpoch();
    m_sessions.prepend(s); m_activeSessionId = s.id; save(); emit sessionsChanged(); emit messagesChanged(); return s.id;
}
void CodingAgent::selectSession(const QString &id)
{
    if (busy() || hasPendingApprovals()) return;
    if (const auto *s = findSession(id)) {
        // Picking a task redraws only what it changes: the same folder, mode or provider as before is not announced again
        // (announcing it made the folder chip, the mode and the provider list blink).
        const QString oldWorkspace = m_workspace, oldMode = m_mode, oldProvider = m_activeProviderId;
        const bool sameTask = id == m_activeSessionId;
        m_activeSessionId = id; m_mode = s->mode == "auto" ? "manual" : s->mode;
        // A task made before any folder was chosen has none: it adopts the current one instead of blanking it.
        if (!s->workspace.isEmpty()) m_workspace = s->workspace;
        else if (auto *mine = findSession(id)) mine->workspace = m_workspace;
        if (findProvider(s->providerId)) m_activeProviderId = s->providerId;
        save();
        if (sameTask && oldWorkspace == m_workspace && oldMode == m_mode && oldProvider == m_activeProviderId) return;
        emit sessionsChanged();
        if (!sameTask) emit messagesChanged();
        if (oldWorkspace != m_workspace) emit workspaceChanged();
        if (oldMode != m_mode) emit modeChanged();
        if (oldProvider != m_activeProviderId) emit providersChanged();
    }
}
void CodingAgent::deleteSession(const QString &id)
{
    if (busy() || hasPendingApprovals()) return;
    for (int i = 0; i < m_sessions.size(); ++i) if (m_sessions[i].id == id) { m_sessions.removeAt(i); QDir(QFileInfo(planFileFor(id)).absolutePath()).removeRecursively(); break; }
    if (m_activeSessionId == id) m_activeSessionId.clear();
    save(); emit sessionsChanged(); emit messagesChanged(); emit planChanged();
}
void CodingAgent::clearSession(const QString &id)
{
    if (busy() || hasPendingApprovals()) return;
    if (auto *s = findSession(id.isEmpty() ? m_activeSessionId : id)) {
        s->messages.clear(); s->plan.clear(); s->planStatus.clear(); s->todos.clear(); s->summary.clear(); s->compactedUpTo = 0; s->lastPromptTokens = 0;
        QDir(QFileInfo(planFileFor(s->id)).absolutePath()).removeRecursively();
    }
    save(); emit messagesChanged();
}
void CodingAgent::renameSession(const QString &id, const QString &title)
{
    if (auto *s = findSession(id)) { s->title = title.left(120); save(); emit sessionsChanged(); }
}
void CodingAgent::sendPrompt(const QString &prompt, const QVariantList &attachedFiles)
{
    QStringList files;
    for (const QVariant &f : attachedFiles) files.append(f.toString());
    startPrompt(prompt, true, files);
}
void CodingAgent::startPrompt(const QString &prompt, bool mayStartPlanMode, const QStringList &files)
{
    if (busy() || m_resumeQueued || hasPendingApprovals() || prompt.trimmed().isEmpty()) return;
    if (m_workspace.isEmpty() || !QFileInfo(m_workspace).isDir()) { emit statusMessage("Choose a workspace first."); return; }
    auto *s = ensureActiveSession(); if (!s) return;
    if (mayStartPlanMode && m_mode != "plan" && asksForPlan(prompt)) { // asking for a plan or a discussion: nothing may change until the person accepts
        m_mode = "plan"; s->mode = m_mode; emit modeChanged();
        emit statusMessage("Plan mode: the agent will discuss and write a plan first. Nothing changes until you accept it.");
    }
    // @files: the ones picked from the list and the ones typed out in full, read through the same sandbox as read_file
    QStringList paths = files;
    for (const auto &mention : AgentCommands::mentionTokens(prompt))
        if (!paths.contains(mention.path) && QFileInfo(resolveWorkspacePath(m_workspace, mention.path)).isFile()) paths.append(mention.path);
    ChatMessage m; m.id = newId(); m.role = "user"; m.content = prompt; m.timestamp = QDateTime::currentMSecsSinceEpoch();
    qint64 total = 0;
    for (const QString &path : paths) {
        if (m.attachments.size() >= 8) { emit statusMessage("At most 8 files can be attached to one message."); break; }
        const QString abs = resolveWorkspacePath(m_workspace, path);
        const QString rel = abs.isEmpty() ? QString() : QDir(m_workspace).relativeFilePath(abs);
        if (abs.isEmpty() || privatePath(rel) || !QFileInfo(abs).isFile()) { emit statusMessage("Could not attach " + path + ": outside the workspace, private or not a file."); continue; }
        bool readable = false; const QString text = readText(abs, &readable);
        if (!readable) { emit statusMessage("Could not attach " + path + ": unreadable, binary or larger than 256 KiB."); continue; }
        if (total + text.size() > 100000) { emit statusMessage("Attachments are limited to 100 KB in total; " + path + " was left out."); continue; }
        total += text.size();
        m.content += "\n\n<file path=\"" + rel + "\">\n" + text + "\n</file>";
        m.attachments.append({rel, qint64(text.toUtf8().size())});
    }
    if (!m.attachments.isEmpty()) m.shown = prompt;
    s->messages.append(m);
    if (s->title == "New task") s->title = prompt.simplified().left(60);
    s->updatedAt = QDateTime::currentMSecsSinceEpoch();
    m_autoCompacted = false; m_continueCount = 0;
    clearRetry();
    m_turnDepth = 0; m_lastError.clear(); emit errorChanged(); save(); emit messagesChanged(); emit sessionsChanged(); startAgentTurn();
}
void CodingAgent::startAgentTurn()
{
    if (busy()) return;
    auto *s = findSession(m_activeSessionId); auto *p = findProvider(m_activeProviderId);
    if (!s || !p) return;
    if (++m_turnDepth > 40) { m_lastError = "Agent stopped after 40 tool rounds. Send a new prompt to continue."; emit errorChanged(); return; }
    m_streamAcc = {}; m_sseBuffer.clear(); m_streamContent.clear(); m_streamReasoning.clear(); m_responseBytes = 0;
    QNetworkRequest req(chatEndpointUrl(*p)); applyAuthHeaders(req, *p, apiKeyFor(p->id));
    QString extra = m_systemPrompt;
    if (m_mode == "plan" && s->planStatus == "pending") extra += (extra.isEmpty() ? "" : "\n") + planContextPrompt(s->plan);
    if (m_mode != "plan" && !s->todos.isEmpty()) extra += (extra.isEmpty() ? "" : "\n") + todoContextPrompt(s->todos);
    ProviderConfig sent = *p; // a copy: a bigger token limit after a cut answer applies to this task only, not to the saved settings
    sent.maxTokens = boostedMaxTokens(*p, m_tokenBoost);
    m_errorBody.clear();
    const QByteArray payload = buildRequestPayload(sent, m_mode, m_workspace, historyForRequest(*s), true, extra, m_effort);
    if (!m_autoCompacted && shouldCompact(*s, *p, payload.size())) { // nearly full: summarize the older messages first, then carry on
        m_autoCompacted = true; --m_turnDepth;
        startCompaction(QString(), true);
        return;
    }
    if (payload.size() > 8 * 1024 * 1024) {
        m_lastError = "Conversation exceeds 8 MiB. Start a new task."; emit errorChanged(); m_turnDepth = 0; return;
    }
    m_reply = m_nam.post(req, payload);
    connect(m_reply, &QNetworkReply::readyRead, this, &CodingAgent::onStreamReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &CodingAgent::onStreamFinished);
    emit busyChanged(); emit streamingChanged(); emit streamChunkChanged();
}
void CodingAgent::onStreamReadyRead()
{
    if (!m_reply) return;
    const auto bytes = m_reply->readAll();
    m_responseBytes += bytes.size();
    if (m_responseBytes > 2 * 1024 * 1024) { m_streamAcc.errorText = "Response exceeds 2 MiB limit."; m_reply->abort(); return; }
    m_sseBuffer += bytes;
    const auto *p = findProvider(m_activeProviderId); if (!p) return;
    int end;
    while ((end = m_sseBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_sseBuffer.left(end).trimmed(); m_sseBuffer.remove(0, end + 1);
        if (line.startsWith("data:")) m_streamAcc.feedDataLine(line.mid(5), p->protocol);
        else if (m_errorBody.size() < 4096) m_errorBody += line + "\n";
    }
    m_streamContent = m_streamAcc.content; m_streamReasoning = m_streamAcc.reasoning;
    if (!m_chunkTimer.isActive()) m_chunkTimer.start(); // throttled: redrawing a long answer at every read made the panel heavy
}
void CodingAgent::onStreamFinished()
{
    if (!m_reply) return;
    onStreamReadyRead();
    if (!m_reply) return;
    auto *r = m_reply; m_reply = nullptr;
    const bool truncated = m_streamAcc.finishReason == "length" || m_streamAcc.finishReason == "max_tokens";
    if (m_errorBody.size() < 4096) m_errorBody += m_sseBuffer; // an error body is one line without a newline
    if (truncated) {
        // The answer ran into the token limit. Tool calls cut in the middle are never run, so a bigger limit is tried (up to four
        // times the configured one); a cut plain answer is kept, and the model is asked to go on.
        auto *ts = findSession(m_activeSessionId); auto *tp = findProvider(m_activeProviderId);
        const bool hasText = !m_streamAcc.content.trimmed().isEmpty(), hasTools = !m_streamAcc.toolCalls.isEmpty();
        if (ts && tp) {
            if (hasTools || !hasText) {
                const int next = boostedMaxTokens(*tp, m_tokenBoost + 1);
                if (!m_boostBlocked && m_tokenBoost < 2 && next > boostedMaxTokens(*tp, m_tokenBoost)) {
                    ++m_tokenBoost; r->deleteLater();
                    scheduleSoon("The answer was cut by the token limit. Trying again with up to " + QString::number(next) + " tokens…");
                    return;
                }
                m_streamAcc.errorText = "The answer was cut by the token limit even at " + QString::number(boostedMaxTokens(*tp, m_tokenBoost)) + " tokens. Raise Max tokens under Advanced, or ask for a smaller step.";
            } else {
                r->deleteLater();
                ChatMessage part; part.id = newId(); part.role = "assistant"; part.content = m_streamAcc.content; part.reasoning = m_streamAcc.reasoning; part.timestamp = QDateTime::currentMSecsSinceEpoch();
                ts->messages.append(part);
                const bool more = m_continueCount < 4;
                ChatMessage next; next.id = newId(); next.timestamp = QDateTime::currentMSecsSinceEpoch();
                if (more) { next.role = "user"; next.shown = "Continue"; next.content = "Continue exactly where you stopped. Do not repeat what you already wrote."; ++m_continueCount; }
                else { next.role = "note"; next.content = "Cut by the token limit · type \"continue\" to go on"; m_turnDepth = 0; }
                ts->messages.append(next);
                m_streamContent.clear(); m_streamReasoning.clear(); m_chunkTimer.stop();
                save(); emit messagesChanged(); emit streamChunkChanged(); emit streamingChanged(); emit busyChanged(); emit contextChanged();
                if (more) scheduleSoon("The answer was cut by the token limit; asking the model to continue…");
                else emit statusMessage("The answer was cut by the token limit. Raise Max tokens under Advanced, or type \"continue\".");
                return;
            }
        } else m_streamAcc.errorText = "Model hit its token limit; incomplete response discarded.";
    }
    const bool ok = r->error() == QNetworkReply::NoError && r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
        && m_streamAcc.errorText.isEmpty() && m_streamAcc.done;
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netError = r->error();
    const int retryAfter = r->rawHeader("Retry-After").toInt();
    if (!ok) {
        m_lastError = !m_streamAcc.errorText.isEmpty() ? m_streamAcc.errorText : r->error() != QNetworkReply::NoError ? r->errorString() : "Incomplete or unsupported streaming response.";
        // an HTTP error carries the provider's own explanation ({"error":{"message":...}}): say that instead of "server replied: Bad Request"
        if (m_streamAcc.errorText.isEmpty() && status > 0 && status != 200) {
            const QJsonObject body = QJsonDocument::fromJson(m_errorBody.trimmed()).object();
            const QJsonValue e = body.value("error");
            const QString said = e.isObject() ? e.toObject().value("message").toString() : e.toString();
            if (!said.simplified().isEmpty()) m_lastError = "HTTP " + QString::number(status) + ": " + said.simplified().left(300);
        }
    }
    r->deleteLater();
    auto *s = findSession(m_activeSessionId);
    if (!ok && s) {
        // Asked for more tokens than this provider allows (HTTP 400 that talks about tokens): go back to the last limit it took.
        if (status == 400 && m_tokenBoost > 0 && QString::fromUtf8(m_errorBody).contains(QRegularExpression("token", QRegularExpression::CaseInsensitiveOption))) {
            --m_tokenBoost; m_boostBlocked = true;
            scheduleSoon("This provider takes fewer tokens; trying again with up to " + QString::number(boostedMaxTokens(*findProvider(m_activeProviderId), m_tokenBoost)) + "…");
            return;
        }
        // A problem that may pass is tried again (up to 10 times); a wrong key, a bad request or a full quota is not.
        bool transient;
        if (truncated) transient = false;
        else if (status > 0 && status != 200) transient = isRetryableStatus(status);
        else if (status == 200) transient = m_streamAcc.errorText.isEmpty() ? true /* the stream was cut */ : isRetryableMessage(m_streamAcc.errorText);
        else switch (netError) {
            case QNetworkReply::ConnectionRefusedError: case QNetworkReply::RemoteHostClosedError: case QNetworkReply::HostNotFoundError:
            case QNetworkReply::TimeoutError: case QNetworkReply::TemporaryNetworkFailureError: case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::UnknownNetworkError: case QNetworkReply::ProxyConnectionRefusedError: case QNetworkReply::ProxyConnectionClosedError:
            case QNetworkReply::ProxyTimeoutError: transient = true; break;
            default: transient = false;
        }
        if (transient && m_retryCount < MaxRetries) {
            ++m_retryCount;
            const int delay = retryDelayMs(m_retryCount, retryAfter, m_retryBaseMs);
            const QString what = status > 0 && status != 200 ? "HTTP " + QString::number(status) : m_streamAcc.errorText.isEmpty() ? QString("connection lost") : m_streamAcc.errorText.simplified().left(60);
            m_retryStatus = "Provider problem (" + what + "). Retrying " + QString::number(m_retryCount) + "/" + QString::number(MaxRetries) + " in " + QString::number((delay + 999) / 1000) + " s…";
            --m_turnDepth; // the failed attempt does not count as a round of tool use
            m_lastError.clear();
            m_streamContent.clear(); m_streamReasoning.clear();
            m_retryTimer.start(delay);
            emit streamChunkChanged(); emit streamingChanged(); emit errorChanged(); emit retryChanged(); emit busyChanged();
            return;
        }
        if (transient) m_lastError += QString(" (gave up after %1 retries)").arg(MaxRetries);
    }
    if (!ok) emit errorChanged();
    clearRetry();
    QString id;
    if (s && ok) {
        ChatMessage m; m.id = newId(); id = m.id; m.role = "assistant"; m.content = m_streamAcc.content; m.reasoning = m_streamAcc.reasoning; m.toolCalls = m_streamAcc.toolCalls;
        for (auto &tc : m.toolCalls) if (tc.id.isEmpty()) tc.id = newId();
        m.timestamp = QDateTime::currentMSecsSinceEpoch(); s->messages.append(m);
        if (m_streamAcc.promptTokens > 0) s->lastPromptTokens = m_streamAcc.promptTokens + m_streamAcc.completionTokens; // the next request starts from here
    }
    m_chunkTimer.stop();
    m_streamContent.clear(); m_streamReasoning.clear(); emit streamChunkChanged(); emit streamingChanged(); emit busyChanged(); emit contextChanged();
    save(); emit messagesChanged();
    if (ok && !m_streamAcc.toolCalls.isEmpty() && m_streamAcc.finishReason != "length" && m_streamAcc.finishReason != "max_tokens") processToolCalls(id);
    else {
        m_turnDepth = 0;
        if (!ok) emit agentEvent("error", "Agent error", m_lastError.simplified().left(140), "close");
        else if (s && s->planStatus != "pending") // a plan waiting for you has already said so
            emit agentEvent("done", "Agent finished", (m_streamAcc.content.simplified().isEmpty() ? s->title : m_streamAcc.content.simplified()).left(110), "check");
    }
}
void CodingAgent::appendToolResultMessage(Session &s, const QString &id, const QString &name, const QString &result)
{
    ChatMessage m; m.id = newId(); m.role = "tool"; m.toolCallId = id; m.toolName = name; m.content = result; m.timestamp = QDateTime::currentMSecsSinceEpoch(); s.messages.append(m);
}
void CodingAgent::processToolCalls(const QString &messageId)
{
    auto *s = findSession(m_activeSessionId); if (!s) return;
    QList<QString> ids;
    for (const auto &m : s->messages) if (m.id == messageId) for (const auto &tc : m.toolCalls) ids.append(tc.id);
    for (const QString &id : ids) {
        ToolCall *tc = toolIn(s, messageId, id); if (!tc || tc->status != "pending") continue;
        QJsonParseError err; const auto doc = QJsonDocument::fromJson(tc->argumentsJson.toUtf8(), &err);
        const auto args = doc.object();
        QString result;
        if (err.error != QJsonParseError::NoError || !doc.isObject()) result = "Error: invalid tool arguments.";
        else if (tc->name == "list_files") result = execListFiles(args, &tc->summary);
        else if (tc->name == "read_file") result = execReadFile(args, &tc->summary);
        else if (tc->name == "search_files") result = execSearchFiles(args, &tc->summary);
        else if (tc->name == "todo_write") {
            QString err;
            const QList<TodoItem> todos = parseTodos(args, &err);
            if (m_mode == "plan") result = "Error: to-do lists are not used in Plan Mode; write the plan with write_plan.";
            else if (!err.isEmpty()) result = "Error: " + err;
            else {
                s->todos = todos;
                const int done = todosDone(todos);
                tc->summary = "To-do updated · " + QString::number(done) + "/" + QString::number(todos.size());
                result = "To-do list saved (" + QString::number(todos.size()) + " items, " + QString::number(done) + " done). Carry on with the in-progress item.";
                emit todosChanged();
            }
        }
        else if (tc->name == "write_plan") {
            const QString content = args.value("content").toString().trimmed();
            if (m_mode != "plan") result = "Error: write_plan only works in Plan Mode.";
            else if (content.isEmpty()) result = "Error: the plan is empty.";
            else if (content.size() > 64 * 1024) result = "Error: the plan is longer than 64 KB; shorten it.";
            else {
                s->plan = content; s->planStatus = "pending"; storePlan(*s);
                emit agentEvent("plan", "Plan ready", s->title.left(80) + " · read plan.md and decide", "pen");
                tc->summary = "Plan saved as plan.md (" + QString::number(content.count('\n') + 1) + " lines)";
                result = "Plan saved. Reply in at most two sentences and stop: the person will accept, reject or ask for changes.";
                emit planChanged();
            }
        }
        else if (m_mode == "plan") result = "Error: Plan Mode prohibits file changes and shell commands.";
        else if (tc->name == "propose_edit") {
            prepareProposeEdit(*tc, args);
            if (tc->status == "error") result = tc->output;
            else { tc->status = "awaiting_approval"; continue; }
        } else if (tc->name == "run_command") {
            tc->command = args.value("command").toString(); tc->summary = args.value("description").toString();
            if (tc->command.trimmed().isEmpty()) result = "Error: empty command.";
            else { tc->status = "awaiting_approval"; continue; }
        } else result = "Error: unknown tool.";
        tc->output = result; tc->status = result.startsWith("Error:") ? "error" : "completed";
        const QString name = tc->name; appendToolResultMessage(*s, id, name, result);
    }
    save(); emit messagesChanged();
    if (m_mode != "auto") { // waiting on you: Apply / Run or Reject (one toast, for the first)
        bool told = false;
        for (const auto &m : s->messages) if (m.id == messageId) for (const auto &tc : m.toolCalls) if (!told && tc.status == "awaiting_approval") {
            told = true;
            emit agentEvent("approval", tc.name == "run_command" ? "Agent wants to run a command" : "Agent wants to edit a file",
                            (tc.name == "run_command" ? tc.command : tc.filePath).simplified().left(110), "info");
        }
    }
    if (m_mode == "auto") approveAllPending();
    else resumeAfterToolCompletion();
}
void CodingAgent::resumeAfterToolCompletion()
{
    if (busy() || hasPendingApprovals()) return;
    if (const auto *s = findSession(m_activeSessionId)) for (const auto &m : s->messages) for (const auto &tc : m.toolCalls) if (tc.status == "pending" || tc.status == "running") return;
    if (m_resumeQueued) return;
    m_resumeQueued = true;
    const auto epoch = m_epoch;
    const auto sessionId = m_activeSessionId;
    emit busyChanged();
    QTimer::singleShot(0, this, [this, epoch, sessionId] {
        if (epoch != m_epoch || sessionId != m_activeSessionId) return;
        m_resumeQueued = false;
        if (!busy() && !hasPendingApprovals() && m_turnDepth > 0) startAgentTurn();
        emit busyChanged();
    });
}
QString CodingAgent::execListFiles(const QJsonObject &args, QString *summary) const
{
    const QString path = resolveWorkspacePath(m_workspace, args.value("path").toString("."));
    if (path.isEmpty() || !QFileInfo(path).isDir()) return "Error: directory outside workspace or missing.";
    QStringList out;
    for (const auto &f : QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::DirsFirst | QDir::Name)) {
        if (out.size() >= 300) { out.append("[truncated]"); break; }
        if (!privatePath(QDir(m_workspace).relativeFilePath(f.filePath()))) out.append(f.fileName() + (f.isDir() ? "/" : ""));
    }
    *summary = "List " + args.value("path").toString("."); return out.join('\n');
}
QString CodingAgent::execReadFile(const QJsonObject &args, QString *summary) const
{
    const QString path = resolveWorkspacePath(m_workspace, args.value("path").toString());
    if (path.isEmpty() || privatePath(QDir(m_workspace).relativeFilePath(path))) return "Error: restricted path.";
    bool ok; const QString text = readText(path, &ok); if (!ok) return "Error: file unreadable, binary or larger than 256 KiB.";
    const auto lines = text.split('\n'); const int start = qMax(1, args.value("start_line").toInt(1));
    const int end = qMin(int(lines.size()), qMin(start + 499, args.value("end_line").toInt(start + 499)));
    QStringList out; for (int i = start; i <= end; ++i) out.append(QString::number(i) + "\t" + lines[i - 1]);
    if (end < lines.size()) out.append("[more lines available]");
    *summary = "Read " + args.value("path").toString(); return out.join('\n').left(64000);
}
QString CodingAgent::execSearchFiles(const QJsonObject &args, QString *summary) const
{
    const QString path = resolveWorkspacePath(m_workspace, args.value("path").toString("."));
    const QString pattern = args.value("pattern").toString();
    if (path.isEmpty() || pattern.isEmpty()) return "Error: invalid search path or empty pattern.";
    // Literal search avoids untrusted regular expressions blocking the GUI thread.
    QStringList out; int visited = 0; QElapsedTimer clock; clock.start();
    QStringList dirs{path};
    while (!dirs.isEmpty() && visited < 2000 && clock.elapsed() < 400 && out.size() < 100) {
        const QString dir = dirs.takeLast();
        for (const auto &f : QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
            if (++visited > 2000 || clock.elapsed() >= 400 || out.size() >= 100) break;
            const QString rel = QDir(m_workspace).relativeFilePath(f.filePath());
            if (f.isSymLink() || privatePath(rel) || rel.startsWith("build/")) continue;
            if (f.isDir()) { dirs.append(f.filePath()); continue; }
            bool ok; const auto text = readText(f.filePath(), &ok); if (!ok) continue;
            const auto lines = text.split('\n');
            for (int i = 0; i < lines.size() && out.size() < 100; ++i) if (lines[i].contains(pattern, Qt::CaseInsensitive)) out.append(rel + ":" + QString::number(i + 1) + ": " + lines[i].left(300));
        }
    }
    *summary = "Search " + pattern; out.append("[bounded search: up to 2000 entries, 100 matches, 1.5 seconds]"); return out.join('\n');
}
void CodingAgent::prepareProposeEdit(ToolCall &tc, const QJsonObject &args) const
{
    tc.filePath = args.value("path").toString(); const QString path = resolveWorkspacePath(m_workspace, tc.filePath);
    auto fail = [&tc](const QString &s) { tc.status = "error"; tc.output = "Error: " + s; };
    if (path.isEmpty() || privatePath(QDir(m_workspace).relativeFilePath(path))) { fail("restricted path"); return; }
    bool ok = true; QString old;
    if (QFileInfo::exists(path)) { old = readText(path, &ok); if (!ok) { fail("file unreadable, binary or too large"); return; } }
    tc.proposedContent = args.value("new_text").toString();
    if (!args.contains("new_text")) { fail("new_text is required"); return; }
    if (args.contains("old_text")) {
        const QString needle = args.value("old_text").toString();
        if (needle.isEmpty() || old.count(needle) != 1) { fail("old_text must match exactly once"); return; }
        tc.proposedContent = old; tc.proposedContent.replace(needle, args.value("new_text").toString());
    }
    if (tc.proposedContent.toUtf8().size() > FileLimit) { fail("proposed file too large"); return; }
    tc.originalHash = QCryptographicHash::hash(old.toUtf8(), QCryptographicHash::Sha256).toHex();
    tc.originalExists = QFileInfo::exists(path);
    const auto diff = computeUnifiedDiff(tc.filePath, old, tc.proposedContent);
    tc.diffText = diff.unified; tc.addedLines = diff.added; tc.removedLines = diff.removed; tc.summary = args.value("description").toString();
}
QString CodingAgent::applyProposedEdit(ToolCall &tc)
{
    const QString path = resolveWorkspacePath(m_workspace, tc.filePath);
    if (path.isEmpty() || privatePath(QDir(m_workspace).relativeFilePath(path))) return "Error: restricted path.";
    bool ok = true; const bool exists = QFileInfo::exists(path); const QString old = exists ? readText(path, &ok) : QString();
    if (!ok || exists != tc.originalExists || QCryptographicHash::hash(old.toUtf8(), QCryptographicHash::Sha256).toHex() != tc.originalHash) return "Error: file changed since proposal; request a fresh edit.";
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return "Error: cannot create parent folder.";
    QSaveFile f(path); const QByteArray bytes = tc.proposedContent.toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) return "Error: cannot save file.";
    emit workspaceChanged(); return "Applied " + tc.filePath;
}
void CodingAgent::approveToolCall(const QString &msg, const QString &id)
{
    if (busy() || m_mode == "plan") return;
    auto *s = findSession(m_activeSessionId); auto *tc = toolIn(s, msg, id);
    if (!tc || tc->status != "awaiting_approval") return;
    if (tc->name == "run_command") { tc->status = "running"; startShellCommand(msg, id, tc->command); return; }
    if (tc->name != "propose_edit") return;
    const QString result = applyProposedEdit(*tc); tc->status = result.startsWith("Error:") ? "error" : "applied"; tc->output = result;
    appendToolResultMessage(*s, id, "propose_edit", result); save(); emit messagesChanged();
    resumeAfterToolCompletion();
}
void CodingAgent::rejectToolCall(const QString &msg, const QString &id)
{
    if (busy()) return;
    auto *s = findSession(m_activeSessionId); auto *tc = toolIn(s, msg, id); if (!tc || tc->status != "awaiting_approval") return;
    tc->status = "rejected"; const QString name = tc->name;
    appendToolResultMessage(*s, id, name, "User rejected this operation. Do not retry without asking."); save(); emit messagesChanged(); resumeAfterToolCompletion();
}
void CodingAgent::approveAllPending()
{
    if (busy() || m_mode != "auto") return;
    auto *s = findSession(m_activeSessionId); if (!s) return;
    QList<QPair<QString, QString>> pending;
    for (const auto &m : s->messages) for (const auto &tc : m.toolCalls) if (tc.status == "awaiting_approval") pending.append({m.id, tc.id});
    for (const auto &pair : pending) { if (busy()) break; approveToolCall(pair.first, pair.second); }
    if (!busy()) resumeAfterToolCompletion();
}
int CodingAgent::boostedMaxTokens(const ProviderConfig &p, int level) const
{
    const qint64 v = qint64(p.maxTokens) << qBound(0, level, 2);
    return int(qMin<qint64>(v, 65536));
}
void CodingAgent::scheduleSoon(const QString &status, int delayMs)
{
    --m_turnDepth; // the cut or refused try does not count as a round of tool use
    m_retryStatus = status;
    m_streamContent.clear(); m_streamReasoning.clear(); m_chunkTimer.stop();
    m_retryTimer.start(delayMs);
    emit streamChunkChanged(); emit streamingChanged(); emit retryChanged(); emit busyChanged();
}
void CodingAgent::clearRetry()
{
    const bool had = m_retryTimer.isActive() || !m_retryStatus.isEmpty() || m_retryCount > 0;
    m_retryTimer.stop(); m_retryCount = 0; m_retryStatus.clear();
    if (had) { emit retryChanged(); emit busyChanged(); }
}
void CodingAgent::messagesSoon() { if (!m_messagesTimer.isActive()) m_messagesTimer.start(); }
void CodingAgent::startShellCommand(const QString &msg, const QString &id, const QString &command, bool userInitiated)
{
    m_proc = new QProcess(this); m_procMsgId = msg; m_procToolCallId = id;
    m_proc->setWorkingDirectory(m_workspace); m_proc->setProcessChannelMode(QProcess::MergedChannels);
    auto *timer = new QTimer(m_proc); timer->setSingleShot(true); timer->setInterval(120000);
    connect(timer, &QTimer::timeout, m_proc, [this] { if (m_proc) killCommand(m_proc); });
#ifdef Q_OS_UNIX
    m_proc->setChildProcessModifier([] { ::setsid(); });
#endif
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this, msg, id] {
        const auto bytes = m_proc->readAllStandardOutput();
        if (auto *tc = toolIn(findSession(m_activeSessionId), msg, id)) {
            tc->output += QString::fromUtf8(bytes).left(qMax(0, 64000 - int(tc->output.size()))); messagesSoon();
        }
    });
    auto finish = [this, msg, id, userInitiated, command](int code) {
        if (!m_proc) return;
        auto *proc = m_proc; m_proc = nullptr; proc->disconnect(this); proc->deleteLater();
        auto *s = findSession(m_activeSessionId); if (auto *tc = toolIn(s, msg, id)) {
            tc->status = code == 0 ? "completed" : "error";
            tc->output += "\n[exit " + QString::number(code) + "]";
            if (!userInitiated) { const QString result = tc->output; appendToolResultMessage(*s, id, "run_command", result); }
        }
        m_messagesTimer.stop(); // the final state goes out now, so a late tick would only redraw the same thing
        save(); emit messagesChanged(); emit busyChanged();
        if (userInitiated) { emit shellFinished(command, code); return; } // the person's own command: no model turn follows
        if (m_mode == "auto" && hasPendingApprovals()) approveAllPending(); else resumeAfterToolCompletion();
    };
    connect(m_proc, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) { finish(status == QProcess::NormalExit ? code : -1); });
    connect(m_proc, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) finish(-1); });
#ifdef Q_OS_WIN
    m_proc->start("cmd.exe", {"/D", "/S", "/C", command});
#else
    m_proc->start("/bin/sh", {"-c", command});
#endif
    timer->start(); emit busyChanged(); emit messagesChanged();
}
void CodingAgent::stop()
{
    const bool wasWorking = m_reply || m_proc || m_compactReply || m_resumeQueued || m_retryTimer.isActive();
    m_messagesTimer.stop(); m_chunkTimer.stop();
    clearRetry();
    m_turnDepth = 0; ++m_epoch; m_resumeQueued = false;
    const QString partial = m_reply ? m_streamContent.trimmed() : QString(); // what the agent had written so far is kept, not thrown away
    if (m_reply) { auto *r = m_reply; m_reply = nullptr; r->disconnect(this); r->abort(); r->deleteLater(); }
    if (m_testReply) { auto *r = m_testReply; m_testReply = nullptr; r->disconnect(this); r->abort(); r->deleteLater(); }
    if (m_compactReply) { auto *r = m_compactReply; m_compactReply = nullptr; r->disconnect(this); r->abort(); r->deleteLater(); }
    if (m_proc) { auto *p = m_proc; m_proc = nullptr; p->disconnect(this); killCommand(p); p->waitForFinished(1000); p->deleteLater(); }
    if (auto *s = findSession(m_activeSessionId)) {
        QList<QPair<QString, QString>> interrupted;
        for (auto &m : s->messages) for (auto &tc : m.toolCalls)
            if (tc.status == "pending" || tc.status == "running" || tc.status == "awaiting_approval") {
                tc.status = "rejected";
                if (m.role == "shell") tc.output += "\n[stopped]"; // a command the person ran: nothing was asked of the agent, so no tool result
                else interrupted.append({tc.id, tc.name});
            }
        for (const auto &pair : interrupted) appendToolResultMessage(*s, pair.first, pair.second, "Stopped by user; operation may have partially completed.");
        if (!partial.isEmpty()) {
            ChatMessage m; m.id = newId(); m.role = "assistant"; m.content = partial; m.timestamp = QDateTime::currentMSecsSinceEpoch(); s->messages.append(m);
        }
        if (wasWorking || !interrupted.isEmpty()) { // a thin line in the chat, never sent to the model
            ChatMessage n; n.id = newId(); n.role = "note"; n.content = "Interrupted"; n.timestamp = QDateTime::currentMSecsSinceEpoch(); s->messages.append(n);
        }
    }
    m_streamContent.clear(); m_streamReasoning.clear(); save();
    emit busyChanged(); emit streamingChanged(); emit streamChunkChanged(); emit messagesChanged();
}
