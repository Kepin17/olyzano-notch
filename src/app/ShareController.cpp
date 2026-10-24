#include "ShareController.h"

#include "services/FileService.h"
#include "services/ShareCrypto.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSaveFile>
#include <QUuid>
#include <QtConcurrent>

namespace {
QString megabytes(qint64 bytes) { return QString::number(bytes / 1048576.0, 'f', bytes >= 10485760 ? 0 : 1) + QStringLiteral(" MB"); }
}

ShareController::ShareController(NotchController *notch, QObject *parent)
    : QObject(parent)
    , m_notch(notch)
    , m_settings(notch->settingsFolder() + QStringLiteral("/share.conf"), QSettings::IniFormat)
{
    m_expire.setSingleShot(true);
    connect(&m_expire, &QTimer::timeout, this, [this] { end(QStringLiteral("expired")); });
    m_poll.setInterval(4000);
    connect(&m_poll, &QTimer::timeout, this, &ShareController::pollRelay);
    connect(&m_server, &ShareServer::fileDownloaded, this, [this] {
        ++m_downloads;
        emit changed();
    });
    connect(&m_server, &ShareServer::finished, this, [this](const QString &reason) {
        if (reason != QLatin1String("stopped") && m_mode == QLatin1String("local") && m_status == QLatin1String("ready"))
            end(reason);
    });
    QDir(m_notch->scratchPath(false) + QStringLiteral("/share")).removeRecursively(); // leftovers of a crash
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { stop(); });
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
            fail(tr("Cannot save the API key"));
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

QNetworkRequest ShareController::relayRequest(const QString &path) const
{
    QUrl url = normalizedRelay();
    url.setPath(url.path() + path);
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "Bearer " + relayKey().toUtf8());
    req.setTransferTimeout(120000);
    return req;
}

QString ShareController::networkError(QNetworkReply *reply)
{
    switch (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()) {
    case 401: return tr("The server did not accept the API key");
    case 413: return tr("The file is too big for the server (or the server is full)");
    case 429: return tr("Too many tries, wait a few minutes");
    default: break;
    }
    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
        return tr("Cannot reach the server");
    case QNetworkReply::SslHandshakeFailedError:
        return tr("The server's HTTPS certificate is not valid");
    default:
        return reply->errorString();
    }
}

void ShareController::testRelay()
{
    if (!normalizedRelay().isValid()) {
        emit relayTested(false, tr("The server address must start with https://"));
        return;
    }
    if (!relayKeySet()) {
        emit relayTested(false, tr("Enter the API key first"));
        return;
    }
    QNetworkReply *reply = m_nam.get(relayRequest(QStringLiteral("/api/v1/ping")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit relayTested(false, networkError(reply));
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

// ---- running a share -------------------------------------------------------------------------

QStringList ShareController::lanAddresses() const
{
    QStringList out;
    for (const QHostAddress &a : ShareServer::lanAddresses())
        out << a.toString();
    return out;
}

void ShareController::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}

void ShareController::fail(const QString &why)
{
    clean();
    m_message = why;
    setStatus(QStringLiteral("error"));
}

// Drops everything a share holds (server, upload, timers, temp files); keeps the last state shown.
void ShareController::clean()
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

void ShareController::end(const QString &reason)
{
    if (m_status != QLatin1String("ready") && m_status != QLatin1String("uploading") && m_status != QLatin1String("preparing"))
        return;
    if (m_mode == QLatin1String("relay") && !m_relayId.isEmpty() && reason != QLatin1String("downloaded")
        && reason != QLatin1String("expired")) {
        QNetworkReply *r = m_nam.deleteResource(relayRequest(QStringLiteral("/api/v1/shares/") + m_relayId));
        connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    }
    clean();
    m_endReason = reason;
    m_message = reason == QLatin1String("downloaded") ? tr("Downloaded")
              : reason == QLatin1String("expired")    ? tr("The link has expired")
                                                      : tr("Sharing stopped");
    setStatus(QStringLiteral("ended"));
}

void ShareController::stop()
{
    if (m_status == QLatin1String("idle"))
        return;
    end(QStringLiteral("stopped"));
    m_relayId.clear();
    m_link.clear();
    m_message.clear();
    m_endReason.clear();
    m_progress = -1;
    setStatus(QStringLiteral("idle"));
}

void ShareController::start(const QStringList &ids, const QString &mode, int ttlSeconds, const QString &pin,
                            bool oneDownload, const QString &address)
{
    stop();
    const auto sources = m_notch->shareSources(ids);
    if (sources.isEmpty()) {
        m_mode = mode;
        fail(tr("These files are not available (locked or still copying)"));
        return;
    }
    m_mode = mode == QLatin1String("relay") ? QStringLiteral("relay") : QStringLiteral("local");
    m_link.clear();
    m_message.clear();
    m_endReason.clear();
    m_relayId.clear();
    m_progress = -1;
    m_downloads = 0;
    m_fileCount = sources.size();
    m_ttl = qBound(60, ttlSeconds, 7 * 86400);
    m_pin = pin.trimmed();
    m_oneDownload = oneDownload;
    m_name = sources.size() == 1 ? sources.first().name : tr("%1 files").arg(sources.size());
    setLastMode(m_mode);

    if (sources.size() > ShareCrypto::MaxFiles) {
        fail(tr("A share can hold up to %1 files").arg(ShareCrypto::MaxFiles));
        return;
    }
    if (m_mode == QLatin1String("relay") && !relayConfigured()) {
        fail(tr("Set up your server first (Settings → Sharing)"));
        return;
    }
    QHostAddress host;
    if (m_mode == QLatin1String("local")) {
        const QList<QHostAddress> lan = ShareServer::lanAddresses();
        for (const QHostAddress &a : lan)
            if (a.toString() == address)
                host = a;
        if (host.isNull() && !lan.isEmpty())
            host = lan.first();
        if (host.isNull()) {
            fail(tr("No network connection found. Connect to WiFi first"));
            return;
        }
        m_bindAddress = host;
    }
    bool anyVault = false;
    for (const auto &s : sources)
        anyVault = anyVault || s.vault;
    const QString root = m_notch->scratchPath(anyVault);
    const bool encrypt = m_mode == QLatin1String("relay");
    const int gen = m_generation;
    setStatus(QStringLiteral("preparing"));
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
        if (m_mode == QLatin1String("relay"))
            return upload(p);
        onPrepared(p);
    });
    watcher->setFuture(QtConcurrent::run([=] { return prepare(sources, encrypt, root); }));
}

ShareController::Prepared ShareController::prepare(const QList<NotchController::ShareSource> &sources, bool encrypt,
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
    if (encrypt) {
        p.key = ShareCrypto::newKey();
        p.encryptedPath = p.workDir + QStringLiteral("/share.enc");
        p.error = ShareCrypto::encryptFiles(files, p.encryptedPath, p.key);
    }
    return p;
}

void ShareController::onPrepared(const Prepared &p)
{
    const QString why = m_server.start(m_bindAddress, p.entries, m_ttl);
    if (!why.isEmpty())
        return fail(why);
    m_link = m_server.url().toString();
    m_expiresAt = QDateTime::currentSecsSinceEpoch() + m_ttl;
    m_expire.start(m_ttl * 1000);
    setStatus(QStringLiteral("ready"));
}

void ShareController::upload(const Prepared &p)
{
    auto *file = new QFile(p.encryptedPath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        return fail(tr("Cannot read the encrypted file"));
    }
    QNetworkRequest req = relayRequest(QStringLiteral("/api/v1/shares"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    req.setRawHeader("X-TTL", QByteArray::number(m_ttl));
    req.setRawHeader("X-Max-Downloads", m_oneDownload && m_fileCount == 1 ? "1" : "0");
    if (!m_pin.isEmpty())
        req.setRawHeader("X-Pin", m_pin.toUtf8());
    m_progress = 0;
    setStatus(QStringLiteral("uploading"));
    m_upload = m_nam.post(req, file);
    file->setParent(m_upload);
    const QString fragment = ShareCrypto::keyToFragment(p.key);
    connect(m_upload, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0 && m_status == QLatin1String("uploading")) {
            m_progress = double(sent) / total;
            emit changed();
        }
    });
    QNetworkReply *reply = m_upload;
    connect(reply, &QNetworkReply::finished, this, [this, reply, fragment] {
        if (reply != m_upload) // aborted by stop()
            return;
        m_upload = nullptr;
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError)
            return fail(networkError(reply));
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        m_relayId = o.value(QStringLiteral("id")).toString();
        if (m_relayId.isEmpty())
            return fail(tr("That address is not a Notch share server"));
        QUrl link = normalizedRelay();
        link.setPath(link.path() + QStringLiteral("/s/") + m_relayId);
        link.setFragment(fragment);
        m_link = link.toString();
        m_expiresAt = o.value(QStringLiteral("expires_at")).toVariant().toLongLong();
        m_progress = 1;
        if (!m_workDir.isEmpty()) { // the ciphertext is on the server now
            QDir(m_workDir).removeRecursively();
            m_workDir.clear();
        }
        m_expire.start(qMax<qint64>(1, m_expiresAt - QDateTime::currentSecsSinceEpoch()) * 1000);
        m_poll.start();
        setStatus(QStringLiteral("ready"));
    });
}

// Asks the relay whether the link is still alive; gone means it was downloaded (single-use) or expired.
void ShareController::pollRelay()
{
    if (m_status != QLatin1String("ready") || m_relayId.isEmpty())
        return;
    const int gen = m_generation;
    QNetworkReply *reply = m_nam.get(relayRequest(QStringLiteral("/api/v1/shares/") + m_relayId));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen] {
        reply->deleteLater();
        if (gen != m_generation || m_status != QLatin1String("ready"))
            return;
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404)
            end(m_oneDownload && m_fileCount == 1 && QDateTime::currentSecsSinceEpoch() < m_expiresAt ? QStringLiteral("downloaded")
                                                                                     : QStringLiteral("expired"));
    });
}
