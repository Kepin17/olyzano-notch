#include "CloudController.h"

#include "services/FileService.h"
#include "services/ShareCrypto.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QtConcurrent>

namespace {
constexpr int PollMs = 5 * 60 * 1000;
constexpr int Parallel = 2;
}

CloudController::CloudController(NotchController *notch, ShareController *share, QObject *parent)
    : QObject(parent)
    , m_notch(notch)
    , m_share(share)
    , m_settings(notch->settingsFolder() + QStringLiteral("/cloud.conf"), QSettings::IniFormat)
{
    m_entries.setSourceModel(&m_model);
    m_pool.setMaxThreadCount(Parallel);
    QDir(notch->scratchPath(false) + QStringLiteral("/cloud")).removeRecursively(); // leftovers of a crash
    m_poll.setInterval(PollMs);
    connect(&m_poll, &QTimer::timeout, this, &CloudController::syncNow);
    // The server's own answers say whether we are paused or removed, so a test of the servers refreshes the state too.
    connect(m_share, &ShareController::serverTested, this, [this](const QString &id, bool ok, const QString &message) {
        if (id != serverId() || !enabled())
            return;
        const QVariantMap st = m_share->serverStatus(id);
        if (st.value(QStringLiteral("disabled")).toBool())
            setState(QStringLiteral("disabled"), message);
        else if (st.value(QStringLiteral("removed")).toBool())
            setState(QStringLiteral("removed"), message);
        else if (ok) {
            if (!st.value(QStringLiteral("cloudOn")).toBool()) {
                setState(QStringLiteral("unsupported"), tr("This server has no cloud shelf"));
                return;
            }
            m_net.clear();
            m_used = st.value(QStringLiteral("cloudUsed")).toLongLong();
            m_quota = st.value(QStringLiteral("cloudQuota")).toLongLong();
            evaluate();
            syncNow();
        } else if (m_net != QLatin1String("offline")) {
            m_net = QStringLiteral("offline");
            setState(QStringLiteral("offline"), message);
        }
    });
    connect(m_share, &ShareController::serversChanged, this, [this] { evaluate(); });
    if (enabled()) {
        m_poll.start();
        loadCache();
        evaluate();
        QTimer::singleShot(2000, this, &CloudController::syncNow); // after the servers have been tested
    }
}

QString CloudController::serverId() const
{
    const QString id = m_settings.value(QStringLiteral("serverId")).toString();
    return id.isEmpty() ? m_share->defaultServerId() : id;
}

void CloudController::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    m_entries.setSearchText(text);
    emit changed();
}

void CloudController::setSortMode(const QString &mode)
{
    if (mode != QLatin1String("newest") && mode != QLatin1String("oldest") && mode != QLatin1String("name") && mode != QLatin1String("size"))
        return;
    if (m_sortMode == mode)
        return;
    m_sortMode = mode;
    m_entries.setSortMode(mode);
    emit changed();
}

bool CloudController::isHost() const { return m_share->serverStatus(serverId()).value(QStringLiteral("owner")).toBool(); }
bool CloudController::usable() const { return m_state == QLatin1String("ready") || m_state == QLatin1String("syncing"); }

QByteArray CloudController::key() const { return m_share->shelfKey(serverId()); }

void CloudController::setServerId(const QString &id)
{
    if (id == m_settings.value(QStringLiteral("serverId")).toString())
        return;
    m_settings.setValue(QStringLiteral("serverId"), id);
    m_raw.clear();
    m_seq = 0;
    m_loadedFor.clear();
    m_net.clear();
    m_model.setAll({});
    evaluate();
    if (enabled()) {
        loadCache();
        m_share->testServer(id);
    }
    emit changed();
}

void CloudController::setEnabled(bool on)
{
    if (on == enabled())
        return;
    m_settings.setValue(QStringLiteral("enabled"), on);
    if (!on) {
        m_poll.stop();
        setState(QStringLiteral("off"));
        return;
    }
    // the host who switches the shelf on makes its key (members get it with their invitations)
    const QString id = serverId();
    if (!id.isEmpty() && key().isEmpty() && isHost())
        m_share->setShelfKey(id, ShareCrypto::newKey());
    m_poll.start();
    m_net.clear();
    loadCache();
    evaluate();
    if (!id.isEmpty())
        m_share->testServer(id);
}

void CloudController::setState(const QString &state, const QString &message)
{
    if (m_state == state && m_message == message)
        return;
    m_state = state;
    m_message = message;
    emit changed();
}

// What the state is when nothing has gone wrong on the network: off, no server, no key, or fine.
void CloudController::evaluate()
{
    if (!enabled())
        return setState(QStringLiteral("off"));
    if (m_net == QLatin1String("disabled") || m_net == QLatin1String("removed") || m_net == QLatin1String("unsupported") || m_net == QLatin1String("offline"))
        return;
    const QString id = serverId();
    if (id.isEmpty())
        return setState(QStringLiteral("needsServer"), tr("Join a Share server first (Settings → Sharing)"));
    if (key().isEmpty()) {
        if (isHost()) { // the host's own key is made the first time
            m_share->setShelfKey(id, ShareCrypto::newKey());
        } else {
            return setState(QStringLiteral("needsKey"), tr("Ask the host for a new invitation: the shelf's key was not in yours"));
        }
    }
    if (m_state == QLatin1String("syncing"))
        return;
    setState(m_share->serverStatus(id).isEmpty() ? QStringLiteral("connecting") : QStringLiteral("ready"), {});
}

QNetworkRequest CloudController::request(const QString &path) const
{
    const RelayAccess relay = m_share->relayFor(serverId());
    if (!relay.valid())
        return QNetworkRequest();
    QNetworkRequest req = relay.request(path.section(u'?', 0, 0));
    if (path.contains(u'?')) {
        QUrl url = req.url();
        url.setQuery(path.section(u'?', 1));
        req.setUrl(url);
    }
    req.setTransferTimeout(120000);
    return req;
}

// The words for a failed call. The relay's answer says when the host paused or removed this member, and why.
QString CloudController::failure(QNetworkReply *reply)
{
    const QJsonObject o = QJsonDocument::fromJson(reply->property("body").toByteArray()).object();
    const QString error = o.value(QStringLiteral("error")).toString(), why = o.value(QStringLiteral("message")).toString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (error == QLatin1String("disabled")) {
        m_net = QStringLiteral("disabled");
        setState(QStringLiteral("disabled"), why);
    } else if (error == QLatin1String("removed") || error == QLatin1String("ended")) {
        m_net = QStringLiteral("removed");
        setState(QStringLiteral("removed"), why);
    } else if (error == QLatin1String("cloud_off")) {
        m_net = QStringLiteral("unsupported");
        setState(QStringLiteral("unsupported"), why);
    } else if (status == 0) {
        m_net = QStringLiteral("offline");
        setState(QStringLiteral("offline"), tr("Cannot reach the server"));
    }
    // Without a word from the relay (an nginx or Cloudflare page, say) the code at least says who answered and how.
    return !why.isEmpty() ? why : status == 0 ? tr("Cannot reach the server")
         : status == 413 ? tr("The server's front door refused the size of the file (HTTP 413): raise client_max_body_size in nginx, or the file is over Cloudflare's 100 MB")
                         : tr("The server refused the request (HTTP %1)").arg(status);
}

// ---- the list --------------------------------------------------------------------------------

QString CloudController::cachePath() const
{
    return m_notch->settingsFolder() + QStringLiteral("/cloud-") + serverId() + QStringLiteral(".json");
}

void CloudController::loadCache()
{
    const QString id = serverId();
    if (id.isEmpty() || m_loadedFor == id)
        return;
    m_loadedFor = id;
    m_raw.clear();
    m_seq = 0;
    QFile f(cachePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_seq = o.value(QStringLiteral("seq")).toVariant().toLongLong();
        m_me = o.contains(QStringLiteral("me")) ? o.value(QStringLiteral("me")).toVariant().toLongLong() : -1;
        const QJsonObject objects = o.value(QStringLiteral("objects")).toObject();
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            const QJsonObject r = it.value().toObject();
            m_raw.insert(it.key(), {r.value(QStringLiteral("owner")).toVariant().toLongLong(), r.value(QStringLiteral("size")).toVariant().toLongLong(),
                                    r.value(QStringLiteral("updated")).toVariant().toLongLong(),
                                    QByteArray::fromBase64(r.value(QStringLiteral("meta")).toString().toLatin1())});
        }
    }
    rebuild();
}

void CloudController::saveCache() const
{
    if (serverId().isEmpty())
        return;
    QJsonObject objects;
    for (auto it = m_raw.cbegin(); it != m_raw.cend(); ++it)
        objects.insert(it.key(), QJsonObject{{QStringLiteral("owner"), it->owner}, {QStringLiteral("size"), it->size}, {QStringLiteral("updated"), it->updated},
                                             {QStringLiteral("meta"), QString::fromLatin1(it->meta.toBase64())}});
    QSaveFile f(cachePath());
    if (f.open(QIODevice::WriteOnly) && f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("seq"), m_seq}, {QStringLiteral("me"), m_me}, {QStringLiteral("objects"), objects}}).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void CloudController::rebuild()
{
    const QByteArray k = key();
    QList<CloudModel::Entry> rows;
    for (auto it = m_raw.cbegin(); it != m_raw.cend(); ++it) {
        CloudModel::Entry e;
        e.id = it.key();
        e.owner = it->owner;
        e.size = it->size;
        const QByteArray plain = k.isEmpty() ? QByteArray() : ShareCrypto::openBlob(k, it->meta);
        const QJsonObject m = QJsonDocument::fromJson(plain).object();
        if (plain.isNull() || m.isEmpty()) {
            e.locked = true;
            e.added = QDateTime::fromSecsSinceEpoch(it->updated, Qt::UTC);
        } else {
            e.name = m.value(QStringLiteral("n")).toString();
            e.mime = m.value(QStringLiteral("m")).toString();
            e.category = m.value(QStringLiteral("c")).toString();
            e.added = QDateTime::fromSecsSinceEpoch(m.value(QStringLiteral("a")).toVariant().toLongLong(), Qt::UTC);
        }
        rows.append(e);
    }
    m_model.setMe(m_me);
    m_model.setAll(rows);
    emit changed();
}

void CloudController::syncNow()
{
    if (!enabled() || m_net == QLatin1String("removed") || m_net == QLatin1String("unsupported"))
        return;
    evaluate();
    if (m_state == QLatin1String("off") || m_state == QLatin1String("needsServer") || m_state == QLatin1String("needsKey"))
        return;
    if (m_syncing) {
        m_again = true;
        return;
    }
    QNetworkRequest req = request(QStringLiteral("/api/v1/cloud?since=%1").arg(m_seq));
    if (req.url().isEmpty())
        return;
    m_syncing = true;
    if (m_state == QLatin1String("ready"))
        setState(QStringLiteral("syncing"));
    QNetworkReply *reply = m_share->nam()->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_syncing = false;
        const QByteArray body = reply->readAll();
        reply->setProperty("body", body);
        if (reply->error() != QNetworkReply::NoError) {
            failure(reply);
            if (m_state == QLatin1String("syncing"))
                setState(QStringLiteral("ready"));
            return;
        }
        m_net.clear();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        m_me = o.contains(QStringLiteral("mine")) ? o.value(QStringLiteral("mine")).toVariant().toLongLong() : -1;
        m_used = o.value(QStringLiteral("used")).toVariant().toLongLong();
        m_quota = o.value(QStringLiteral("quota")).toVariant().toLongLong();
        for (const QJsonValue &v : o.value(QStringLiteral("objects")).toArray()) {
            const QJsonObject r = v.toObject();
            const QString id = r.value(QStringLiteral("id")).toString();
            if (r.value(QStringLiteral("deleted")).toBool())
                m_raw.remove(id);
            else
                m_raw.insert(id, {r.value(QStringLiteral("owner")).toVariant().toLongLong(), r.value(QStringLiteral("size")).toVariant().toLongLong(),
                                  r.value(QStringLiteral("updated")).toVariant().toLongLong(), QByteArray::fromBase64(r.value(QStringLiteral("meta")).toString().toLatin1())});
        }
        if (o.contains(QStringLiteral("seq")))
            m_seq = o.value(QStringLiteral("seq")).toVariant().toLongLong();
        saveCache();
        rebuild();
        if (m_state == QLatin1String("syncing") || m_state == QLatin1String("disabled") || m_state == QLatin1String("offline") || m_state == QLatin1String("connecting"))
            setState(QStringLiteral("ready"));
        if (o.value(QStringLiteral("more")).toBool() || m_again) {
            m_again = false;
            QTimer::singleShot(0, this, &CloudController::syncNow);
        }
    });
}

// ---- sending ---------------------------------------------------------------------------------

QByteArray CloudController::metaFor(const Job &job, qint64 size, qint64 added) const
{
    const QJsonObject m{{QStringLiteral("n"), job.name}, {QStringLiteral("m"), job.mime}, {QStringLiteral("c"), job.category},
                        {QStringLiteral("a"), added > 0 ? added : QDateTime::currentSecsSinceEpoch()}, {QStringLiteral("s"), size}};
    return ShareCrypto::sealBlob(key(), QJsonDocument(m).toJson(QJsonDocument::Compact));
}

void CloudController::uploadItems(const QStringList &itemIds)
{
    for (const QString &id : itemIds) {
        const FileItem *item = m_notch->itemFor(id);
        const QString path = m_notch->storedPath(id);
        if (!item || path.isEmpty())
            continue;
        QString category;
        for (const QVariant &c : m_notch->categories())
            if (c.toMap().value(QStringLiteral("id")).toString() == item->categoryId)
                category = c.toMap().value(QStringLiteral("name")).toString();
        Job job{QUuid::createUuid().toString(QUuid::WithoutBraces), path, item->originalName, item->mimeType, category};
        m_jobs.insert(job.id, job);
        m_uploads.append({job.id, job.name, QStringLiteral("Queued"), {}, -1});
        m_queue.append(job);
    }
    pump();
    emit changed();
}

void CloudController::uploadUrls(const QList<QUrl> &urls)
{
    static const QMimeDatabase mimes;
    for (const QUrl &url : urls) {
        const QFileInfo fi(url.toLocalFile());
        if (url.isLocalFile() && fi.isFile()) {
            Job job{QUuid::createUuid().toString(QUuid::WithoutBraces), fi.absoluteFilePath(), fi.fileName(), mimes.mimeTypeForFile(fi).name(), QString()};
            m_jobs.insert(job.id, job);
            m_uploads.append({job.id, job.name, QStringLiteral("Queued"), {}, -1});
            m_queue.append(job);
        }
    }
    pump();
    emit changed();
}

void CloudController::retryUpload(const QString &uploadId)
{
    const CloudUploadModel::Upload *upload = m_uploads.find(uploadId);
    const auto job = m_jobs.constFind(uploadId);
    if (!upload || upload->status != QLatin1String("Failed") || job == m_jobs.cend())
        return;
    if (!usable()) {
        emit failed(m_state == QLatin1String("disabled") || m_state == QLatin1String("removed") ? m_message
                                                                                                : tr("The cloud shelf is not ready: %1").arg(m_message.isEmpty() ? m_state : m_message));
        return;
    }
    if (!QFileInfo(job->path).isFile()) {
        const QString error = tr("The original file is no longer available");
        m_uploads.update(uploadId, QStringLiteral("Failed"), -1, error);
        emit failed(tr("%1: %2").arg(job->name, error));
        return;
    }
    m_uploads.update(uploadId, QStringLiteral("Queued"), -1);
    m_queue.append(job.value());
    pump();
    emit changed();
}

void CloudController::pump()
{
    if (!usable() && !m_queue.isEmpty()) {
        const QString error = m_state == QLatin1String("disabled") || m_state == QLatin1String("removed") ? m_message
                                                                                                           : tr("The cloud shelf is not ready: %1").arg(m_message.isEmpty() ? m_state : m_message);
        for (const Job &job : std::as_const(m_queue))
            m_uploads.update(job.id, QStringLiteral("Failed"), -1, error);
        m_queue.clear();
        emit failed(error);
        emit changed();
        return;
    }
    while (m_active < Parallel && !m_queue.isEmpty()) {
        ++m_active;
        startUpload(m_queue.takeFirst());
    }
}

void CloudController::startUpload(const Job &job)
{
    const QString tmpDir = m_notch->scratchPath(false) + QStringLiteral("/cloud");
    QDir().mkpath(tmpDir);
    const QString tmp = tmpDir + u'/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".up");
    const QString transferId = QStringLiteral("cloud-up-") + QFileInfo(tmp).completeBaseName();
    NotchController::Transfer t;
    t.done = std::make_shared<std::atomic<qint64>>(0);
    t.cancel = std::make_shared<std::atomic_bool>(false);
    t.total = QFileInfo(job.path).size();
    t.title = job.name;
    t.verb = tr("Uploading");
    m_notch->beginTransfer(transferId, t);
    m_uploads.update(job.id, QStringLiteral("Preparing"), -1);

    auto finish = [this, transferId, tmp, job](const QString &error) {
        m_notch->endTransfer(transferId);
        QFile::remove(tmp);
        --m_active;
        if (!error.isEmpty()) {
            m_uploads.update(job.id, QStringLiteral("Failed"), -1, error);
            emit failed(error);
        } else {
            m_uploads.update(job.id, QStringLiteral("Complete"), 100);
        }
        pump();
        emit changed();
    };
    // First the file is encrypted to a temporary file (a worker: it can be big), then that is sent.
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, job, tmp, t, finish] {
        const QString err = watcher->result();
        watcher->deleteLater();
        if (!err.isEmpty())
            return finish(tr("%1: %2").arg(job.name, err));
        if (t.cancel->load())
            return finish(tr("%1: upload cancelled").arg(job.name));
        auto *file = new QFile(tmp);
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            return finish(tr("%1: cannot read the encrypted copy").arg(job.name));
        }
        QNetworkRequest req = request(QStringLiteral("/api/v1/cloud/objects"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        req.setRawHeader("X-Meta", metaFor(job, QFileInfo(job.path).size()).toBase64());
        req.setTransferTimeout(0); // a big file takes as long as it takes
        QNetworkReply *reply = m_share->nam()->post(req, file);
        file->setParent(reply);
        const qint64 plainSize = t.total, sealedSize = file->size();
        m_uploads.update(job.id, QStringLiteral("Uploading"), 0);
        connect(reply, &QNetworkReply::uploadProgress, this, [this, reply, t, plainSize, sealedSize, uploadId = job.id](qint64 sent, qint64) {
            t.done->store(sealedSize > 0 ? sent * plainSize / sealedSize : 0); // bytes of the file, not of its encrypted copy
            const int percent = sealedSize > 0 ? int(qMin<qint64>(99, sent * 100 / sealedSize)) : -1;
            m_uploads.update(uploadId, QStringLiteral("Uploading"), percent);
            if (t.cancel->load())
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, job, finish, t] {
            reply->deleteLater();
            const QByteArray body = reply->readAll();
            reply->setProperty("body", body);
            if (t.cancel->load())
                return finish(tr("%1: upload cancelled").arg(job.name));
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 413)
                return finish(tr("%1: upload rejected; the maximum file size through Cloudflare is 100 MB").arg(job.name));
            if (reply->error() != QNetworkReply::NoError)
                return finish(tr("%1: %2").arg(job.name, failure(reply)));
            emit notice(tr("Sent to the cloud shelf"), job.name);
            finish({});
            syncNow();
        });
    });
    const QString src = job.path, name = job.name, mime = job.mime;
    const QByteArray k = key();
    watcher->setFuture(QtConcurrent::run(&m_pool, [src, name, mime, tmp, k] {
        return ShareCrypto::encryptFiles({{src, name, mime}}, tmp, k);
    }));
}

// ---- fetching and changing -------------------------------------------------------------------------------------

void CloudController::download(const QString &cloudId, bool thenShare)
{
    const CloudModel::Entry *entry = m_model.find(cloudId);
    if (!entry || entry->locked || entry->busy || !usable())
        return;
    const QString name = entry->name, category = entry->category;
    const qint64 total = entry->size;
    const QString tmpDir = m_notch->scratchPath(false) + QStringLiteral("/cloud/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(tmpDir);
    auto *enc = new QFile(tmpDir + QStringLiteral("/file.enc"));
    if (!enc->open(QIODevice::WriteOnly)) {
        delete enc;
        emit failed(tr("%1: cannot write to the temporary folder").arg(name));
        return;
    }
    NotchController::Transfer t;
    t.done = std::make_shared<std::atomic<qint64>>(0);
    t.cancel = std::make_shared<std::atomic_bool>(false);
    t.total = total;
    t.title = name;
    t.verb = tr("Downloading");
    const QString transferId = QStringLiteral("cloud-down-") + cloudId;
    m_notch->beginTransfer(transferId, t);
    m_model.setProgress(cloudId, true, 0);

    QNetworkReply *reply = m_share->nam()->get(request(QStringLiteral("/api/v1/cloud/objects/") + cloudId));
    enc->setParent(reply);
    auto done = [this, cloudId, transferId, tmpDir](const QString &error) {
        m_notch->endTransfer(transferId);
        m_model.setProgress(cloudId, false, -1);
        if (!error.isEmpty()) {
            QDir(tmpDir).removeRecursively();
            emit failed(error);
        } else {
            QTimer::singleShot(15 * 60 * 1000, qApp, [tmpDir] { QDir(tmpDir).removeRecursively(); }); // by then the shelf has its own copy
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, enc] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200 || status == 206) // an error's JSON stays unread, for failure() to put in words
            enc->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply, t, cloudId](qint64 got, qint64 all) {
        t.done->store(got);
        m_model.setProgress(cloudId, true, all > 0 ? int(qMin<qint64>(99, got * 100 / all)) : 0);
        if (t.cancel->load())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, enc, name, category, tmpDir, done, t, thenShare] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200 || status == 206)
            enc->write(reply->readAll());
        else
            reply->setProperty("body", reply->readAll());
        enc->close();
        if (t.cancel->load())
            return done(QString());
        if (reply->error() != QNetworkReply::NoError)
            return done(tr("%1: %2").arg(name, failure(reply)));
        // decrypt on a worker, then the file goes on the local shelf like any other
        auto *watcher = new QFutureWatcher<QString>(this);
        auto names = std::make_shared<QStringList>();
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, names, tmpDir, name, category, done, thenShare] {
            const QString err = watcher->result();
            watcher->deleteLater();
            if (!err.isEmpty() || names->isEmpty())
                return done(tr("%1: %2").arg(name, err.isEmpty() ? tr("the file could not be opened") : err));
            const QString catId = category.isEmpty() ? QString() : m_notch->createCategory(category);
            // Adopt straight onto the shelf (Available at once) so Share via QR can open without waiting on a copy.
            const QString localId = m_notch->adoptLocalFile(tmpDir + QStringLiteral("/out/") + names->first(), names->first(),
                                                            tr("From the cloud shelf"), catId);
            if (localId.isEmpty())
                return done(tr("%1: %2").arg(name, tr("cannot save")));
            emit notice(tr("Fetched from the cloud shelf"), name);
            if (thenShare)
                emit readyToShare(localId);
            done({});
        });
        const QString in = tmpDir + QStringLiteral("/file.enc"), out = tmpDir + QStringLiteral("/out");
        const QByteArray k = key();
        watcher->setFuture(QtConcurrent::run(&m_pool, [in, out, k, names] {
            QDir().mkpath(out);
            return ShareCrypto::decryptFiles(in, out, k, names.get());
        }));
    });
}

void CloudController::remove(const QString &cloudId)
{
    if (!usable())
        return;
    QNetworkReply *reply = m_share->nam()->deleteResource(request(QStringLiteral("/api/v1/cloud/objects/") + cloudId));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        reply->setProperty("body", reply->readAll());
        if (reply->error() != QNetworkReply::NoError)
            emit failed(failure(reply));
        syncNow();
    });
}

QStringList CloudController::allIds() const
{
    QStringList ids;
    ids.reserve(m_model.rows().size());
    for (const CloudModel::Entry &entry : m_model.rows())
        ids.append(entry.id);
    return ids;
}

QStringList CloudController::removableIds() const
{
    QStringList ids;
    for (const CloudModel::Entry &entry : m_model.rows())
        if (isHost() || entry.owner == m_me)
            ids.append(entry.id);
    return ids;
}

bool CloudController::canRemoveItems(const QStringList &cloudIds) const
{
    if (cloudIds.isEmpty())
        return false;
    for (const QString &id : cloudIds) {
        const CloudModel::Entry *entry = m_model.find(id);
        if (!entry || entry->busy || (!isHost() && entry->owner != m_me))
            return false;
    }
    return true;
}

void CloudController::removeItems(const QStringList &cloudIds)
{
    if (!usable()) {
        emit failed(tr("The cloud shelf is not ready: %1").arg(m_message.isEmpty() ? m_state : m_message));
        return;
    }
    if (!canRemoveItems(cloudIds)) {
        emit failed(tr("You can only remove files you own from the cloud shelf"));
        return;
    }

    struct Batch {
        int remaining = 0;
        QStringList errors;
    };
    auto batch = std::make_shared<Batch>();
    QSet<QString> unique;
    for (const QString &id : cloudIds) {
        if (unique.contains(id))
            continue;
        unique.insert(id);
        const CloudModel::Entry *entry = m_model.find(id);
        if (!entry)
            continue;
        const QString name = entry->name.isEmpty() ? id : entry->name;
        ++batch->remaining;
        QNetworkReply *reply = m_share->nam()->deleteResource(request(QStringLiteral("/api/v1/cloud/objects/") + id));
        connect(reply, &QNetworkReply::finished, this, [this, reply, batch, name] {
            reply->deleteLater();
            reply->setProperty("body", reply->readAll());
            if (reply->error() != QNetworkReply::NoError)
                batch->errors.append(tr("%1: %2").arg(name, failure(reply)));
            if (--batch->remaining == 0) {
                if (!batch->errors.isEmpty())
                    emit failed(batch->errors.join(QLatin1Char('\n')));
                syncNow();
            }
        });
    }
}

void CloudController::rename(const QString &cloudId, const QString &name)
{
    const CloudModel::Entry *e = m_model.find(cloudId);
    const QString clean = FileService::safeFileName(name);
    if (!e || e->locked || !usable() || clean.isEmpty() || clean == e->name)
        return;
    const Job job{QString(), QString(), clean, e->mime, e->category};
    QNetworkRequest req = request(QStringLiteral("/api/v1/cloud/objects/") + cloudId + QStringLiteral("/meta"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("meta"), QString::fromLatin1(metaFor(job, e->size, e->added.toSecsSinceEpoch()).toBase64())}}).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_share->nam()->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        reply->setProperty("body", reply->readAll());
        if (reply->error() != QNetworkReply::NoError)
            emit failed(failure(reply));
        syncNow();
    });
}
