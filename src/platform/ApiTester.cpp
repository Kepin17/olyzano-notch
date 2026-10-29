#include "ApiTester.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJSEngine>
#include <QJSValue>
#include <QJSValueIterator>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>

#include <QCoreApplication>
#include <QHostAddress>
#include <QTime>
#include <QTcpSocket>

#ifndef Q_OS_WIN
#include <csignal>
#include <cerrno>
#include <cstring>
#endif

#include <functional>

namespace {

QVector<ApiTesterModel::KeyValue> kvFromVariant(const QVariant &v)
{
    QVector<ApiTesterModel::KeyValue> out;
    for (const QVariant &x : v.toList()) {
        const QVariantMap m = x.toMap();
        out << ApiTesterModel::KeyValue{m.value(QStringLiteral("key")).toString(), m.value(QStringLiteral("value")).toString(),
                                        m.value(QStringLiteral("enabled"), true).toBool(), m.value(QStringLiteral("secret")).toBool()};
    }
    return out;
}

QVariantList kvToVariant(const QVector<ApiTesterModel::KeyValue> &list)
{
    QVariantList out;
    for (const auto &kv : list)
        out << QVariantMap{{QStringLiteral("key"), kv.key}, {QStringLiteral("value"), kv.value}, {QStringLiteral("enabled"), kv.enabled},
                           {QStringLiteral("secret"), kv.secret}};
    return out;
}

QJsonObject kvVarsToObject(const QVector<ApiTesterModel::KeyValue> &vars)
{
    QJsonObject o;
    for (const auto &kv : vars)
        if (kv.enabled)
            o.insert(kv.key, kv.value);
    return o;
}

} // namespace

ApiTester::ApiTester(QObject *parent)
    : QObject(parent)
    , m_settings(configPath(), QSettings::IniFormat)
{
    load();
    // Nothing is made up: a new install has no collections and no environments; they are the person's own.
    if (m_migrated)
        saveCollections(); // the templates older versions saved are gone from disk too
    bool activeKnown = false;
    for (const auto &e : std::as_const(m_environments))
        activeKnown = activeKnown || e.id == m_activeEnvId;
    if (!activeKnown)
        m_activeEnvId = m_environments.isEmpty() ? QString() : m_environments.constFirst().id;
}

QString ApiTester::dataDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/notch/apitester");
    QDir().mkpath(dir);
    return dir;
}

QString ApiTester::configPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/apitester.conf");
}

void ApiTester::load()
{
    {
        QFile f(dataDir() + QStringLiteral("/collections.json"));
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = f.readAll();
            m_collections = ApiTesterModel::collectionsFromJson(bytes);
            m_migrated = bytes.contains("\"readonly\": true");
        }
    }
    {
        QFile f(dataDir() + QStringLiteral("/environments.json"));
        if (f.open(QIODevice::ReadOnly)) {
            QString active;
            m_environments = ApiTesterModel::environmentsFromJson(f.readAll(), &active);
            m_activeEnvId = active;
        }
    }
    {
        QFile f(dataDir() + QStringLiteral("/history.json"));
        if (f.open(QIODevice::ReadOnly))
            m_history = ApiTesterModel::historyFromJson(f.readAll());
    }
    // watched ports: "8080,3000" in conf — last probe state starts unknown
    for (const QString &s : m_settings.value(QStringLiteral("ports")).toStringList()) {
        bool ok = false;
        const int p = s.toInt(&ok);
        if (ok && p > 0 && p < 65536)
            m_ports << PortEntry{p, false, -1, {}, {}, {}};
    }
}

void ApiTester::saveCollections() const
{
    QSaveFile f(dataDir() + QStringLiteral("/collections.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(ApiTesterModel::collectionsToJson(m_collections));
        f.commit();
    }
}

void ApiTester::saveEnvironments() const
{
    QSaveFile f(dataDir() + QStringLiteral("/environments.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(ApiTesterModel::environmentsToJson(m_environments, m_activeEnvId));
        f.commit();
    }
}

void ApiTester::saveHistory() const
{
    QSaveFile f(dataDir() + QStringLiteral("/history.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(ApiTesterModel::historyToJson(m_history));
        f.commit();
    }
}

void ApiTester::setActiveEnvId(const QString &id)
{
    if (m_activeEnvId == id)
        return;
    m_activeEnvId = id;
    saveEnvironments();
    emit changed();
}

void ApiTester::setHistoryEnabled(bool on)
{
    m_settings.setValue(QStringLiteral("historyEnabled"), on);
    emit settingsChanged();
}
void ApiTester::setHistoryLimit(int n)
{
    m_settings.setValue(QStringLiteral("historyLimit"), qBound(5, n, 500));
    emit settingsChanged();
}
void ApiTester::setPrettyJson(bool on)
{
    m_settings.setValue(QStringLiteral("prettyJson"), on);
    emit settingsChanged();
}
void ApiTester::setFollowRedirects(bool on)
{
    m_settings.setValue(QStringLiteral("followRedirects"), on);
    emit settingsChanged();
}
void ApiTester::setTimeoutMs(int ms)
{
    m_settings.setValue(QStringLiteral("timeoutMs"), qBound(1000, ms, 300000));
    emit settingsChanged();
}

QVector<ApiTesterModel::KeyValue> ApiTester::activeVars() const
{
    for (const auto &e : m_environments)
        if (e.id == m_activeEnvId)
            return e.variables;
    return {};
}

// `parentId`: what holds this node: its collection, or the folder it sits in.
void ApiTester::flatten(const ApiTesterModel::RequestNode &n, const QString &parentId, int depth, QList<FlatRow> &out) const
{
    FlatRow row;
    row.id = ApiTesterModel::nodeId(n);
    row.parentId = parentId;
    row.name = n.type == QLatin1String("folder") ? n.name : n.request.name;
    row.kind = n.type;
    row.depth = depth;
    if (n.type == QLatin1String("request"))
        row.method = n.request.method;
    out << row;
    if (n.type == QLatin1String("folder"))
        for (const auto &c : n.items)
            flatten(c, row.id, depth + 1, out);
}

QVariantList ApiTester::collections() const
{
    QList<FlatRow> rows;
    for (const auto &c : m_collections) {
        FlatRow head;
        head.id = c.id;
        head.name = c.name;
        head.kind = QStringLiteral("collection");
        rows << head;
        for (const auto &n : c.items)
            flatten(n, c.id, 1, rows);
    }
    QVariantList out;
    for (const auto &r : rows)
        out << QVariantMap{{QStringLiteral("id"), r.id},     {QStringLiteral("name"), r.name},
                           {QStringLiteral("kind"), r.kind}, {QStringLiteral("method"), r.method},
                           {QStringLiteral("depth"), r.depth}, {QStringLiteral("parentId"), r.parentId}};
    return out;
}

QVariantList ApiTester::environments() const
{
    QVariantList out;
    for (const auto &e : m_environments)
        out << QVariantMap{{QStringLiteral("id"), e.id}, {QStringLiteral("name"), e.name}};
    return out;
}

QVariantList ApiTester::history() const
{
    QVariantList out;
    for (const auto &h : m_history) {
        int pass = 0, total = 0;
        for (const auto &t : h.tests) {
            ++total;
            if (t.passed)
                ++pass;
        }
        out << QVariantMap{{QStringLiteral("id"), h.id},       {QStringLiteral("at"), h.atIso},
                           {QStringLiteral("method"), h.method}, {QStringLiteral("url"), h.url},
                           {QStringLiteral("status"), h.status}, {QStringLiteral("durationMs"), h.durationMs},
                           {QStringLiteral("error"), h.error},   {QStringLiteral("testsPassed"), pass},
                           {QStringLiteral("testsTotal"), total}};
    }
    return out;
}

ApiTesterModel::RequestNode *ApiTester::findNode(const QString &id, ApiTesterModel::Collection **coll)
{
    for (auto &c : m_collections) {
        if (c.id == id) {
            if (coll)
                *coll = &c;
            return nullptr; // collection itself, not a node
        }
        std::function<ApiTesterModel::RequestNode *(ApiTesterModel::RequestNode &)> walk = [&](ApiTesterModel::RequestNode &n) -> ApiTesterModel::RequestNode * {
            if ((n.type == QLatin1String("folder") ? n.id : n.request.id) == id) {
                if (coll)
                    *coll = &c;
                return &n;
            }
            if (n.type == QLatin1String("folder"))
                for (auto &ch : n.items)
                    if (auto *hit = walk(ch))
                        return hit;
            return nullptr;
        };
        for (auto &n : c.items)
            if (auto *hit = walk(n))
                return hit;
    }
    return nullptr;
}

const ApiTesterModel::RequestNode *ApiTester::findNode(const QString &id) const
{
    return const_cast<ApiTester *>(this)->findNode(id);
}

QVariantMap ApiTester::specToMap(const ApiTesterModel::RequestSpec &r) const
{
    return QVariantMap{{QStringLiteral("id"), r.id},
                       {QStringLiteral("name"), r.name},
                       {QStringLiteral("method"), r.method},
                       {QStringLiteral("url"), r.url},
                       {QStringLiteral("headers"), kvToVariant(r.headers)},
                       {QStringLiteral("query"), kvToVariant(r.query)},
                       {QStringLiteral("bodyMode"), r.bodyMode},
                       {QStringLiteral("body"), r.body},
                       {QStringLiteral("authType"), r.auth.type},
                       {QStringLiteral("authToken"), r.auth.token},
                       {QStringLiteral("authUser"), r.auth.user},
                       {QStringLiteral("authPassword"), r.auth.password},
                       {QStringLiteral("preScript"), r.preScript},
                       {QStringLiteral("testScript"), r.testScript},
                       {QStringLiteral("description"), r.description}};
}

QVariantMap ApiTester::requestById(const QString &id) const
{
    const auto *n = findNode(id);
    if (!n || n->type != QLatin1String("request"))
        return {};
    return specToMap(n->request);
}

void ApiTester::applyDraftTo(ApiTesterModel::RequestSpec &r, const QVariantMap &draft) const
{
    if (draft.contains(QStringLiteral("method")))
        r.method = draft.value(QStringLiteral("method")).toString();
    if (draft.contains(QStringLiteral("url")))
        r.url = draft.value(QStringLiteral("url")).toString();
    if (draft.contains(QStringLiteral("headers")))
        r.headers = kvFromVariant(draft.value(QStringLiteral("headers")));
    if (draft.contains(QStringLiteral("query")))
        r.query = kvFromVariant(draft.value(QStringLiteral("query")));
    if (draft.contains(QStringLiteral("bodyMode")))
        r.bodyMode = draft.value(QStringLiteral("bodyMode")).toString();
    if (draft.contains(QStringLiteral("body")))
        r.body = draft.value(QStringLiteral("body")).toString();
    if (draft.contains(QStringLiteral("preScript")))
        r.preScript = draft.value(QStringLiteral("preScript")).toString();
    if (draft.contains(QStringLiteral("testScript")))
        r.testScript = draft.value(QStringLiteral("testScript")).toString();
    r.auth.type = draft.value(QStringLiteral("authType"), r.auth.type).toString();
    r.auth.token = draft.value(QStringLiteral("authToken"), r.auth.token).toString();
    r.auth.user = draft.value(QStringLiteral("authUser"), r.auth.user).toString();
    r.auth.password = draft.value(QStringLiteral("authPassword"), r.auth.password).toString();
}

QString ApiTester::newCollection(const QString &name)
{
    ApiTesterModel::Collection c;
    c.id = ApiTesterModel::newId();
    c.name = name.trimmed().isEmpty() ? QStringLiteral("New collection") : name.trimmed();
    m_collections << c;
    saveCollections();
    emit changed();
    return c.id;
}

// `parentId` is a collection or a folder of one; the node lands at its end.
QString ApiTester::addRequest(const QString &parentId, const QString &folderId)
{
    const QString id = ApiTesterModel::addRequestNode(m_collections, folderId.isEmpty() ? parentId : folderId);
    if (!id.isEmpty())
        commit();
    return id;
}

QString ApiTester::addFolder(const QString &parentId, const QString &name)
{
    const QString id = ApiTesterModel::addFolder(m_collections, parentId, name);
    if (!id.isEmpty())
        commit();
    return id;
}

QString ApiTester::duplicateNode(const QString &id)
{
    const QString copy = ApiTesterModel::duplicateNode(m_collections, id);
    if (!copy.isEmpty())
        commit();
    return copy;
}

bool ApiTester::moveNode(const QString &id, const QString &parentId, int index)
{
    const bool ok = ApiTesterModel::moveNode(m_collections, id, parentId, index);
    if (ok)
        commit();
    return ok;
}

// A request the person built in the builder (or took from the history) kept in a collection or folder. -> the new request's id
QString ApiTester::saveRequestAs(const QVariantMap &draft, const QString &parentId, const QString &name)
{
    const QString id = ApiTesterModel::addRequestNode(m_collections, parentId, name);
    if (id.isEmpty())
        return {};
    if (auto *n = findNode(id)) {
        applyDraftTo(n->request, draft);
        n->request.name = n->name;
    }
    commit();
    return id;
}

void ApiTester::updateRequest(const QString &id, const QVariantMap &fields)
{
    auto *n = findNode(id);
    if (!n || n->type != QLatin1String("request"))
        return;
    applyDraftTo(n->request, fields);
    if (fields.contains(QStringLiteral("name")) && !fields.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
        n->request.name = fields.value(QStringLiteral("name")).toString().trimmed();
        n->name = n->request.name;
    }
    commit();
}

void ApiTester::renameNode(const QString &id, const QString &name)
{
    if (ApiTesterModel::renameNode(m_collections, id, name))
        commit();
}

void ApiTester::removeNode(const QString &id)
{
    if (ApiTesterModel::removeNode(m_collections, id))
        commit();
}

void ApiTester::commit()
{
    saveCollections();
    emit changed();
}

QString ApiTester::newEnvironment(const QString &name)
{
    ApiTesterModel::Environment e;
    e.id = ApiTesterModel::newId();
    e.name = name.trimmed().isEmpty() ? QStringLiteral("New environment") : name.trimmed();
    m_environments << e;
    m_activeEnvId = e.id;
    saveEnvironments();
    emit changed();
    return e.id;
}

void ApiTester::updateEnvironment(const QString &id, const QVariantMap &fields)
{
    for (auto &e : m_environments) {
        if (e.id != id)
            continue;
        if (fields.contains(QStringLiteral("name")))
            e.name = fields.value(QStringLiteral("name")).toString();
        if (fields.contains(QStringLiteral("variables")))
            e.variables = kvFromVariant(fields.value(QStringLiteral("variables")));
        saveEnvironments();
        emit changed();
        return;
    }
}

void ApiTester::removeEnvironment(const QString &id)
{
    for (int i = 0; i < m_environments.size(); ++i)
        if (m_environments[i].id == id) {
            m_environments.removeAt(i);
            if (m_activeEnvId == id)
                m_activeEnvId = m_environments.isEmpty() ? QString() : m_environments.constFirst().id;
            saveEnvironments();
            emit changed();
            return;
        }
}

QString ApiTester::duplicateEnvironment(const QString &id)
{
    for (int i = 0; i < m_environments.size(); ++i)
        if (m_environments[i].id == id) {
            ApiTesterModel::Environment e = m_environments[i];
            e.id = ApiTesterModel::newId();
            e.name += QStringLiteral(" copy");
            m_environments.insert(i + 1, e);
            saveEnvironments();
            emit changed();
            return e.id;
        }
    return {};
}

QVariantMap ApiTester::environmentById(const QString &id) const
{
    for (const auto &e : m_environments)
        if (e.id == id)
            return QVariantMap{{QStringLiteral("id"), e.id},
                               {QStringLiteral("name"), e.name},
                               {QStringLiteral("variables"), kvToVariant(e.variables)}};
    return {};
}

void ApiTester::clearHistory()
{
    m_history.clear();
    saveHistory();
    emit changed();
}

void ApiTester::removeHistory(const QString &id)
{
    for (int i = 0; i < m_history.size(); ++i)
        if (m_history[i].id == id) {
            m_history.removeAt(i);
            saveHistory();
            emit changed();
            return;
        }
}

// ---- local ports (watch / probe / stop / run a stop command) ----

void ApiTester::savePorts() const
{
    QStringList list;
    for (const auto &p : m_ports)
        list << QString::number(p.port);
    m_settings.setValue(QStringLiteral("ports"), list);
}

QVariantList ApiTester::ports() const
{
    QVariantList out;
    for (const auto &p : m_ports)
        out << QVariantMap{{QStringLiteral("port"), p.port},
                           {QStringLiteral("open"), p.open},
                           {QStringLiteral("pid"), p.pid},
                           {QStringLiteral("process"), p.process},
                           {QStringLiteral("checkedAt"), p.checkedAt},
                           {QStringLiteral("error"), p.error}};
    return out;
}

void ApiTester::addPort(int port)
{
    if (port <= 0 || port >= 65536)
        return;
    for (const auto &p : m_ports) {
        if (p.port == port) {
            checkPort(port);
            return;
        }
    }
    m_ports << PortEntry{port, false, -1, {}, {}, {}};
    savePorts();
    emit portsChanged();
    checkPort(port);
}

void ApiTester::removePort(int port)
{
    for (int i = 0; i < m_ports.size(); ++i)
        if (m_ports[i].port == port) {
            m_ports.removeAt(i);
            savePorts();
            emit portsChanged();
            return;
        }
}

int ApiTester::findListenerPid(int port, QString *process) const
{
    // ss (Linux) or netstat (Windows): one short helper beats parsing /proc by hand.
    QProcess proc;
#ifdef Q_OS_WIN
    proc.start(QStringLiteral("netstat"), {QStringLiteral("-ano")});
#else
    proc.start(QStringLiteral("ss"), {QStringLiteral("-lptnH")});
#endif
    if (!proc.waitForFinished(1500))
        return -1;
    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    const QString needle = QStringLiteral(":%1 ").arg(port);
    const QString needle6 = QStringLiteral("]:%1 ").arg(port);
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        if (!line.contains(needle) && !line.contains(QStringLiteral(":%1").arg(port)) && !line.contains(needle6))
            continue;
#ifdef Q_OS_WIN
        if (!line.contains(QLatin1String("LISTENING")) && !line.contains(QLatin1String("LISTEN")))
            continue;
        const QStringList cols = line.simplified().split(QLatin1Char(' '));
        if (cols.isEmpty())
            continue;
        bool ok = false;
        const int pid = cols.last().toInt(&ok);
        if (!ok || pid <= 0)
            continue;
        if (process) {
            QProcess tl;
            tl.start(QStringLiteral("tasklist"),
                     {QStringLiteral("/FI"), QStringLiteral("PID eq %1").arg(pid), QStringLiteral("/FO"), QStringLiteral("CSV"), QStringLiteral("/NH")});
            if (tl.waitForFinished(1000)) {
                const QString s = QString::fromLocal8Bit(tl.readAllStandardOutput());
                const int a = s.indexOf(QLatin1Char('"'));
                const int b = s.indexOf(QLatin1Char('"'), a + 1);
                if (a >= 0 && b > a)
                    *process = s.mid(a + 1, b - a - 1);
            }
        }
        return pid;
#else
        // users:(("node",pid=123,fd=20)) — exact :port to avoid :8080 matching :80801
        if (!line.contains(needle) && !line.contains(QStringLiteral("]:%1 ").arg(port)) && !line.contains(QStringLiteral(":%1 ").arg(port)))
            continue;
        const int p = line.indexOf(QStringLiteral("pid="));
        if (p < 0)
            continue;
        int end = p + 4;
        while (end < line.size() && line[end].isDigit())
            ++end;
        const int pid = line.mid(p + 4, end - (p + 4)).toInt();
        if (pid <= 0)
            continue;
        if (process) {
            const int n0 = line.indexOf(QLatin1Char('"'));
            const int n1 = line.indexOf(QLatin1Char('"'), n0 + 1);
            if (n0 >= 0 && n1 > n0)
                *process = line.mid(n0 + 1, n1 - n0 - 1);
        }
        return pid;
#endif
    }
    return -1;
}

void ApiTester::checkPort(int port)
{
    PortEntry *entry = nullptr;
    for (auto &p : m_ports)
        if (p.port == port) {
            entry = &p;
            break;
        }
    if (!entry) {
        m_ports << PortEntry{port, false, -1, {}, {}, {}};
        entry = &m_ports.last();
    }

    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, ushort(port));
    const bool open = sock.waitForConnected(400);
    entry->open = open;
    entry->error.clear();
    entry->checkedAt = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    entry->pid = -1;
    entry->process.clear();
    if (open) {
        QString name;
        entry->pid = findListenerPid(port, &name);
        entry->process = name;
        sock.abort();
    } else {
        entry->error = QStringLiteral("no listener");
    }

    savePorts();
    emit portsChanged();
    emit portChecked(QVariantMap{{QStringLiteral("port"), port},
                                 {QStringLiteral("open"), entry->open},
                                 {QStringLiteral("pid"), entry->pid},
                                 {QStringLiteral("process"), entry->process},
                                 {QStringLiteral("error"), entry->error},
                                 {QStringLiteral("checkedAt"), entry->checkedAt}});
}

void ApiTester::checkAllPorts()
{
    const QList<PortEntry> snapshot = m_ports;
    for (const auto &p : snapshot)
        checkPort(p.port);
}

QString ApiTester::stopService(int port)
{
    QString process;
    const int pid = findListenerPid(port, &process);
    if (pid <= 0) {
        const auto text = QStringLiteral("Nothing listening on :%1").arg(port);
        emit statusMessage(text);
        checkPort(port);
        return text;
    }
    QString text;
#ifdef Q_OS_WIN
    QProcess p;
    p.start(QStringLiteral("taskkill"), {QStringLiteral("/PID"), QString::number(pid)});
    const bool ok = p.waitForFinished(4000) && p.exitCode() == 0;
    text = ok ? QStringLiteral("Stopped %1 (pid %2) on :%3").arg(process.isEmpty() ? QStringLiteral("process") : process).arg(pid).arg(port)
              : QStringLiteral("taskkill failed for pid %1: %2").arg(pid).arg(QString::fromLocal8Bit(p.readAllStandardError()).trimmed());
#else
    const bool ok = ::kill(pid, SIGTERM) == 0;
    text = ok ? QStringLiteral("Sent SIGTERM to %1 (pid %2) on :%3").arg(process.isEmpty() ? QStringLiteral("process") : process).arg(pid).arg(port)
              : QStringLiteral("kill(%1) failed: %2").arg(pid).arg(QString::fromLocal8Bit(strerror(errno)));
#endif
    emit statusMessage(text);
    QTimer::singleShot(400, this, [this, port] { checkPort(port); });
    return text;
}

void ApiTester::runServiceCommand(const QString &cmd)
{
    if (m_cmdRunning || cmd.trimmed().isEmpty())
        return;
    if (m_cmdProc) {
        m_cmdProc->disconnect(this);
        m_cmdProc->deleteLater();
        m_cmdProc = nullptr;
    }
    m_cmdProc = new QProcess(this);
    m_cmdRunning = true;
    emit commandRunningChanged();

    auto finish = [this, cmd](int code, bool normalExit, const QString &tail) {
        const bool ok = normalExit && code == 0;
        if (m_cmdProc) {
            m_cmdProc->deleteLater();
            m_cmdProc = nullptr;
        }
        m_cmdRunning = false;
        emit commandRunningChanged();
        emit serviceCommandFinished(QVariantMap{{QStringLiteral("ok"), ok},
                                               {QStringLiteral("output"), tail},
                                               {QStringLiteral("exitCode"), code},
                                               {QStringLiteral("command"), cmd}});
        emit statusMessage(ok ? QStringLiteral("API — command finished") : QStringLiteral("API — command failed (") + QString::number(code) + QStringLiteral(")"));
    };

    connect(m_cmdProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, finish](int code, QProcess::ExitStatus st) {
                const QString all =
                    (QString::fromLocal8Bit(m_cmdProc ? m_cmdProc->readAllStandardOutput() : QByteArray())
                     + QString::fromLocal8Bit(m_cmdProc ? m_cmdProc->readAllStandardError() : QByteArray()))
                        .trimmed();
                finish(code, st == QProcess::NormalExit, all);
            });
    connect(m_cmdProc, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            finish(-1, false, QStringLiteral("Failed to start command"));
    });

#ifdef Q_OS_WIN
    m_cmdProc->start(QStringLiteral("cmd"), {QStringLiteral("/c"), cmd});
#else
    m_cmdProc->start(QStringLiteral("sh"), {QStringLiteral("-c"), cmd});
#endif
}

QByteArray ApiTester::buildBody(const ApiTesterModel::RequestSpec &r, QString *contentType) const
{
    if (r.bodyMode == QLatin1String("json")) {
        *contentType = QStringLiteral("application/json");
        return r.body.toUtf8();
    }
    if (r.bodyMode == QLatin1String("form")) {
        *contentType = QStringLiteral("application/x-www-form-urlencoded");
        return r.body.toUtf8();
    }
    if (r.bodyMode == QLatin1String("raw")) {
        *contentType = QStringLiteral("text/plain; charset=utf-8");
        return r.body.toUtf8();
    }
    contentType->clear();
    return {};
}

void ApiTester::send(const QVariantMap &draft)
{
    if (m_busy)
        return;
    ApiTesterModel::RequestSpec r;
    applyDraftTo(r, draft);
    const auto vars0 = activeVars();

    // 1) substitute, 2) preScript, 3) substitute again
    r.url = ApiTesterModel::substitute(r.url, vars0);
    r.headers = ApiTesterModel::substituteAll(r.headers, vars0);
    r.query = ApiTesterModel::substituteAll(r.query, vars0);
    r.body = ApiTesterModel::substitute(r.body, vars0);
    r.auth.token = ApiTesterModel::substitute(r.auth.token, vars0);
    r.auth.user = ApiTesterModel::substitute(r.auth.user, vars0);
    r.auth.password = ApiTesterModel::substitute(r.auth.password, vars0);

    m_scriptLog.clear();
    m_pendingTests.clear();
    m_pendingPreLog.clear();
    if (!r.preScript.trimmed().isEmpty()) {
        runScript(r.preScript, false, {});
        // env.set may have changed vars mid-send
        const auto vars1 = activeVars();
        r.url = ApiTesterModel::substitute(r.url, vars1);
        r.headers = ApiTesterModel::substituteAll(r.headers, vars1);
        r.query = ApiTesterModel::substituteAll(r.query, vars1);
        r.body = ApiTesterModel::substitute(r.body, vars1);
        r.auth.token = ApiTesterModel::substitute(r.auth.token, vars1);
        r.auth.user = ApiTesterModel::substitute(r.auth.user, vars1);
        r.auth.password = ApiTesterModel::substitute(r.auth.password, vars1);
    }
    m_pendingPreLog = m_scriptLog;
    m_scriptLog.clear();

    const QString finalUrl = ApiTesterModel::buildUrl(r.url, r.query);
    if (!finalUrl.startsWith(QStringLiteral("http://")) && !finalUrl.startsWith(QStringLiteral("https://"))) {
        m_last = QVariantMap{{QStringLiteral("status"), 0},
                             {QStringLiteral("error"), QStringLiteral("URL must start with http:// or https://")},
                             {QStringLiteral("body"), QString()},
                             {QStringLiteral("bodyPretty"), QString()},
                             {QStringLiteral("headers"), QVariantList()},
                             {QStringLiteral("durationMs"), 0},
                             {QStringLiteral("tests"), QVariantList()},
                             {QStringLiteral("scriptLog"), m_pendingPreLog}};
        emit responseChanged();
        return;
    }

    QNetworkRequest req{QUrl(finalUrl)};
    req.setTransferTimeout(timeoutMs());
    if (followRedirects())
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OlyzanoNotch/%1").arg(QStringLiteral(NOTCH_VERSION)));
    for (const auto &h : r.headers)
        if (h.enabled && !h.key.isEmpty())
            req.setRawHeader(h.key.toUtf8(), h.value.toUtf8());
    if (r.auth.type == QLatin1String("bearer") && !r.auth.token.isEmpty())
        req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(r.auth.token).toUtf8());
    else if (r.auth.type == QLatin1String("basic"))
        req.setRawHeader("Authorization", QStringLiteral("Basic %1")
                                                     .arg(QString::fromLatin1((r.auth.user + QLatin1Char(':') + r.auth.password).toUtf8().toBase64()))
                                                     .toUtf8());

    QString contentType;
    const QByteArray body = buildBody(r, &contentType);
    if (!contentType.isEmpty() && !req.hasRawHeader("Content-Type"))
        req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);

    m_inflight = r;
    m_inflight.url = finalUrl;
    m_busy = true;
    emit busyChanged();
    m_sendTimer.start();

    const QString method = r.method.isEmpty() ? QStringLiteral("GET") : r.method.toUpper();
    if (method == QLatin1String("GET"))
        m_reply = m_nam.get(req);
    else if (method == QLatin1String("DELETE"))
        m_reply = m_nam.deleteResource(req);
    else if (method == QLatin1String("HEAD"))
        m_reply = m_nam.head(req);
    else if (method == QLatin1String("POST"))
        m_reply = m_nam.post(req, body);
    else if (method == QLatin1String("PUT"))
        m_reply = m_nam.put(req, body);
    else
        m_reply = m_nam.sendCustomRequest(req, method.toLatin1(), body);

    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        if (!reply)
            return;
        reply->deleteLater();
        const qint64 ms = m_sendTimer.elapsed();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString statusText = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        QByteArray raw = reply->readAll();
        const bool truncated = raw.size() > 256 * 1024;
        if (truncated)
            raw = raw.left(256 * 1024);
        QString err;
        if (reply->error() != QNetworkReply::NoError && status == 0)
            err = reply->errorString();

        QString resHeaders;
        const auto list = reply->rawHeaderList();
        for (const QByteArray &k : list)
            resHeaders += QString::fromUtf8(k) + QStringLiteral(": ") + QString::fromUtf8(reply->rawHeader(k)) + QLatin1Char('\n');

        QString reqHeaders;
        for (const auto &h : m_inflight.headers)
            if (h.enabled && !h.key.isEmpty())
                reqHeaders += h.key + QStringLiteral(": ") + h.value + QLatin1Char('\n');
        if (m_inflight.auth.type == QLatin1String("bearer"))
            reqHeaders += QStringLiteral("Authorization: Bearer …\n");

        const QString bodyText = QString::fromUtf8(raw);
        const QString pretty = prettyJson() ? ApiTesterModel::prettyJson(raw) : bodyText;

        QVariantMap resp{{QStringLiteral("status"), status},
                         {QStringLiteral("statusText"), statusText},
                         {QStringLiteral("durationMs"), ms},
                         {QStringLiteral("body"), truncated ? bodyText.left(256 * 1024) + QStringLiteral("\n… truncated") : bodyText},
                         {QStringLiteral("bodyPretty"), pretty},
                         {QStringLiteral("bodyTruncated"), truncated},
                         {QStringLiteral("error"), err},
                         {QStringLiteral("url"), m_inflight.url},
                         {QStringLiteral("method"), m_inflight.method},
                         {QStringLiteral("contentType"), QString::fromUtf8(reply->header(QNetworkRequest::ContentTypeHeader).toByteArray())}};

        QVariantList hdrs;
        for (const QByteArray &k : list)
            hdrs << QVariantMap{{QStringLiteral("key"), QString::fromUtf8(k)}, {QStringLiteral("value"), QString::fromUtf8(reply->rawHeader(k))}};
        resp.insert(QStringLiteral("headers"), hdrs);

        // test script
        m_scriptLog = m_pendingPreLog;
        m_pendingTests.clear();
        if (!m_inflight.testScript.trimmed().isEmpty())
            runScript(m_inflight.testScript, true, resp);
        QVariantList tests;
        for (const auto &t : m_pendingTests)
            tests << QVariantMap{{QStringLiteral("name"), t.name}, {QStringLiteral("passed"), t.passed}, {QStringLiteral("message"), t.message}};
        resp.insert(QStringLiteral("tests"), tests);
        resp.insert(QStringLiteral("scriptLog"), m_scriptLog);

        m_last = resp;
        m_busy = false;
        emit busyChanged();
        emit responseChanged();

        if (historyEnabled()) {
            ApiTesterModel::HistoryEntry h;
            h.atIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            h.method = m_inflight.method;
            h.url = m_inflight.url;
            h.status = status;
            h.durationMs = ms;
            h.reqHeaders = reqHeaders;
            h.resHeaders = resHeaders;
            h.body = resp.value(QStringLiteral("bodyPretty")).toString();
            if (h.body.toUtf8().size() > 64 * 1024) {
                h.body = QString::fromUtf8(h.body.toUtf8().left(64 * 1024));
                h.bodyTruncated = true;
            }
            h.error = err;
            h.tests = m_pendingTests;
            ApiTesterModel::historyPush(m_history, std::move(h), historyLimit());
            saveHistory();
            emit changed();
        }
    });
}

void ApiTester::cancel()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
        m_busy = false;
        emit busyChanged();
        m_last = QVariantMap{{QStringLiteral("status"), 0}, {QStringLiteral("error"), QStringLiteral("Cancelled")}};
        emit responseChanged();
    }
}

void ApiTester::runScript(const QString &source, bool isTest, const QVariantMap &response)
{
    if (source.trimmed().isEmpty())
        return;
    QJSEngine eng;
    eng.globalObject().setProperty(QStringLiteral("__log__"), eng.newArray());
    eng.globalObject().setProperty(QStringLiteral("__tests__"), eng.newArray());

    // env + console as plain JS (no network / fs / Qt objects in the sandbox).
    const QString envSeed = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("v"), kvVarsToObject(activeVars())}}).toJson(QJsonDocument::Compact));
    const QString prelude =
        QStringLiteral(
            "var __v = %1;\n"
            "var env = {\n"
            "  get: function(k) { return Object.prototype.hasOwnProperty.call(__v, k) ? __v[k] : undefined; },\n"
            "  set: function(k, v) { __v[k] = String(v); },\n"
            "  unset: function(k) { delete __v[k]; }\n"
            "};\n"
            "var console = { log: function() {\n"
            "  var parts = []; for (var i = 0; i < arguments.length; ++i) parts.push(String(arguments[i]));\n"
            "  __log__.push(parts.join(' '));\n"
            "} };\n")
            .arg(envSeed);

    QJSValue pre = eng.evaluate(prelude, QStringLiteral("sandbox.js"));
    if (pre.isError()) {
        m_scriptLog << QStringLiteral("sandbox error: ") + pre.toString();
        return;
    }

    if (isTest) {
        const QJsonArray hlist = QJsonValue::fromVariant(response.value(QStringLiteral("headers"))).toArray();
        const QString bodyJson =
            QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("b"), QJsonValue::fromVariant(response.value(QStringLiteral("body")))}})
                                  .toJson(QJsonDocument::Compact));
        const QString hdrJson =
            QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("h"), QJsonValue::fromVariant(response.value(QStringLiteral("headers")))}})
                                  .toJson(QJsonDocument::Compact));
        const QString testPrelude =
            QStringLiteral(
                "var __b = %1;\n"
                "var __h = %2;\n"
                "var response = {\n"
                "  status: %3, time: %4,\n"
                "  get ok() { return this.status >= 200 && this.status < 300; },\n"
                "  headers: {\n"
                "    get: function(n) { n = String(n).toLowerCase(); for (var i = 0; i < __h.length; ++i) if (__h[i].key.toLowerCase() === n) return __h[i].value; return null; },\n"
                "    all: function() { return __h; }\n"
                "  },\n"
                "  text: function() { return __b; },\n"
                "  json: function() { return JSON.parse(__b); }\n"
                "};\n"
                "function test(name, fn) {\n"
                "  try { fn(); __tests__.push({ name: name, passed: true, message: '' }); }\n"
                "  catch (e) { __tests__.push({ name: name, passed: false, message: String(e) }); }\n"
                "}\n"
                "function expect(actual) {\n"
                "  var a = actual;\n"
                "  var api = {\n"
                "    toBe: function(v) { if (a !== v) throw new Error('expected ' + JSON.stringify(v) + ' got ' + JSON.stringify(a)); },\n"
                "    toBeTruthy: function() { if (!a) throw new Error('expected truthy'); },\n"
                "    toBeFalsy: function() { if (a) throw new Error('expected falsy'); },\n"
                "    toBeNull: function() { if (a !== null) throw new Error('expected null'); },\n"
                "    toBeDefined: function() { if (a === undefined) throw new Error('expected defined'); },\n"
                "    toBeUndefined: function() { if (a !== undefined) throw new Error('expected undefined'); },\n"
                "    toContain: function(s) { if (typeof a === 'string' ? a.indexOf(s) < 0 : !a || a.indexOf(s) < 0) throw new Error('expected to contain ' + s); },\n"
                "    toBeGreaterThan: function(n) { if (!(a > n)) throw new Error('expected > ' + n); },\n"
                "    toBeLessThan: function(n) { if (!(a < n)) throw new Error('expected < ' + n); },\n"
                "    toBeType: function(t) { var got = Array.isArray(a) ? 'array' : typeof a; if (got !== t) throw new Error('expected ' + t + ' got ' + got); }\n"
                "  };\n"
                "  api.not = { toBe: function(v) { if (a === v) throw new Error('expected not ' + JSON.stringify(v)); } };\n"
                "  return api;\n"
                "}\n")
                .arg(bodyJson, hdrJson)
                .arg(response.value(QStringLiteral("status")).toInt())
                .arg(response.value(QStringLiteral("durationMs")).toLongLong());
        QJSValue tp = eng.evaluate(testPrelude, QStringLiteral("test-prelude.js"));
        if (tp.isError()) {
            m_scriptLog << QStringLiteral("test prelude error: ") + tp.toString();
            return;
        }
    }

    QJSValue result = eng.evaluate(source, isTest ? QStringLiteral("test.js") : QStringLiteral("prerequest.js"));
    if (result.isError())
        m_scriptLog << QStringLiteral("error: ") + result.toString();

    QJSValue logs = eng.globalObject().property(QStringLiteral("__log__"));
    for (quint32 i = 0; i < logs.property(QStringLiteral("length")).toUInt(); ++i)
        m_scriptLog << logs.property(i).toString();

    if (isTest) {
        QJSValue arr = eng.globalObject().property(QStringLiteral("__tests__"));
        for (quint32 i = 0; i < arr.property(QStringLiteral("length")).toUInt(); ++i) {
            const QJSValue t = arr.property(i);
            ApiTesterModel::TestResult tr;
            tr.name = t.property(QStringLiteral("name")).toString();
            tr.passed = t.property(QStringLiteral("passed")).toBool();
            tr.message = t.property(QStringLiteral("message")).toString();
            m_pendingTests << tr;
        }
        return;
    }

    // pre-request: write env.set / env.unset back into the active environment
    QJSValue v = eng.globalObject().property(QStringLiteral("__v"));
    if (v.isObject()) {
        for (auto &e : m_environments) {
            if (e.id != m_activeEnvId)
                continue;
            QJsonObject kept = kvVarsToObject(e.variables);
            QStringList keys;
            {
                QJSValueIterator it(v);
                while (it.hasNext()) {
                    it.next();
                    keys << it.name();
                    kept.insert(it.name(), it.value().toVariant().toString());
                }
            }
            // honor unset: keys that existed before but are gone from __v
            const QJsonObject before = kvVarsToObject(e.variables);
            for (auto it = before.begin(); it != before.end(); ++it)
                if (!keys.contains(it.key()))
                    kept.remove(it.key());
            e.variables.clear();
            for (auto it = kept.begin(); it != kept.end(); ++it)
                e.variables << ApiTesterModel::KeyValue{it.key(), it.value().toString(), true};
            saveEnvironments();
            emit changed();
            break;
        }
    }
}

// ---- import / export ----

QString ApiTester::importPostmanCollection(const QString &json)
{
    bool ok = false;
    const auto c = ApiTesterModel::collectionFromPostman(json.toUtf8(), &ok);
    if (!ok)
        return QStringLiteral("Not a Postman collection (missing info/item)");
    m_collections << c;
    saveCollections();
    emit changed();
    const auto text = QStringLiteral("Imported “%1”").arg(c.name);
    emit statusMessage(text);
    return text;
}

QString ApiTester::exportPostmanCollection(const QString &id) const
{
    for (const auto &c : m_collections)
        if (c.id == id)
            return QString::fromUtf8(ApiTesterModel::collectionToPostman(c));
    return {};
}

QString ApiTester::importNativeCollection(const QString &json)
{
    const auto list = ApiTesterModel::collectionsFromJson(json.toUtf8());
    if (list.isEmpty())
        return QStringLiteral("No collections in file");
    m_collections << list;
    saveCollections();
    emit changed();
    const auto text = QStringLiteral("Imported %1 collection(s)").arg(list.size());
    emit statusMessage(text);
    return text;
}

QString ApiTester::exportNativeCollection(const QString &id) const
{
    for (const auto &c : m_collections)
        if (c.id == id)
            return QString::fromUtf8(ApiTesterModel::collectionsToJson({c}));
    return {};
}

QString ApiTester::importEnvironment(const QString &json)
{
    bool ok = false;
    const auto e = ApiTesterModel::environmentFromPostman(json.toUtf8(), &ok);
    if (!ok)
        return QStringLiteral("Not a Postman environment");
    m_environments << e;
    saveEnvironments();
    emit changed();
    const auto text = QStringLiteral("Imported environment “%1”").arg(e.name);
    emit statusMessage(text);
    return text;
}

QString ApiTester::exportEnvironment(const QString &id) const
{
    for (const auto &e : m_environments)
        if (e.id == id)
            return QString::fromUtf8(ApiTesterModel::environmentToPostman(e));
    return {};
}
