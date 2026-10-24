#include "ShareController.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSaveFile>

namespace {
QString megabytes(qint64 bytes) { return QString::number(bytes / 1048576.0, 'f', bytes >= 10485760 ? 0 : 1) + QStringLiteral(" MB"); }
}

ShareController::ShareController(NotchController *notch, QObject *parent)
    : QAbstractListModel(parent)
    , m_notch(notch)
    , m_settings(notch->settingsFolder() + QStringLiteral("/share.conf"), QSettings::IniFormat)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &ShareController::saveNow);
    QDir(m_notch->scratchPath(false) + QStringLiteral("/share")).removeRecursively(); // leftovers of a crash
    restore();
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
            {ProtectionRole, "protection"}, {ActiveRole, "active"}, {SingleUseRole, "singleUse"}};
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
                               const QString &secret, bool oneDownload, const QString &address)
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
    auto *s = new ShareSession(&m_nam, relayAccess(), this);
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
    for (const QJsonValue &v : list)
        if (ShareSession *s = ShareSession::fromJson(v.toObject(), &m_nam, relayAccess(), this))
            add(s);
}

// ---- settings --------------------------------------------------------------------------------

void ShareController::setRelayUrl(const QString &url)
{
    m_settings.setValue(QStringLiteral("relayUrl"), url.trimmed());
    emit settingsChanged();
}

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

QString ShareController::relayKey() const
{
    QFile f(m_notch->settingsFolder() + QStringLiteral("/relay.key"));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

void ShareController::setRelayKey(const QString &key)
{
    const QString path = m_notch->settingsFolder() + QStringLiteral("/relay.key");
    if (key.trimmed().isEmpty()) {
        QFile::remove(path);
    } else {
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly)
            || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || f.write(key.trimmed().toUtf8()) < 0 || !f.commit()) {
            emit relayTested(false, tr("Cannot save the API key"));
            return;
        }
    }
    emit settingsChanged();
}

QUrl ShareController::normalizedRelay() const
{
    QUrl url(relayUrl().trimmed());
    while (url.path().endsWith(u'/'))
        url.setPath(url.path().chopped(1));
    const bool local = url.host() == QLatin1String("localhost") || url.host() == QLatin1String("127.0.0.1");
    if (url.host().isEmpty() || !(url.scheme() == QLatin1String("https") || (url.scheme() == QLatin1String("http") && local)))
        return {};
    return url;
}

void ShareController::testRelay()
{
    const RelayAccess relay = relayAccess();
    if (!normalizedRelay().isValid()) {
        emit relayTested(false, tr("The server address must start with https://"));
        return;
    }
    if (!relayKeySet()) {
        emit relayTested(false, tr("Enter the API key first"));
        return;
    }
    QNetworkReply *reply = m_nam.get(relay.request(QStringLiteral("/api/v1/ping")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
            emit relayTested(false, tr("The server did not accept the API key"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit relayTested(false, reply->error() == QNetworkReply::SslHandshakeFailedError ? tr("The server's HTTPS certificate is not valid")
                                                                                            : tr("Cannot reach the server"));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (!o.value(QStringLiteral("ok")).toBool()) {
            emit relayTested(false, tr("That address is not a Notch share server"));
            return;
        }
        emit relayTested(true, tr("Connected. Up to %1 per file, %2 free.")
                                   .arg(megabytes(o.value(QStringLiteral("max_file")).toVariant().toLongLong()),
                                        megabytes(o.value(QStringLiteral("free")).toVariant().toLongLong())));
    });
}
