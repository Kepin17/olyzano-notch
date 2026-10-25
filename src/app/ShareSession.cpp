#include "ShareSession.h"

#include "services/FileService.h"
#include "services/ShareCrypto.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUuid>
#include <QtConcurrent>

QNetworkRequest RelayAccess::request(const QString &path) const
{
    QUrl url = base;
    url.setPath(url.path() + path);
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "Bearer " + apiKey);
    req.setTransferTimeout(120000);
    return req;
}

static QString networkError(QNetworkReply *reply)
{
    switch (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()) {
    case 401: return ShareSession::tr("The server did not accept the API key");
    case 413: return ShareSession::tr("The file is too big for the server (or the server is full)");
    case 429: return ShareSession::tr("Too many tries, wait a few minutes");
    default: break;
    }
    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
        return ShareSession::tr("Cannot reach the server");
    case QNetworkReply::SslHandshakeFailedError:
        return ShareSession::tr("The server's HTTPS certificate is not valid");
    default:
        return reply->errorString();
    }
}

ShareSession::ShareSession(QNetworkAccessManager *nam, const RelayAccess &relay, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
    , m_relay(relay)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_expire.setSingleShot(true);
    connect(&m_expire, &QTimer::timeout, this, [this] { end(QStringLiteral("expired")); });
    m_poll.setInterval(4000);
    connect(&m_poll, &QTimer::timeout, this, &ShareSession::poll);
    connect(&m_server, &ShareServer::fileDownloaded, this, [this] {
        ++m_downloads;
        emit changed();
    });
    connect(&m_server, &ShareServer::finished, this, [this](const QString &reason) {
        if (reason != QLatin1String("stopped") && m_mode == QLatin1String("local") && m_status == QLatin1String("ready"))
            end(reason);
    });
}

QString ShareSession::checkSecret(const QString &protection, const QString &secret)
{
    if (protection == QLatin1String("pin")) {
        static const QRegularExpression digits(QStringLiteral("^[0-9]{4,12}$"));
        return digits.match(secret).hasMatch() ? QString() : tr("A PIN is 4 to 12 digits");
    }
    if (protection == QLatin1String("password"))
        return secret.size() >= 8 ? QString() : tr("Use at least 8 characters for the password");
    return {};
}

void ShareSession::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}

void ShareSession::fail(const QString &why)
{
    clean();
    m_message = why;
    setStatus(QStringLiteral("error"));
}

// Drops everything a share holds (server, upload, timers, temp files); keeps the last state shown.
void ShareSession::clean()
{
    ++m_generation;
    m_expire.stop();
    m_poll.stop();
    m_server.stop();
    if (m_upload) {
        QNetworkReply *r = m_upload;
        m_upload = nullptr;
        r->abort();
        r->deleteLater();
    }
    if (!m_workDir.isEmpty())
        QDir(m_workDir).removeRecursively();
    m_workDir.clear();
}

void ShareSession::end(const QString &reason)
{
    if (!active())
        return;
    if (m_mode == QLatin1String("relay") && !m_relayId.isEmpty() && reason == QLatin1String("stopped")) {
        QNetworkReply *r = m_nam->deleteResource(m_relay.request(QStringLiteral("/api/v1/shares/") + m_relayId));
        connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    }
    clean();
    m_endReason = reason;
    m_message = reason == QLatin1String("downloaded") ? tr("Downloaded")
              : reason == QLatin1String("expired")    ? tr("The link has expired")
                                                      : tr("Sharing stopped");
    setStatus(QStringLiteral("ended"));
}

void ShareSession::stop()
{
    end(QStringLiteral("stopped"));
}

void ShareSession::suspend()
{
    if (m_mode == QLatin1String("relay") && m_status == QLatin1String("ready")) {
        m_expire.stop();
        m_poll.stop();
        return; // stays on the server; the next start picks it up again
    }
    stop();
}

void ShareSession::begin(const QList<NotchController::ShareSource> &sources, const Options &o)
{
    m_mode = o.mode == QLatin1String("relay") ? QStringLiteral("relay") : QStringLiteral("local");
    m_protection = o.secret.isEmpty() ? QString() : o.protection;
    m_secret = o.secret;
    m_fileCount = sources.size();
    m_ttl = qBound(60, o.ttl, 7 * 86400);
    m_name = sources.size() == 1 ? sources.first().name : tr("%1 files").arg(sources.size());
    m_singleUse = o.oneDownload && sources.size() == 1;
    m_status = QStringLiteral("preparing");
    emit changed();
    if (sources.isEmpty())
        return fail(tr("These files are not available (locked or still copying)"));
    if (sources.size() > ShareCrypto::MaxFiles)
        return fail(tr("A share can hold up to %1 files").arg(ShareCrypto::MaxFiles));
    if (const QString why = checkSecret(m_protection, m_secret); !why.isEmpty())
        return fail(why);
    if (m_mode == QLatin1String("relay") && !m_relay.valid())
        return fail(tr("Set up your server first (Settings → Sharing)"));
    m_bindAddress = o.address;
    if (m_mode == QLatin1String("local") && m_bindAddress.isNull())
        return fail(tr("No network connection found. Connect to WiFi first"));

    const int gen = m_generation;
    auto *watcher = new QFutureWatcher<Prepared>(this);
    connect(watcher, &QFutureWatcher<Prepared>::finished, this, [this, watcher, gen] {
        const Prepared p = watcher->result();
        watcher->deleteLater();
        if (gen != m_generation) { // stopped meanwhile
            if (!p.workDir.isEmpty())
                QDir(p.workDir).removeRecursively();
            return;
        }
        m_workDir = p.workDir;
        if (!p.error.isEmpty())
            return fail(p.error);
        m_mode == QLatin1String("relay") ? upload(p) : onPrepared(p);
    });
    const Options opts = o;
    watcher->setFuture(QtConcurrent::run([=] { return prepare(sources, opts, o.workRoot); }));
}

ShareSession::Prepared ShareSession::prepare(const QList<NotchController::ShareSource> &sources, const Options &o,
                                             const QString &workRoot)
{
    Prepared p;
    p.workDir = workRoot + QStringLiteral("/share/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(p.workDir)) {
        p.error = tr("Cannot create a temporary folder");
        return p;
    }
    QFile::setPermissions(p.workDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    // Two files may share a name (report.pdf from two folders): the recipient sees "report (1).pdf".
    QStringList used;
    QList<ShareCrypto::SourceFile> files;
    for (const auto &s : sources) {
        const QString name = FileService::uniqueAmong(s.name, [&](const QString &n) { return used.contains(n); });
        used << name;
        const QString mime = s.mimeType.isEmpty() ? QStringLiteral("application/octet-stream") : s.mimeType;
        p.entries.append({s.path, name, mime, 0});
        files.append({s.path, name, mime});
    }
    if (o.mode == QLatin1String("relay")) {
        const QByteArray key = ShareCrypto::newKey();
        p.encryptedPath = p.workDir + QStringLiteral("/share.enc");
        p.error = ShareCrypto::encryptFiles(files, p.encryptedPath, key);
        // A password goes into the link itself: the key is locked with it (a PIN is checked by the server instead).
        p.fragment = o.protection == QLatin1String("password") ? ShareCrypto::wrapKey(key, o.secret) : ShareCrypto::keyToFragment(key);
        if (p.error.isEmpty() && p.fragment.isEmpty())
            p.error = tr("Cannot protect the link with a password");
    }
    return p;
}

void ShareSession::onPrepared(const Prepared &p)
{
    const QString why = m_server.start(m_bindAddress, p.entries, m_ttl, m_protection, m_secret);
    if (!why.isEmpty())
        return fail(why);
    m_link = m_server.url().toString();
    m_expiresAt = QDateTime::currentSecsSinceEpoch() + m_ttl;
    m_expire.start(m_ttl * 1000);
    setStatus(QStringLiteral("ready"));
}

void ShareSession::upload(const Prepared &p)
{
    auto *file = new QFile(p.encryptedPath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        return fail(tr("Cannot read the encrypted file"));
    }
    QNetworkRequest req = m_relay.request(QStringLiteral("/api/v1/shares"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    req.setRawHeader("X-TTL", QByteArray::number(m_ttl));
    req.setRawHeader("X-Max-Downloads", m_singleUse ? "1" : "0");
    if (m_protection == QLatin1String("pin"))
        req.setRawHeader("X-Pin", m_secret.toUtf8());
    m_progress = 0;
    setStatus(QStringLiteral("uploading"));
    m_upload = m_nam->post(req, file);
    file->setParent(m_upload);
    connect(m_upload, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0 && m_status == QLatin1String("uploading")) {
            m_progress = double(sent) / total;
            emit changed();
        }
    });
    QNetworkReply *reply = m_upload;
    const QString fragment = p.fragment;
    connect(reply, &QNetworkReply::finished, this, [this, reply, fragment] {
        if (reply != m_upload) // aborted by stop()
            return;
        m_upload = nullptr;
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (reply->error() != QNetworkReply::NoError) {
            // The server says why when it can ("Your space on this server is full (4 of 5 MB used)").
            const QString why = o.value(QStringLiteral("message")).toString();
            return fail(why.isEmpty() ? networkError(reply) : why);
        }
        m_relayId = o.value(QStringLiteral("id")).toString();
        if (m_relayId.isEmpty())
            return fail(tr("That address is not a Olyzano Notch share server"));
        QUrl link = m_relay.base;
        link.setPath(link.path() + QStringLiteral("/s/") + m_relayId);
        link.setFragment(fragment);
        m_link = link.toString();
        m_expiresAt = o.value(QStringLiteral("expires_at")).toVariant().toLongLong();
        m_progress = 1;
        if (!m_workDir.isEmpty()) { // the ciphertext is on the server now
            QDir(m_workDir).removeRecursively();
            m_workDir.clear();
        }
        watchRelay();
        setStatus(QStringLiteral("ready"));
    });
}

// A ready relay share: end it when its time is up, and ask the server now and then whether the link
// still lives (gone = downloaded, if it was single-use, else expired).
void ShareSession::watchRelay()
{
    m_expire.start(qMax<qint64>(1, m_expiresAt - QDateTime::currentSecsSinceEpoch()) * 1000);
    m_poll.start();
}

void ShareSession::poll()
{
    if (m_status != QLatin1String("ready") || m_relayId.isEmpty())
        return;
    const int gen = m_generation;
    QNetworkReply *reply = m_nam->get(m_relay.request(QStringLiteral("/api/v1/shares/") + m_relayId));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen] {
        reply->deleteLater();
        if (gen != m_generation || m_status != QLatin1String("ready"))
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const int n = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("downloads")).toInt();
            if (n != m_downloads) {
                m_downloads = n;
                emit changed();
            }
        } else if (status == 404) {
            end(m_singleUse && QDateTime::currentSecsSinceEpoch() < m_expiresAt ? QStringLiteral("downloaded")
                                                                                   : QStringLiteral("expired"));
        }
    });
}

QJsonObject ShareSession::toJson() const
{
    return {{QStringLiteral("id"), m_id},
            {QStringLiteral("name"), m_name},
            {QStringLiteral("fileCount"), m_fileCount},
            {QStringLiteral("url"), m_link},
            {QStringLiteral("relayId"), m_relayId},
            {QStringLiteral("expiresAt"), double(m_expiresAt)},
            {QStringLiteral("singleUse"), m_singleUse},
            {QStringLiteral("protection"), m_protection},
            {QStringLiteral("serverId"), m_relay.id},
            {QStringLiteral("downloads"), m_downloads}};
}

ShareSession *ShareSession::fromJson(const QJsonObject &o, QNetworkAccessManager *nam, const RelayAccess &relay, QObject *parent)
{
    const qint64 expires = o.value(QStringLiteral("expiresAt")).toVariant().toLongLong();
    if (o.value(QStringLiteral("relayId")).toString().isEmpty() || expires <= QDateTime::currentSecsSinceEpoch())
        return nullptr;
    auto *s = new ShareSession(nam, relay, parent);
    s->m_id = o.value(QStringLiteral("id")).toString(s->m_id);
    s->m_mode = QStringLiteral("relay");
    s->m_name = o.value(QStringLiteral("name")).toString();
    s->m_fileCount = o.value(QStringLiteral("fileCount")).toInt();
    s->m_link = o.value(QStringLiteral("url")).toString();
    s->m_relayId = o.value(QStringLiteral("relayId")).toString();
    s->m_expiresAt = expires;
    s->m_singleUse = o.value(QStringLiteral("singleUse")).toBool();
    s->m_protection = o.value(QStringLiteral("protection")).toString();
    s->m_downloads = o.value(QStringLiteral("downloads")).toInt();
    s->m_status = QStringLiteral("ready");
    s->m_progress = 1;
    s->watchRelay();
    return s;
}
