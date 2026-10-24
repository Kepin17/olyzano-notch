#include "ShareController.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSaveFile>
#include <QUuid>

#include "services/Invite.h"

ShareController::ShareController(NotchController *notch, QObject *parent)
    : QAbstractListModel(parent)
    , m_notch(notch)
    , m_settings(notch->settingsFolder() + QStringLiteral("/share.conf"), QSettings::IniFormat)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &ShareController::saveNow);
    QDir(m_notch->scratchPath(false) + QStringLiteral("/share")).removeRecursively(); // leftovers of a crash
    loadServers();
    restore();
    QTimer::singleShot(0, this, &ShareController::testAllServers); // so the limits of each server are known
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        saveNow(); // relay sessions stay on the server and come back next time; local ones end
        for (ShareSession *s : std::as_const(m_sessions))
            s->suspend();
    });
}

// ---- list model ------------------------------------------------------------------------------

QHash<int, QByteArray> ShareController::roleNames() const
{
    return {{IdRole, "sessionId"}, {NameRole, "name"}, {ModeRole, "mode"}, {StatusRole, "status"}, {UrlRole, "url"},
            {ProgressRole, "progress"}, {ExpiresAtRole, "expiresAt"}, {FileCountRole, "fileCount"},
            {DownloadsRole, "downloads"}, {MessageRole, "message"}, {EndReasonRole, "endReason"},
            {ProtectionRole, "protection"}, {ActiveRole, "active"}, {SingleUseRole, "singleUse"},
            {ServerNameRole, "serverName"}};
}

QVariant ShareController::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_sessions.size())
        return {};
    const ShareSession *s = m_sessions.at(index.row());
    switch (role) {
    case IdRole: return s->id();
    case NameRole: return s->name();
    case ModeRole: return s->mode();
    case StatusRole: return s->status();
    case UrlRole: return s->url();
    case ProgressRole: return s->progress();
    case ExpiresAtRole: return s->expiresAt();
    case FileCountRole: return s->fileCount();
    case DownloadsRole: return s->downloads();
    case MessageRole: return s->message();
    case EndReasonRole: return s->endReason();
    case ProtectionRole: return s->protection();
    case ActiveRole: return s->active();
    case SingleUseRole: return s->singleUse();
    case ServerNameRole: return s->mode() == QLatin1String("relay") ? s->serverName() : QString();
    default: return {};
    }
}

int ShareController::activeCount() const
{
    int n = 0;
    for (const ShareSession *s : m_sessions)
        n += s->active();
    return n;
}

int ShareController::rowOf(const QObject *session) const
{
    for (int i = 0; i < m_sessions.size(); ++i)
        if (m_sessions.at(i) == session)
            return i;
    return -1;
}

void ShareController::add(ShareSession *s)
{
    // Newest first, so the share just made is at the top of the list.
    beginInsertRows({}, 0, 0);
    m_sessions.prepend(s);
    endInsertRows();
    connect(s, &ShareSession::changed, this, [this, s] {
        if (const int row = rowOf(s); row >= 0)
            emit dataChanged(index(row), index(row));
        emit countChanged();
        scheduleSave();
    });
    emit countChanged();
}

// ---- running sessions ------------------------------------------------------------------------

QString ShareController::start(const QStringList &ids, const QString &mode, int ttlSeconds, const QString &protection,
                               const QString &secret, bool oneDownload, const QString &address, const QString &serverId)
{
    ShareSession::Options o;
    o.mode = mode;
    o.ttl = ttlSeconds;
    o.protection = protection;
    o.secret = secret;
    o.oneDownload = oneDownload;
    o.workRoot = m_notch->scratchPath(false);
    const auto sources = m_notch->shareSources(ids);
    for (const auto &s : sources)
        if (s.vault)
            o.workRoot = m_notch->scratchPath(true); // decrypted copies only ever on tmpfs
    if (mode != QLatin1String("relay")) {
        const QList<QHostAddress> lan = ShareServer::lanAddresses();
        for (const QHostAddress &a : lan)
            if (a.toString() == address)
                o.address = a;
        if (o.address.isNull() && !lan.isEmpty())
            o.address = lan.first();
    }
    setLastMode(mode == QLatin1String("relay") ? QStringLiteral("relay") : QStringLiteral("local"));
    auto *s = new ShareSession(&m_nam, accessFor(serverId), this);
    if (mode == QLatin1String("relay") && !serverId.isEmpty() && serverIndex(serverId) >= 0) {
        m_settings.setValue(QStringLiteral("lastServer"), serverId);
        emit serversChanged();
    }
    add(s);
    s->begin(sources, o);
    return s->id();
}

void ShareController::stop(const QString &id)
{
    for (ShareSession *s : std::as_const(m_sessions))
        if (s->id() == id)
            s->stop();
}

void ShareController::dismiss(const QString &id)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i)->id() != id || m_sessions.at(i)->active())
            continue;
        beginRemoveRows({}, i, i);
        m_sessions.takeAt(i)->deleteLater();
        endRemoveRows();
        emit countChanged();
        scheduleSave();
        return;
    }
}

void ShareController::stopAll()
{
    for (ShareSession *s : std::as_const(m_sessions))
        s->stop();
}

void ShareController::clearFinished()
{
    for (int i = m_sessions.size() - 1; i >= 0; --i)
        if (!m_sessions.at(i)->active()) {
            beginRemoveRows({}, i, i);
            m_sessions.takeAt(i)->deleteLater();
            endRemoveRows();
        }
    emit countChanged();
    scheduleSave();
}

QStringList ShareController::lanAddresses() const
{
    QStringList out;
    for (const QHostAddress &a : ShareServer::lanAddresses())
        out << a.toString();
    return out;
}

// ---- remembering relay sessions --------------------------------------------------------------

void ShareController::saveNow()
{
    QJsonArray list;
    for (const ShareSession *s : m_sessions)
        if (s->survivesRestart())
            list.append(s->toJson());
    const QString path = m_notch->settingsFolder() + QStringLiteral("/sessions.json");
    if (list.isEmpty()) {
        QFile::remove(path);
        return;
    }
    QSaveFile f(path); // the links carry the keys: owner-only
    if (f.open(QIODevice::WriteOnly) && f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        f.write(QJsonDocument(list).toJson(QJsonDocument::Compact));
    f.commit();
}

void ShareController::restore()
{
    QFile f(m_notch->settingsFolder() + QStringLiteral("/sessions.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray list = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : list) {
        // Back to the server it was made on (an older file has none: the default one); gone server, gone entry.
        const RelayAccess relay = accessFor(v.toObject().value(QStringLiteral("serverId")).toString());
        if (!relay.valid())
            continue;
        if (ShareSession *s = ShareSession::fromJson(v.toObject(), &m_nam, relay, this))
            add(s);
    }
}

// ---- settings --------------------------------------------------------------------------------

void ShareController::setDefaultTtl(int seconds)
{
    m_settings.setValue(QStringLiteral("ttl"), qBound(60, seconds, 7 * 86400));
    emit settingsChanged();
}

void ShareController::setLastMode(const QString &mode)
{
    if (mode == lastMode())
        return;
    m_settings.setValue(QStringLiteral("mode"), mode);
    emit settingsChanged();
}

// ---- saved servers ---------------------------------------------------------------------------

QString ShareController::serversFolder() const
{
    return m_notch->settingsFolder() + QStringLiteral("/servers");
}

int ShareController::serverIndex(const QString &id) const
{
    for (int i = 0; i < m_servers.size(); ++i)
        if (m_servers.at(i).id == id)
            return i;
    return -1;
}

QString ShareController::keyOf(const QString &id) const
{
    QFile f(serversFolder() + u'/' + id + QStringLiteral(".key"));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

bool ShareController::storeKey(const QString &id, const QString &key)
{
    QDir().mkpath(serversFolder());
    QFile::setPermissions(serversFolder(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(serversFolder() + u'/' + id + QStringLiteral(".key")); // the key is a password: owner-only
    return f.open(QIODevice::WriteOnly) && f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
           && f.write(key.trimmed().toUtf8()) >= 0 && f.commit();
}

QString ShareController::defaultServerId() const
{
    const QString id = m_settings.value(QStringLiteral("defaultServer")).toString();
    return serverIndex(id) >= 0 ? id : (m_servers.isEmpty() ? QString() : m_servers.first().id);
}

void ShareController::setDefaultServerId(const QString &id)
{
    if (serverIndex(id) < 0)
        return;
    m_settings.setValue(QStringLiteral("defaultServer"), id);
    emit serversChanged();
}

RelayAccess ShareController::accessFor(const QString &id) const
{
    int i = serverIndex(id);
    if (i < 0)
        i = serverIndex(defaultServerId());
    if (i < 0)
        return {};
    return {m_servers.at(i).id, m_servers.at(i).name, m_servers.at(i).url, keyOf(m_servers.at(i).id).toUtf8()};
}

QVariantList ShareController::servers() const
{
    QVariantList out;
    for (const Server &s : m_servers) {
        QVariantMap m = m_status.value(s.id);
        m.insert(QStringLiteral("id"), s.id);
        m.insert(QStringLiteral("name"), s.name);
        m.insert(QStringLiteral("url"), s.url.toString());
        m.insert(QStringLiteral("hasKey"), !keyOf(s.id).isEmpty());
        m.insert(QStringLiteral("isDefault"), s.id == defaultServerId());
        m.insert(QStringLiteral("tested"), m_status.contains(s.id));
        out.append(m);
    }
    return out;
}

void ShareController::loadServers()
{
    QFile f(m_notch->settingsFolder() + QStringLiteral("/servers.json"));
    if (!f.exists()) {
        migrateOldServer();
        return;
    }
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
        const QJsonObject o = v.toObject();
        const QUrl url = Invite::normalizeServer(o.value(QStringLiteral("url")).toString());
        if (!o.value(QStringLiteral("id")).toString().isEmpty() && url.isValid())
            m_servers.append({o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("name")).toString(), url});
    }
}

void ShareController::saveServers()
{
    QJsonArray list;
    for (const Server &s : m_servers)
        list.append(QJsonObject{{QStringLiteral("id"), s.id}, {QStringLiteral("name"), s.name}, {QStringLiteral("url"), s.url.toString()}});
    QSaveFile f(m_notch->settingsFolder() + QStringLiteral("/servers.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(list).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

// Before several servers existed there was one: share.conf relayUrl + relay.key. It becomes the first profile.
void ShareController::migrateOldServer()
{
    const QString oldKeyPath = m_notch->settingsFolder() + QStringLiteral("/relay.key");
    QFile oldKey(oldKeyPath);
    const QUrl url = Invite::normalizeServer(m_settings.value(QStringLiteral("relayUrl")).toString());
    if (!url.isValid() || !oldKey.open(QIODevice::ReadOnly))
        return;
    const QString key = QString::fromUtf8(oldKey.readAll()).trimmed();
    oldKey.close();
    if (key.isEmpty() || saveNewServer(tr("My server"), url, key).isEmpty())
        return;
    QFile::remove(oldKeyPath);
    m_settings.remove(QStringLiteral("relayUrl"));
}

QString ShareController::saveNewServer(const QString &name, const QUrl &url, const QString &key)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!storeKey(id, key))
        return {};
    m_servers.append({id, name.trimmed().isEmpty() ? url.host() : name.trimmed(), url});
    saveServers();
    emit serversChanged();
    return id;
}

QString ShareController::addServer(const QString &name, const QString &address, const QString &key)
{
    const QUrl url = Invite::normalizeServer(address);
    if (!url.isValid())
        return tr("The server address must start with https://");
    if (key.trimmed().isEmpty())
        return tr("Enter the API key first");
    const QString id = saveNewServer(name, url, key);
    if (id.isEmpty())
        return tr("Cannot save the API key");
    testServer(id);
    return {};
}

void ShareController::removeServer(const QString &id)
{
    const int i = serverIndex(id);
    if (i < 0)
        return;
    m_servers.removeAt(i);
    m_status.remove(id);
    QFile::remove(serversFolder() + u'/' + id + QStringLiteral(".key"));
    saveServers();
    emit serversChanged();
}

void ShareController::renameServer(const QString &id, const QString &name)
{
    const int i = serverIndex(id);
    if (i < 0 || name.trimmed().isEmpty())
        return;
    m_servers[i].name = name.trimmed();
    saveServers();
    emit serversChanged();
}

void ShareController::joinServer(const QString &invitation)
{
    const Invite::Parsed p = Invite::parse(invitation);
    if (!p.error.isEmpty()) {
        emit joined(false, p.error);
        return;
    }
    QUrl url = p.server;
    url.setPath(url.path() + QStringLiteral("/api/v1/join"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(30000);
    QNetworkReply *reply = m_nam.post(req, QJsonDocument(QJsonObject{{QStringLiteral("token"), p.token}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, server = p.server] {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            const QString why = o.value(QStringLiteral("message")).toString();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit joined(false, !why.isEmpty() ? why
                               : status == 429 ? tr("Too many tries, wait a few minutes")
                               : status == 0   ? tr("Cannot reach the server")
                                               : tr("The server refused the invitation"));
            return;
        }
        const QString key = o.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) {
            emit joined(false, tr("That address is not a Notch share server"));
            return;
        }
        const QString id = saveNewServer(server.host(), server, key);
        if (id.isEmpty()) {
            emit joined(false, tr("Cannot save the API key"));
            return;
        }
        emit joined(true, tr("Joined %1 as %2").arg(server.host(), o.value(QStringLiteral("name")).toString()));
        testServer(id);
    });
}

void ShareController::testAllServers()
{
    for (const Server &s : std::as_const(m_servers))
        testServer(s.id);
}

void ShareController::testServer(const QString &id)
{
    const RelayAccess relay = accessFor(id);
    if (serverIndex(id) < 0 || !relay.valid()) {
        emit serverTested(id, false, tr("Enter the API key first"));
        return;
    }
    QNetworkReply *reply = m_nam.get(relay.request(QStringLiteral("/api/v1/ping")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
        reply->deleteLater();
        QVariantMap st;
        QString message;
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
            message = tr("The server did not accept the API key");
        } else if (reply->error() != QNetworkReply::NoError) {
            message = reply->error() == QNetworkReply::SslHandshakeFailedError ? tr("The server's HTTPS certificate is not valid")
                                                                              : tr("Cannot reach the server");
        } else {
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            if (!o.value(QStringLiteral("ok")).toBool()) {
                message = tr("That address is not a Notch share server");
            } else {
                const QJsonObject lim = o.value(QStringLiteral("limits")).toObject(), use = o.value(QStringLiteral("usage")).toObject();
                auto num = [](const QJsonValue &v) { return v.toVariant().toLongLong(); };
                st = {{QStringLiteral("member"), o.value(QStringLiteral("member")).toString()},
                      // An older relay has no "limits": its max_file / max_ttl still say what it takes.
                      {QStringLiteral("maxFile"), num(lim.value(QStringLiteral("max_file"))) ? num(lim.value(QStringLiteral("max_file"))) : num(o.value(QStringLiteral("max_file")))},
                      {QStringLiteral("maxTtl"), num(lim.value(QStringLiteral("max_ttl"))) ? num(lim.value(QStringLiteral("max_ttl"))) : num(o.value(QStringLiteral("max_ttl")))},
                      {QStringLiteral("quota"), num(lim.value(QStringLiteral("quota")))},
                      {QStringLiteral("maxActive"), num(lim.value(QStringLiteral("max_active")))},
                      {QStringLiteral("used"), num(use.value(QStringLiteral("used")))},
                      {QStringLiteral("active"), num(use.value(QStringLiteral("active")))}};
                message = tr("Connected as %1").arg(o.value(QStringLiteral("member")).toString(tr("guest")));
            }
        }
        const bool ok = !st.isEmpty();
        st.insert(QStringLiteral("ok"), ok);
        st.insert(QStringLiteral("message"), message);
        if (serverIndex(id) >= 0) { // it may have been removed while the test ran
            m_status.insert(id, st);
            emit serversChanged();
            emit serverTested(id, ok, message);
        }
    });
}
