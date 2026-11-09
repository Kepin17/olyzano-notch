#include "ModelStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStorageInfo>
#include <QtConcurrent>

namespace {
// SHA-256 of a file, "" when it cannot be read.
QString sha256Of(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f))
        return {};
    return QString::fromLatin1(h.result().toHex());
}

// the program called `name` anywhere under `dir`
QString findProgram(const QString &dir, const QString &name)
{
    QDirIterator it(dir, {name}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}
}

ModelStore::ModelStore(const QString &root, QObject *parent, const QList<Audio::CatalogItem> &items)
    : QObject(parent), m_root(root), m_items(items)
{
}

Audio::CatalogItem ModelStore::item(const QString &id) const
{
    for (const Audio::CatalogItem &i : m_items)
        if (i.id == id)
            return i;
    return {};
}

QString ModelStore::finalPath(const Audio::CatalogItem &i) const
{
    return i.kind == Audio::Kind::Helper ? m_root + QStringLiteral("/helpers/") + i.id : m_root + QStringLiteral("/models/") + i.file;
}

QString ModelStore::partPath(const Audio::CatalogItem &i) const
{
    return m_root + QStringLiteral("/downloads/") + i.file + QStringLiteral(".part");
}

QString ModelStore::folder(const QString &id) const
{
    const Audio::CatalogItem i = item(id);
    if (i.id.isEmpty())
        return {};
    const QString p = path(id);
    return i.kind == Audio::Kind::Helper && !p.isEmpty() ? QFileInfo(p).absolutePath() : QString();
}

QString ModelStore::path(const QString &id) const
{
    const Audio::CatalogItem i = item(id);
    if (i.id.isEmpty())
        return {};
    if (i.kind == Audio::Kind::Helper) {
        // ready = unpacked and checked (the marker is written last)
        if (!QFileInfo::exists(finalPath(i) + QStringLiteral("/.ready")))
            return {};
        return findProgram(finalPath(i), i.exe);
    }
    const QFileInfo f(finalPath(i));
    return f.exists() && f.size() == i.size ? f.absoluteFilePath() : QString();
}

QString ModelStore::state(const QString &id) const
{
    const Audio::CatalogItem i = item(id);
    if (i.id.isEmpty())
        return QStringLiteral("missing");
    if (m_verifying.value(id))
        return QStringLiteral("verifying");
    if (m_jobs.contains(id))
        return QStringLiteral("downloading");
    if (!path(id).isEmpty())
        return QStringLiteral("ready");
    return partialBytes(id) > 0 ? QStringLiteral("partial") : QStringLiteral("missing");
}

qint64 ModelStore::partialBytes(const QString &id) const
{
    const Audio::CatalogItem i = item(id);
    return i.id.isEmpty() ? 0 : QFileInfo(partPath(i)).size();
}

double ModelStore::speed(const QString &id) const
{
    const auto j = m_jobs.constFind(id);
    return j != m_jobs.cend() ? j->rate : 0;
}

qint64 ModelStore::receivedBytes(const QString &id) const
{
    const auto j = m_jobs.constFind(id);
    return j != m_jobs.cend() ? j->start + j->got : partialBytes(id);
}

double ModelStore::progress(const QString &id) const
{
    const Audio::CatalogItem i = item(id);
    if (i.id.isEmpty() || i.size <= 0)
        return 0;
    const auto j = m_jobs.constFind(id);
    const qint64 have = j != m_jobs.cend() ? j->start + j->got : partialBytes(id);
    return std::clamp(double(have) / double(i.size), 0.0, 1.0);
}

qint64 ModelStore::diskBytes(const QString &id) const
{
    const Audio::CatalogItem i = item(id);
    if (i.id.isEmpty())
        return 0;
    qint64 total = partialBytes(id);
    if (i.kind == Audio::Kind::Helper) {
        QDirIterator it(finalPath(i), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            total += QFileInfo(it.next()).size();
    } else {
        total += QFileInfo(finalPath(i)).size();
    }
    return total;
}

void ModelStore::fail(const QString &id, const QString &why)
{
    m_error.insert(id, why);
    m_verifying.remove(id);
    emit changed(id);
}

void ModelStore::download(const QString &id)
{
    const Audio::CatalogItem i = item(id);
    if (i.id.isEmpty() || m_jobs.contains(id) || m_verifying.value(id) || !path(id).isEmpty())
        return;
    m_error.remove(id);
    QDir().mkpath(m_root + QStringLiteral("/downloads"));
    QDir().mkpath(m_root + QStringLiteral("/models"));
    const qint64 have = std::min(partialBytes(id), i.size);
    // room for what is left, and for a helper also for the unpacked files (a few times the archive)
    const QStorageInfo disk(m_root + QStringLiteral("/downloads"));
    const qint64 need = (i.size - have) + (i.kind == Audio::Kind::Helper ? i.size * 4 : i.size / 20) + (Q_INT64_C(50) << 20);
    if (disk.isValid() && disk.bytesAvailable() >= 0 && disk.bytesAvailable() < need) {
        fail(id, tr("Not enough disk space: %1 needed, %2 free").arg(QLocale().formattedDataSize(need), QLocale().formattedDataSize(disk.bytesAvailable())));
        return;
    }
    auto *file = new QFile(partPath(i), this);
    if (!file->open(have > 0 ? QIODevice::WriteOnly | QIODevice::Append : QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString why = file->errorString();
        delete file;
        fail(id, tr("Cannot write the download: %1").arg(why));
        return;
    }
    QNetworkRequest req{QUrl(i.url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60000);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OlyzanoNotch"));
    if (have > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(have) + "-");
    QNetworkReply *reply = m_net.get(req);
    Job job;
    job.reply = reply;
    job.file = file;
    job.start = have;
    job.clock.start();
    m_jobs.insert(id, job);
    m_lastPercent.insert(id, -1);
    emit changed(id);
    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply] {
        auto it = m_jobs.find(id);
        if (it == m_jobs.end())
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200 && status != 206) // an error page: not written into the file
            return;
        if (status == 200 && it->start > 0 && it->got == 0) { // asked to continue, but the server sends it all again
            it->file->resize(0);
            it->start = 0;
        }
        const QByteArray data = reply->readAll();
        if (it->file->write(data) != data.size()) {
            reply->abort();
            return;
        }
        it->got += data.size();
        bool tell = false;
        if (const qint64 now = it->clock.elapsed(); now - it->sampleAt >= 700) { // the speed, smoothed so the number does not jump
            const double instant = double(it->got - it->sampleGot) * 1000.0 / double(now - it->sampleAt);
            it->rate = it->rate > 0 ? it->rate * 0.6 + instant * 0.4 : instant;
            it->sampleAt = now;
            it->sampleGot = it->got;
            tell = true;
        }
        const int pct = int(progress(id) * 100);
        if (pct != m_lastPercent.value(id)) {
            m_lastPercent.insert(id, pct);
            tell = true;
        }
        if (tell)
            emit changed(id);
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply] {
        reply->deleteLater();
        const auto it = m_jobs.find(id);
        if (it == m_jobs.end())
            return;
        Job job = *it;
        m_jobs.erase(it);
        job.file->flush();
        job.file->close();
        job.file->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const Audio::CatalogItem i = item(id);
        if (job.cancelled) {
            emit changed(id);
            return;
        }
        if (status == 416) { // what we have is all of it, or more than the server holds
            if (QFileInfo(partPath(i)).size() >= i.size) {
                verifyThenInstall(id);
                return;
            }
            QFile::remove(partPath(i));
            fail(id, tr("The server did not accept the resume; try again"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            fail(id, status >= 400 ? tr("The server answered %1").arg(status)
                                   : reply->error() == QNetworkReply::SslHandshakeFailedError ? tr("The server's HTTPS certificate is not valid")
                                                                                              : tr("The download was interrupted: %1").arg(reply->errorString()));
            return;
        }
        if (QFileInfo(partPath(i)).size() != i.size) {
            fail(id, tr("The download is incomplete (%1 of %2 bytes)").arg(QFileInfo(partPath(i)).size()).arg(i.size));
            return;
        }
        verifyThenInstall(id);
    });
}

void ModelStore::verifyThenInstall(const QString &id)
{
    const Audio::CatalogItem i = item(id);
    m_verifying.insert(id, true);
    emit changed(id);
    const QString part = partPath(i), finalP = finalPath(i), expect = i.sha256, exe = i.exe;
    const bool helper = i.kind == Audio::Kind::Helper;
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, id, part] {
        const QString err = w->result();
        w->deleteLater();
        m_verifying.remove(id);
        if (!err.isEmpty()) {
            fail(id, err);
            return;
        }
        QFile::remove(part);
        emit changed(id);
    });
    w->setFuture(QtConcurrent::run([part, finalP, expect, exe, helper]() -> QString {
        if (!expect.isEmpty() && sha256Of(part) != expect) {
            QFile::remove(part); // a file that does not match is no use to resume
            return QObject::tr("The downloaded file does not match its checksum and was deleted");
        }
        if (!helper) {
            QFile::remove(finalP);
            return QFile::rename(part, finalP) ? QString() : QObject::tr("Cannot save the model");
        }
        // a helper: unpack into a fresh folder, find the program, then mark it ready
        QDir(finalP).removeRecursively();
        QDir().mkpath(finalP);
        QProcess tar;
        tar.start(QStringLiteral("tar"), {QStringLiteral("-xf"), part, QStringLiteral("-C"), finalP});
        if (!tar.waitForStarted(5000))
            return QObject::tr("tar is needed to unpack this download but could not be started");
        if (!tar.waitForFinished(300000) || tar.exitCode() != 0) {
            const QString why = QString::fromUtf8(tar.readAllStandardError()).trimmed().left(200);
            QDir(finalP).removeRecursively();
            return QObject::tr("The download could not be unpacked: %1").arg(why);
        }
        const QString program = findProgram(finalP, exe);
        if (program.isEmpty()) {
            QDir(finalP).removeRecursively();
            return QObject::tr("The program %1 is not in the download").arg(exe);
        }
        QFile::setPermissions(program, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ExeGroup);
        QFile marker(finalP + QStringLiteral("/.ready"));
        return marker.open(QIODevice::WriteOnly) ? QString() : QObject::tr("Cannot finish the installation");
    }));
}

void ModelStore::cancel(const QString &id)
{
    const auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return;
    it->cancelled = true;
    if (it->reply)
        it->reply->abort();
}

bool ModelStore::remove(const QString &id)
{
    const Audio::CatalogItem i = item(id);
    if (i.id.isEmpty() || m_jobs.contains(id) || m_verifying.value(id))
        return false;
    bool ok = true;
    if (i.kind == Audio::Kind::Helper)
        ok = !QFileInfo::exists(finalPath(i)) || QDir(finalPath(i)).removeRecursively();
    else
        ok = !QFileInfo::exists(finalPath(i)) || QFile::remove(finalPath(i));
    ok = (!QFileInfo::exists(partPath(i)) || QFile::remove(partPath(i))) && ok;
    m_error.remove(id);
    emit changed(id);
    return ok;
}
