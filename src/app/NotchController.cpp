#include "NotchController.h"

#include "services/CleanupService.h"
#ifdef Q_OS_WIN
#include "platform/WinPrivacy.h"
#endif
#include "services/FileService.h"
#include "services/ZipWriter.h"
#include "services/TextTools.h"
#include "services/ConvertModel.h"
#include "services/DocTools.h"

#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QProcess>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUuid>
#include <QtConcurrent>

#include <memory>

// ponytail: folder drops are walked on the main thread; the cap keeps a huge tree from stalling the UI.
constexpr int MaxFolderFiles = 1000;

static QString configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

// Spectacle (KDE's screenshot tool) runs with the session's config dir, not ours (ours may be redirected for testing).
static QString spectacleConfig()
{
    return QDir::homePath() + QStringLiteral("/.config/spectaclerc");
}

static QString autostartPath()
{
    return configDir() + QStringLiteral("/autostart/notch.desktop");
}

// A private tmpfs-like folder: XDG_RUNTIME_DIR on Linux, the temp folder where there is no such thing.
static QString runtimeDir()
{
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation); // RuntimeLocation is the home folder there
#else
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::TempLocation) : dir;
#endif
}

#ifdef Q_OS_WIN
// "Start with Windows" is a value in HKCU\...\Run.
static const QString RunKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
#endif

NotchController::NotchController(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_settings(configDir() + QStringLiteral("/notch/notch.conf"), QSettings::IniFormat)
    , m_runtimeDir(runtimeDir())
    // The mount name includes a hash of the data dir: separate Notch instances never share it.
    , m_vault(dataDir + QStringLiteral("/vault"),
              m_runtimeDir + QStringLiteral("/notch-vault-")
                  + QString::fromLatin1(QCryptographicHash::hash(dataDir.toUtf8(), QCryptographicHash::Sha1).toHex().left(8)))
    , m_dataDir(dataDir)
    , m_filesDir(m_settings.value(QStringLiteral("storageDir"), dataDir + QStringLiteral("/files")).toString())
    , m_retentionDays(qBound(1, m_settings.value(QStringLiteral("retentionDays"), DefaultRetentionDays).toInt(), 365))
    , m_model(m_filesDir)
{
    m_model.setRetentionDays(m_retentionDays);
    m_proxy.setSourceModel(&m_model);
    m_ingestPool.setMaxThreadCount(1); // one listing at a time, and not in the pool the rest of the app shares
    m_copyPool.setMaxThreadCount(2);
    m_ingestTimer.setSingleShot(true);
    connect(&m_ingestTimer, &QTimer::timeout, this, &NotchController::ingestStep);
    m_progressTimer.setInterval(250);
    connect(&m_progressTimer, &QTimer::timeout, this, &NotchController::progressTick);
    m_batchTimer.setSingleShot(true);
    connect(&m_batchTimer, &QTimer::timeout, this, &NotchController::nextBatch);
    m_indexTimer.setSingleShot(true);
    connect(&m_indexTimer, &QTimer::timeout, this, &NotchController::indexStep);
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(150);
    connect(&m_searchTimer, &QTimer::timeout, this, &NotchController::runSearch);
    // a category that has had a file put in is no longer "just made"
    connect(&m_model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &, int first, int last) {
        for (int i = first; i <= last && !m_fresh.isEmpty(); ++i)
            m_fresh.remove(m_model.items().at(i).categoryId);
    });
    // a file that arrived, or finished copying, gets its text into the search index
    connect(&m_model, &QAbstractItemModel::rowsInserted, this, &NotchController::scheduleIndexing);
    connect(&m_model, &QAbstractItemModel::modelReset, this, &NotchController::scheduleIndexing);
    // (the progress of a copy is for its own tile only: no index, count or category work four times a second)
    connect(&m_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                if (!(roles.size() == 1 && roles.first() == FileItemModel::CopyPercentRole))
                    scheduleIndexing();
            });
    m_proxy.setSortMode(sortMode());
    connect(&m_screenshots, &FolderWatcher::fileReady, this, [this](const QString &path) {
        onWatchedFile(path, QStringLiteral("screenshotCategory"), tr("Screenshots"), tr("Screenshot added"));
    });
    connect(&m_downloads, &FolderWatcher::fileReady, this, [this](const QString &path) {
        onWatchedFile(path, QStringLiteral("downloadCategory"), tr("Downloads"), tr("Downloaded"));
    });

    // Per-category counts only change when files come, go, move or get removed; not on the
    // minute tick or a copy's status change. refreshCategories() also skips no-op updates.
    // A drop of a thousand files inserts a thousand rows: the counts and the chips are brought up to date once for them, a moment after.
    m_countsTimer.setSingleShot(true);
    m_countsTimer.setInterval(40);
    connect(&m_countsTimer, &QTimer::timeout, this, [this] {
        refreshCategories();
        emit countChanged();
    });
    for (auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(&m_model, signal, this, [this] { if (!m_countsTimer.isActive()) m_countsTimer.start(); });
    connect(&m_model, &QAbstractItemModel::modelReset, this, &NotchController::countChanged);
    connect(&m_model, &QAbstractItemModel::modelReset, this, &NotchController::refreshCategories);
    connect(&m_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                if (roles.isEmpty() || roles.contains(FileItemModel::CategoryIdRole) || roles.contains(FileItemModel::RemovedRole))
                    refreshCategories();
            });

    m_clock.setInterval(60 * 1000);
    connect(&m_clock, &QTimer::timeout, &m_model, &FileItemModel::refreshTimes);
    m_cleanup.setInterval(60 * 60 * 1000);
    connect(&m_cleanup, &QTimer::timeout, this, &NotchController::runCleanup);
    m_trashTimer.setSingleShot(true);
    m_trashTimer.setInterval(6000);
    connect(&m_trashTimer, &QTimer::timeout, this, [this] {
        m_lastRemoved.clear(); // the files stay in Recently removed; only the toast goes
        emit undoCountChanged();
    });
    connect(&m_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                if (!(roles.size() == 1 && roles.first() == FileItemModel::CopyPercentRole))
                    emit countChanged();
            });
}

bool NotchController::start()
{
    if (!QDir().mkpath(m_filesDir)) {
        setError(tr("Cannot create storage directory %1").arg(m_filesDir));
        return false;
    }
    if (!m_repo.open(m_dataDir + QStringLiteral("/notch.db"))) {
        setError(tr("Database error: %1").arg(m_repo.errorString()));
        return false;
    }
    m_ready = true;
    CleanupService::recover(m_repo, m_filesDir);
    CleanupService::purge(m_repo, m_filesDir, QDateTime::currentDateTimeUtc(), m_retentionDays);
    m_categoryList = m_repo.categories();
    lockVault(); // a mount left behind by a crash: start locked
    m_model.setVaultDir(m_vault.mountDir());
    vaultStateChanged();
    connect(qApp, &QCoreApplication::aboutToQuit, this, &NotchController::lockVault);
    m_model.setItems(m_repo.visible());
    m_indexed = m_repo.indexedIds();
    scheduleIndexing();
    updateWatchers();
    expireEmptyCategories();
    m_clock.start();
    m_cleanup.start();
    return true;
}

const FileItem *NotchController::available(const QString &id) const
{
    const FileItem *item = m_model.find(id);
    if (item && item->vault && !m_vault.isUnlocked())
        return nullptr; // locked away
    return item && item->status == Status::Available ? item : nullptr;
}

QList<NotchController::ShareSource> NotchController::shareSources(const QStringList &ids) const
{
    QList<ShareSource> out;
    for (const QString &id : ids)
        if (const FileItem *item = available(id))
            out.append({pathOf(*item), item->originalName, item->mimeType, item->vault});
    return out;
}

int NotchController::addReceivedFiles(const QString &folder, const QStringList &names, const QString &fromDevice)
{
    if (!m_ready || names.isEmpty())
        return 0;
    const QString category = autoCategory(QStringLiteral("nearbyCategory"), tr("Nearby"));
    int added = 0;
    QString lastName, lastMime;
    for (const QString &name : names) {
        // adoptFile renames the file out of the (temporary) folder, so nothing is copied twice
        if (adoptFile(QDir(folder).filePath(name), name, tr("From %1").arg(fromDevice), category)) {
            ++added;
            lastName = name;
        }
    }
    if (added > 0) {
        emit sound(QStringLiteral("receive"));
        const QString title = fromDevice.isEmpty() ? tr("Received") : tr("From %1").arg(fromDevice);
        emit notice(title, added == 1 ? lastName : tr("%1 files").arg(added),
                    added == 1 ? QMimeDatabase().mimeTypeForFile(lastName).iconName() : QStringLiteral("folder-download"), QString());
    }
    return added;
}

QString NotchController::settingsFolder() const
{
    return QFileInfo(m_settings.fileName()).absolutePath();
}

bool NotchController::storeSetting(const QString &key, const QVariant &value)
{
    if (m_settings.contains(key) && m_settings.value(key) == value)
        return false;
    m_settings.setValue(key, value);
    return true;
}

QString NotchController::baseDir(const FileItem &item) const
{
    return item.vault ? m_vault.mountDir() : m_filesDir;
}

QString NotchController::pathOf(const FileItem &item) const
{
    return baseDir(item) + u'/' + item.storedName;
}

// Scratch space for derived plaintext (Quick Look renders, tool output): tmpfs when the source
// is a vault file, so nothing decrypted ever lands on disk.
QString NotchController::scratchDir(bool forVault) const
{
    return forVault ? m_runtimeDir + QStringLiteral("/notch-scratch") : m_dataDir;
}

bool NotchController::isLockedCategory(const QString &categoryId) const
{
    return !categoryId.isEmpty() && m_settings.value(QStringLiteral("lockedCategories")).toStringList().contains(categoryId);
}

bool NotchController::assignStorage(FileItem &item, QString *why)
{
    item.vault = isLockedCategory(item.categoryId);
    if (item.vault && !m_vault.isUnlocked()) {
        *why = tr("unlock the category first");
        return false;
    }
    return true;
}

// Moves one file between the files dir and the vault. Copy, then delete the source: a failure
// leaves the original where it was.
bool NotchController::relocate(const FileItem &item, bool toVault)
{
    if (item.vault == toVault)
        return true;
    FileItem moved = item;
    moved.vault = toVault;
    const QString dir = baseDir(moved) + u'/' + item.id;
    if (!QDir().mkpath(dir) || !QFile::copy(pathOf(item), pathOf(moved)) || !m_repo.setVault(item.id, toVault)) {
        QFile::remove(pathOf(moved));
        QDir().rmdir(dir);
        setError(tr("Could not move %1 %2 the locked category").arg(item.originalName, toVault ? tr("into") : tr("out of")));
        return false;
    }
    FileService::removeManaged(baseDir(item), item.storedName);
    m_model.upsert(moved);
    return true;
}

// Names stay unique on the shelf: a second "report.pdf" becomes "report (1).pdf".
QString NotchController::freeName(const QString &name, const QString &exceptId) const
{
    return FileService::uniqueAmong(name, [this, &exceptId](const QString &candidate) {
        for (const FileItem &item : m_model.items())
            if (item.id != exceptId && item.originalName.compare(candidate, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    });
}

FileItem NotchController::newItem(const QString &name, const QString &categoryId) const
{
    FileItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.originalName = freeName(FileService::safeFileName(name)); // a category called "a/b" must not become a folder in the zip's name
    item.storedName = item.id + u'/' + item.originalName;
    item.addedAt = QDateTime::currentDateTimeUtc();
    item.status = Status::Copying;
    item.categoryId = categoryId;
    return item;
}

namespace {
// What a worker makes of a drop: the files (a folder's files with its name), each looked at once, and what could not be used.
NotchController::Prepared prepareDrop(const QStringList &paths, const QStringList &errors, const QString &filesCanon, const QString &vaultCanon,
                                      const std::atomic_bool *cancel)
{
    NotchController::Prepared out;
    out.errors = errors;
    QMimeDatabase mimes;
    const auto add = [&](const QString &path, const QString &folder, bool fromFolder) {
        const QFileInfo fi(path);
        if (const QString err = FileService::validateSource(path); !err.isEmpty()) {
            out.errors << fi.fileName() + QStringLiteral(": ") + err;
            return;
        }
        const QString canonical = fi.canonicalFilePath();
        if (canonical.startsWith(filesCanon + u'/') || (!vaultCanon.isEmpty() && canonical.startsWith(vaultCanon + u'/')))
            return; // dropped back from Notch itself
        out.items.append({fi.absoluteFilePath(), fi.fileName(), mimes.mimeTypeForFile(fi).name(), folder, fromFolder, fi.size()});
    };
    for (const QString &path : paths) {
        if (cancel && cancel->load())
            break;
        if (!QFileInfo(path).isDir()) {
            add(path, {}, false);
            continue;
        }
        bool truncated = false;
        const auto entries = FileService::expandFolder(path, MaxFolderFiles, &truncated, cancel);
        if (truncated)
            out.errors << NotchController::tr("%1: only the first %2 files were added").arg(QFileInfo(path).fileName()).arg(MaxFolderFiles);
        if (entries.isEmpty())
            out.errors << NotchController::tr("%1: folder has no files").arg(QFileInfo(path).fileName());
        for (const auto &e : entries) {
            if (cancel && cancel->load())
                break;
            add(e.path, e.category, true);
        }
    }
    return out;
}
}

void NotchController::addUrls(const QList<QUrl> &urls, const QString &categoryId)
{
    if (!m_ready)
        return;
    QStringList errors, paths;
    for (const QUrl &url : urls) {
        const QString scheme = url.scheme();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("data")) {
            startDownload(url, categoryId);
            continue;
        }
        if (!url.isLocalFile()) {
            errors << tr("%1: unsupported location").arg(url.toDisplayString());
            continue;
        }
        paths << url.toLocalFile();
    }
    if (paths.isEmpty()) {
        if (!errors.isEmpty())
            setError(errors.join(u'\n'));
        return;
    }
    // Listing a folder and looking at every file can take long (a big folder, a slow disk): a worker does it, and the panel
    // says so meanwhile.
    m_ingestCancel = false;
    ++m_preparing;
    progressTick();
    const QString filesCanon = QDir(m_filesDir).canonicalPath();
    const QString vaultCanon = m_vault.isUnlocked() ? QDir(m_vault.mountDir()).canonicalPath() : QString();
    auto *watcher = new QFutureWatcher<Prepared>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, categoryId] {
        Prepared r = watcher->result();
        watcher->deleteLater();
        --m_preparing;
        if (m_ingestCancel) // stopped while it was being listed
            r.items.clear();
        qint64 need = 0;
        for (const Prep &p : std::as_const(r.items))
            need += p.size;
        const QStorageInfo storage(m_filesDir);
        if (need > 0 && storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < need) {
            r.errors << tr("Not enough disk space: these files need %1 and %2 is free")
                            .arg(QLocale().formattedDataSize(need), QLocale().formattedDataSize(storage.bytesAvailable()));
            r.items.clear();
        }
        if (!r.errors.isEmpty())
            setError(r.errors.join(u'\n'));
        if (!r.items.isEmpty()) {
            m_ingest.append({r.items, categoryId, 0});
            ingestStep();
        } else {
            progressTick();
        }
    });
    watcher->setFuture(QtConcurrent::run(&m_ingestPool, [paths, errors, filesCanon, vaultCanon, cancel = &m_ingestCancel] {
        return prepareDrop(paths, errors, filesCanon, vaultCanon, cancel);
    }));
}

// A few files at a time go on the shelf (a row each), so the panel keeps drawing while a thousand arrive.
void NotchController::ingestStep()
{
    constexpr int perStep = 10;
    QStringList errors;
    int n = 0;
    while (!m_ingest.isEmpty() && n < perStep) {
        Ingest &batch = m_ingest.first();
        if (batch.next >= batch.items.size()) {
            m_ingest.removeFirst();
            continue;
        }
        ingestOne(batch.items.at(batch.next++), batch.categoryId, errors);
        ++n;
    }
    if (!errors.isEmpty())
        setError(errors.join(u'\n'));
    if (!m_ingest.isEmpty())
        m_ingestTimer.start(8);
    else
        progressTick();
}

void NotchController::ingestOne(const Prep &p, const QString &categoryId, QStringList &errors)
{
    FileItem item = newItem(p.name, p.fromFolder ? createCategory(p.folder) : categoryId);
    item.mimeType = p.mime;
    item.categoryId = ruledCategory(item.categoryId, item.mimeType);
    item.sizeBytes = p.size;
    item.sourcePath = p.path;
    if (QString why; !assignStorage(item, &why)) {
        errors << item.originalName + QStringLiteral(": ") + why;
        return;
    }
    if (!m_repo.insert(item)) {
        errors << item.originalName + QStringLiteral(": ") + m_repo.errorString();
        return;
    }
    m_model.upsert(item);
    startCopy(item);
}

void NotchController::addDrop(const QList<QUrl> &urls, const QString &html, const QString &categoryId)
{
    if (m_ready && !urls.isEmpty())
        emit sound(QStringLiteral("drop"));
    QList<QUrl> clean;
    for (const QUrl &url : urls)
        if (url.isValid() && !url.isRelative())
            clean << url;
    const bool fromWeb = !clean.isEmpty()
                         && std::none_of(clean.cbegin(), clean.cend(), [](const QUrl &u) { return u.isLocalFile(); });
    if (fromWeb) {
        if (const QUrl image = FileService::imageFromHtml(html, clean.first()); image.isValid()) {
            addUrls({image}, categoryId);
            return;
        }
    }
    addUrls(clean, categoryId);
}

void NotchController::addLocalFile(const QString &path, const QString &categoryId, QStringList &errors)
{
    const QFileInfo fi(path);
    if (const QString err = FileService::validateSource(path); !err.isEmpty()) {
        errors << fi.fileName() + QStringLiteral(": ") + err;
        return;
    }
    if (fi.canonicalFilePath().startsWith(QDir(m_filesDir).canonicalPath() + u'/')
        || (m_vault.isUnlocked() && fi.canonicalFilePath().startsWith(QDir(m_vault.mountDir()).canonicalPath() + u'/')))
        return; // dropped back from Notch itself

    FileItem item = newItem(fi.fileName(), categoryId);
    item.mimeType = QMimeDatabase().mimeTypeForFile(fi).name();
    item.categoryId = ruledCategory(item.categoryId, item.mimeType);
    item.sizeBytes = fi.size();
    item.sourcePath = fi.absoluteFilePath();
    if (QString why; !assignStorage(item, &why)) {
        errors << item.originalName + QStringLiteral(": ") + why;
        return;
    }
    if (!m_repo.insert(item)) {
        errors << item.originalName + QStringLiteral(": ") + m_repo.errorString();
        return;
    }
    m_model.upsert(item);
    startCopy(item);
}

void NotchController::startCopy(const FileItem &item)
{
    auto *watcher = new QFutureWatcher<FileService::CopyResult>(this);
    ++m_copying;
    emit copyingChanged();
    Transfer t;
    t.done = std::make_shared<std::atomic<qint64>>(0);
    t.cancel = std::make_shared<std::atomic_bool>(false);
    t.total = item.sizeBytes;
    t.title = item.originalName;
    beginTransfer(item.id, t);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, item] {
        const FileService::CopyResult r = watcher->result();
        watcher->deleteLater();
        --m_copying;
        emit copyingChanged();
        endTransfer(item.id);
        if (r.ok && m_repo.markAvailable(item.id, r.bytes)) {
            FileItem done = item;
            done.status = Status::Available;
            done.sizeBytes = r.bytes;
            m_model.finishCopy(done);
            return;
        }
        const QString reason = r.ok ? m_repo.errorString() : r.error;
        if (r.ok)
            FileService::removeManaged(baseDir(item), item.storedName);
        m_repo.remove(item.id);
        m_model.removeId(item.id);
        if (reason != QLatin1String("cancelled"))
            setError(item.originalName + QStringLiteral(": ") + reason);
        expireEmptyCategories();
    });
    const QString src = item.sourcePath, dir = baseDir(item) + u'/' + item.id, name = item.originalName;
    watcher->setFuture(QtConcurrent::run(&m_copyPool, [src, dir, name, done = t.done, cancel = t.cancel] {
        return FileService::copyInto(src, dir, name, done.get(), cancel.get());
    }));
}

void NotchController::beginTransfer(const QString &id, const Transfer &t)
{
    m_transfers.insert(id, t);
    m_xferTotal += t.total;
    if (!t.zip && t.verb.isEmpty())
        ++m_xferFiles;
    if (!m_progressTimer.isActive()) {
        m_lastDone = m_xferBase;
        m_lastTickMs = QDateTime::currentMSecsSinceEpoch();
        m_rate = 0;
        m_progressTimer.start();
    }
    progressTick();
}

void NotchController::endTransfer(const QString &id)
{
    const auto it = m_transfers.constFind(id);
    if (it == m_transfers.cend())
        return;
    m_xferBase += it->total; // counted as done, so the bar never goes back when one is cancelled or fails
    if (!it->zip && it->verb.isEmpty())
        ++m_xferFinished;
    m_transfers.erase(it);
    m_model.setCopyPercent(id, -1);
    progressTick();
}

// A few times a second: how far everything is, in words and as a fraction, and each file's own percent. (Not once per megabyte.)
void NotchController::progressTick()
{
    if (m_transfers.isEmpty() && m_ingest.isEmpty() && m_preparing == 0) {
        m_progressTimer.stop();
        m_xferBase = m_xferTotal = m_lastDone = 0;
        m_xferFiles = m_xferFinished = 0;
        m_ingestCancel = false;
        if (!m_copyLabel.isEmpty() || m_copyFraction >= 0) {
            m_copyLabel.clear();
            m_copyDetail.clear();
            m_copyFraction = -1;
            emit copyProgressChanged();
        }
        return;
    }
    qint64 done = m_xferBase;
    bool zipOnly = true; // nothing but jobs named in the pill (a zip, a cloud upload or download)
    QString zipTitle, jobVerb;
    int jobs = 0;
    for (auto it = m_transfers.cbegin(); it != m_transfers.cend(); ++it) {
        const qint64 d = it->done->load();
        done += d;
        if (it->zip || !it->verb.isEmpty()) {
            zipTitle = it->title;
            jobVerb = it->zip ? tr("Zipping") : it->verb;
            ++jobs;
        } else {
            zipOnly = false;
            m_model.setCopyPercent(it.key(), it->total > 0 ? int(qMin<qint64>(99, d * 100 / it->total)) : -1);
        }
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastTickMs >= 200) {
        const double rate = double(done - m_lastDone) * 1000.0 / double(now - m_lastTickMs);
        m_rate = m_rate <= 0 ? rate : m_rate * 0.7 + rate * 0.3;
        m_lastDone = done;
        m_lastTickMs = now;
    }
    QString label, detail;
    double fraction = m_xferTotal > 0 ? qBound(0.0, double(done) / double(m_xferTotal), 1.0) : -1;
    if (m_transfers.isEmpty()) {
        label = tr("Reading files");
        fraction = -1;
    } else {
        const int files = m_xferFiles;
        label = zipOnly ? (jobs > 1 ? tr("%1 %2 files").arg(jobVerb).arg(jobs) : tr("%1 %2").arg(jobVerb, zipTitle))
                        : tr("Copying %1 of %2").arg(qMin(m_xferFinished + 1, qMax(1, files))).arg(qMax(1, files));
        if (m_xferTotal > 0) {
            detail = tr("%1 of %2").arg(QLocale().formattedDataSize(done), QLocale().formattedDataSize(m_xferTotal));
            if (m_rate > 1024 && m_xferTotal > done) {
                const qint64 secs = qint64(double(m_xferTotal - done) / m_rate);
                detail += secs >= 3600 ? tr(" · %1 h %2 min left").arg(secs / 3600).arg(secs % 3600 / 60)
                        : secs >= 60   ? tr(" · %1 min left").arg(secs / 60 + 1)
                                       : tr(" · %1 s left").arg(qMax<qint64>(1, secs));
            }
        }
    }
    if (label != m_copyLabel || detail != m_copyDetail || !qFuzzyCompare(fraction + 2, m_copyFraction + 2)) {
        m_copyLabel = label;
        m_copyDetail = detail;
        m_copyFraction = fraction;
        emit copyProgressChanged();
    }
}

void NotchController::cancelCopies()
{
    m_ingestCancel = true; // the listing of a folder
    m_ingest.clear();      // and what was listed but not on the shelf yet
    for (const Transfer &t : std::as_const(m_transfers))
        t.cancel->store(true);
    progressTick();
}

void NotchController::cancelCopy(const QString &id)
{
    if (const auto it = m_transfers.constFind(id); it != m_transfers.cend())
        it->cancel->store(true);
}

// Streams a URL into managed storage with the same lifecycle as a copy (row "copying" ->
// tmp file -> rename -> "available"). An HTML response means the user dragged a page link:
// the download is dropped and a link shortcut is stored instead.
void NotchController::startDownload(const QUrl &url, const QString &categoryId)
{
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = m_net->get(request);
    ++m_copying;
    emit copyingChanged();

    struct Download {
        FileItem item;
        std::unique_ptr<QFile> out;
        bool begun = false;
        bool isPage = false;
        QString error;
    };
    auto d = std::make_shared<Download>();

    auto begin = [this, reply, d, categoryId] {
        d->begun = true;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 400) {
            d->error = tr("server answered %1").arg(status);
            reply->abort();
            return;
        }
        const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().section(u';', 0, 0).trimmed();
        if (type == QLatin1String("text/html") || type == QLatin1String("application/xhtml+xml")) {
            d->isPage = true;
            reply->abort();
            return;
        }
        FileItem item = newItem(FileService::downloadName(reply->url(), type), categoryId);
        const QMimeType mt = QMimeDatabase().mimeTypeForName(type);
        item.mimeType = mt.isValid() && !mt.isDefault() ? mt.name()
                                                        : QMimeDatabase().mimeTypeForFile(item.originalName, QMimeDatabase::MatchExtension).name();
        item.categoryId = ruledCategory(item.categoryId, item.mimeType);
        item.sizeBytes = qMax<qint64>(0, reply->header(QNetworkRequest::ContentLengthHeader).toLongLong());
        item.sourcePath = reply->url().scheme() == QLatin1String("data") ? QStringLiteral("data URL") : reply->url().toDisplayString();
        if (QString why; !assignStorage(item, &why)) {
            d->error = why;
            reply->abort();
            return;
        }
        const QString dir = baseDir(item) + u'/' + item.id;
        const QStorageInfo storage(m_filesDir);
        if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < item.sizeBytes) {
            d->error = tr("not enough disk space");
            reply->abort();
            return;
        }
        auto out = std::make_unique<QFile>(dir + u'/' + FileService::TmpName);
        if (!QDir().mkpath(dir) || !out->open(QIODevice::WriteOnly) || !m_repo.insert(item)) {
            d->error = tr("cannot create file");
            out.reset();
            QDir().rmdir(dir);
            reply->abort();
            return;
        }
        d->item = item;
        d->out = std::move(out);
        m_model.upsert(item);
    };

    connect(reply, &QNetworkReply::readyRead, this, [reply, d, begin] {
        if (!d->begun)
            begin();
        if (d->out && d->out->write(reply->readAll()) < 0) {
            d->error = d->out->error() == QFileDevice::ResourceError ? tr("disk full") : d->out->errorString();
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, d, begin, url, categoryId] {
        reply->deleteLater();
        --m_copying;
        emit copyingChanged();
        if (!d->begun && reply->error() == QNetworkReply::NoError)
            begin();
        if (d->isPage) {
            addLink(url, categoryId);
            return;
        }
        bool ok = d->out && reply->error() == QNetworkReply::NoError && d->error.isEmpty();
        qint64 bytes = 0;
        if (ok) {
            d->out->write(reply->readAll());
            ok = d->out->flush() && FileService::syncFile(*d->out);
            bytes = d->out->size();
            d->out->close();
            ok = ok && QFile::rename(d->out->fileName(), pathOf(d->item))
                 && m_repo.markAvailable(d->item.id, bytes);
        }
        if (ok) {
            d->item.status = Status::Available;
            d->item.sizeBytes = bytes;
            m_model.upsert(d->item);
            return;
        }
        if (d->out) {
            d->out->close();
            d->out->remove();
            FileService::removeManaged(baseDir(d->item), d->item.storedName);
            m_repo.remove(d->item.id);
            m_model.removeId(d->item.id);
        }
        const QString reason = !d->error.isEmpty() ? d->error : reply->errorString();
        setError(url.toDisplayString(QUrl::RemoveQuery).left(80) + QStringLiteral(": ") + reason);
    });
}

void NotchController::addLink(const QUrl &url, const QString &categoryId)
{
    QString base = url.host();
    if (!url.fileName().isEmpty())
        base += QStringLiteral(" - ") + url.fileName();
    addBytes(FileService::safeFileName(base, QStringLiteral("link")) + QStringLiteral(".desktop"),
             QStringLiteral("application/x-desktop"), FileService::linkFile(url), url.toDisplayString(), categoryId);
}

bool NotchController::addBytes(const QString &name, const QString &mimeType, const QByteArray &bytes,
                               const QString &source, const QString &categoryId, QString *idOut)
{
    FileItem item = newItem(FileService::safeFileName(name), categoryId);
    item.mimeType = mimeType;
    item.categoryId = ruledCategory(item.categoryId, mimeType);
    if (QString why; !assignStorage(item, &why)) {
        setError(item.originalName + QStringLiteral(": ") + why);
        return false;
    }
    item.sizeBytes = bytes.size();
    item.sourcePath = source;
    item.status = Status::Available;
    if (!FileService::writeFile(baseDir(item) + u'/' + item.id, item.originalName, bytes) || !m_repo.insert(item)) {
        FileService::removeManaged(baseDir(item), item.storedName);
        setError(tr("%1: cannot save").arg(item.originalName));
        return false;
    }
    m_model.upsert(item);
    if (idOut)
        *idOut = item.id;
    return true;
}

// Keeps what's on the clipboard in its own format: files are copied, image bytes are stored
// as-is (png stays png), a lone URL is fetched like a browser drop, other text becomes a file
// whose extension follows the editor's language or the content.
void NotchController::pasteClipboard(const QString &categoryId)
{
    const QMimeData *md = QGuiApplication::clipboard()->mimeData();
    if (!md)
        return;
    emit sound(QStringLiteral("drop"));
    const QList<QUrl> urls = md->urls();
    if (!urls.isEmpty() && std::all_of(urls.cbegin(), urls.cend(), [](const QUrl &u) { return u.isLocalFile(); })) {
        addUrls(urls, categoryId);
        return;
    }
    const QString stamp = QStringLiteral("Clipboard ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
    for (const QString &format : md->formats()) {
        const QMimeType mt = QMimeDatabase().mimeTypeForName(format);
        if (!format.startsWith(QLatin1String("image/")) || !mt.isValid() || mt.preferredSuffix().isEmpty())
            continue;
        if (const QByteArray bytes = md->data(format); !bytes.isEmpty()) {
            addBytes(stamp + u'.' + mt.preferredSuffix(), mt.name(), bytes, tr("Clipboard"), categoryId);
            return;
        }
    }
    if (md->hasImage()) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (qvariant_cast<QImage>(md->imageData()).save(&buffer, "PNG")) {
            addBytes(stamp + QStringLiteral(".png"), QStringLiteral("image/png"), png, tr("Clipboard"), categoryId);
            return;
        }
    }
    if (!urls.isEmpty()) {
        addDrop(urls, md->html(), categoryId);
        return;
    }
    const QString text = md->text();
    const QUrl asUrl(text.trimmed(), QUrl::StrictMode);
    if (!text.trimmed().contains(u' ') && (asUrl.scheme() == QLatin1String("http") || asUrl.scheme() == QLatin1String("https"))) {
        addUrls({asUrl}, categoryId);
        return;
    }
    if (!text.isEmpty()) {
        const QString language = QJsonDocument::fromJson(md->data(QStringLiteral("vscode-editor-data")))
                                     .object().value(QStringLiteral("mode")).toString();
        const QString ext = FileService::textExtension(text, language);
        const QString mime = QMimeDatabase().mimeTypeForFile(QStringLiteral("x.") + ext, QMimeDatabase::MatchExtension).name();
        addBytes(stamp + u'.' + ext, mime, text.toUtf8(), tr("Clipboard"), categoryId);
        return;
    }
    setError(tr("The clipboard has nothing Olyzano Notch can keep."));
}

void NotchController::keepBytes(const QString &name, const QString &mimeType, const QByteArray &bytes, const QString &source)
{
    addBytes(name, mimeType, bytes, source, m_categoryFilter);
}

QString NotchController::keepBytesId(const QString &name, const QString &mimeType, const QByteArray &bytes, const QString &source)
{
    QString id;
    return addBytes(name, mimeType, bytes, source, m_categoryFilter, &id) ? id : QString();
}

void NotchController::addTextNote(const QString &text, const QString &categoryId)
{
    if (text.trimmed().isEmpty())
        return;
    emit sound(QStringLiteral("drop"));
    QString title;
    for (const QString &line : text.split(u'\n'))
        if (!line.trimmed().isEmpty()) {
            title = line.simplified().left(40);
            break;
        }
    const QString ext = FileService::textExtension(text);
    // Prose is named by its first line; code/JSON make poor titles, so those get a dated name.
    const QString base = ext == QLatin1String("txt")
                             ? FileService::safeFileName(title, QStringLiteral("Note"))
                             : QStringLiteral("Snippet ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm"));
    const QString name = base + u'.' + ext;
    const QString mime = QMimeDatabase().mimeTypeForFile(QStringLiteral("x.") + ext, QMimeDatabase::MatchExtension).name();
    if (addBytes(name, mime, text.toUtf8(), tr("Dropped text"), categoryId))
        emit notice(tr("Text saved as a note"), name, QStringLiteral("text-plain"), QString());
}

// Files as files (file managers and chat apps attach them). A single image also goes on the
// clipboard as image data, so pasting into a chat sends the picture itself.
void NotchController::copyFiles(const QStringList &ids)
{
    QList<QUrl> urls;
    QByteArray gnome("copy");
    const FileItem *single = nullptr;
    for (const QString &id : ids) {
        const FileItem *item = available(id);
        if (!item)
            continue;
        const QUrl url = QUrl::fromLocalFile(pathOf(*item));
        urls << url;
        gnome += '\n' + url.toEncoded();
        single = item;
    }
    if (urls.isEmpty())
        return;
    auto *md = new QMimeData;
    md->setUrls(urls);
    md->setData(QStringLiteral("x-special/gnome-copied-files"), gnome); // GTK file managers
    if (urls.size() == 1 && single->mimeType.startsWith(QLatin1String("image/"))) {
        const QString path = urls.first().toLocalFile();
        if (single->mimeType == QLatin1String("image/png") || single->mimeType == QLatin1String("image/svg+xml")) {
            QFile f(path);
            if (f.size() < (64 << 20) && f.open(QIODevice::ReadOnly))
                md->setData(single->mimeType, f.readAll()); // original bytes
        } else if (const QImage image(path); !image.isNull()) {
            md->setImageData(image); // Qt offers it as png/jpeg/bmp…
        }
    }
    QGuiApplication::clipboard()->setMimeData(md);
}

void NotchController::copyContents(const QString &id)
{
    const FileItem *item = available(id);
    if (!item)
        return;
    QFile f(pathOf(*item));
    constexpr qint64 Limit = 16 << 20;
    if (f.size() > Limit || !f.open(QIODevice::ReadOnly)) {
        setError(tr("%1: too large to copy as text").arg(item->originalName));
        return;
    }
    QGuiApplication::clipboard()->setText(QString::fromUtf8(f.readAll()));
}

void NotchController::copyPaths(const QStringList &ids)
{
    QStringList paths;
    for (const QString &id : ids)
        if (const FileItem *item = available(id))
            paths << pathOf(*item);
    if (!paths.isEmpty())
        QGuiApplication::clipboard()->setText(paths.join(u'\n'));
}

void NotchController::setPinned(const QStringList &ids, bool pinned)
{
    runBatches(pinned ? tr("Keeping") : tr("Releasing"), ids, [this, pinned](const QStringList &chunk) {
        QStringList done;
        for (const QString &id : chunk)
            if (m_repo.setPinned(id, pinned))
                done << id;
        m_model.setPinned(done, pinned);
    }, [] {});
}

void NotchController::open(const QString &id)
{
    const FileItem *item = available(id);
    if (!item)
        return;
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(pathOf(*item))))
        setError(tr("%1: no application could open this file").arg(item->originalName));
}

void NotchController::remove(const QString &id)
{
    removeItems({id});
}

namespace {
constexpr int FreshCategorySeconds = 30;
constexpr int kBatch = 10;       // items per step of a big remove / restore / delete
constexpr int kBatchPauseMs = 8; // the panel gets this long between steps to draw and answer
}

bool NotchController::runBatches(const QString &verb, const QStringList &ids, std::function<void(const QStringList &)> step,
                                 std::function<void()> finish)
{
    if (ids.isEmpty())
        return true;
    if (!m_job.verb.isEmpty()) {
        setError(tr("Wait for the current job to finish"));
        return false;
    }
    if (ids.size() <= kBatch) { // the usual case: done at once
        step(ids);
        finish();
        return true;
    }
    m_job = {verb, ids, 0, std::move(step), std::move(finish)};
    m_batchLabel = tr("%1 0 of %2").arg(verb).arg(ids.size());
    emit batchLabelChanged();
    nextBatch();
    return true;
}

void NotchController::nextBatch()
{
    const QStringList chunk = m_job.ids.mid(m_job.done, kBatch);
    m_job.step(chunk);
    m_job.done += chunk.size();
    if (m_job.done >= m_job.ids.size()) {
        const auto finish = std::move(m_job.finish);
        m_job = {};
        m_batchLabel.clear();
        emit batchLabelChanged();
        finish();
        return;
    }
    m_batchLabel = tr("%1 %2 of %3").arg(m_job.verb).arg(m_job.done).arg(m_job.ids.size());
    emit batchLabelChanged();
    m_batchTimer.start(kBatchPauseMs);
}

void NotchController::removeItems(const QStringList &ids)
{
    if (!m_job.verb.isEmpty()) {
        setError(tr("Wait for the current job to finish"));
        return;
    }
    m_trashed.clear();
    runBatches(tr("Removing"), ids, [this](const QStringList &chunk) { trashBatch(chunk); }, [this] {
        if (!m_trashed.isEmpty()) {
            m_lastRemoved = m_trashed;
            m_trashed.clear();
            m_trashTimer.start(); // the Undo toast's lifetime
            emit undoCountChanged();
        }
        emit countChanged();
    });
}

void NotchController::trashBatch(const QStringList &chunk)
{
    QStringList trash, vault;
    for (const QString &id : chunk) {
        const FileItem *item = m_model.find(id);
        if (!item)
            continue;
        if (item->vault) // locked files skip Recently removed: no plain copy should linger there
            vault << id;
        else if (item->status != Status::Copying && !item->removedAt.isValid())
            trash << id;
    }
    if (!vault.isEmpty())
        deleteBatch(vault);
    if (trash.isEmpty())
        return;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!m_repo.setRemovedMany(trash, now)) {
        setError(tr("Database error: %1").arg(m_repo.errorString()));
        return;
    }
    m_model.setRemoved(QSet<QString>(trash.begin(), trash.end()), now);
    m_trashed += trash;
}

void NotchController::undoRemove()
{
    restoreItems(m_lastRemoved);
}

void NotchController::restoreItems(const QStringList &ids)
{
    runBatches(tr("Restoring"), ids, [this](const QStringList &chunk) { restoreBatch(chunk); }, [this] {
        if (!m_lastRemoved.isEmpty()) {
            m_lastRemoved.clear();
            m_trashTimer.stop();
            emit undoCountChanged();
        }
        emit countChanged();
    });
}

void NotchController::restoreBatch(const QStringList &chunk)
{
    if (!m_repo.setRemovedMany(chunk, QDateTime())) {
        setError(tr("Database error: %1").arg(m_repo.errorString()));
        return;
    }
    m_model.setRemoved(QSet<QString>(chunk.begin(), chunk.end()), QDateTime());
}

void NotchController::deleteForever(const QStringList &ids)
{
    runBatches(tr("Deleting"), ids, [this](const QStringList &chunk) { deleteBatch(chunk); }, [this] {
        emit countChanged();
        expireEmptyCategories();
    });
}

void NotchController::emptyRecentlyRemoved()
{
    QStringList ids;
    for (const FileItem &item : m_model.items())
        if (item.removedAt.isValid())
            ids << item.id;
    deleteForever(ids);
}

// For good: marked for cleanup (so a crash in the middle is finished on the next start), gone from the shelf, the stored copies
// deleted, then the rows. Two transactions for the whole step.
void NotchController::deleteBatch(const QStringList &chunk)
{
    QList<FileItem> items;
    QStringList ids;
    for (const QString &id : chunk)
        if (const FileItem *item = m_model.find(id); item && item->status != Status::Copying) {
            items << *item;
            ids << id;
        }
    if (items.isEmpty())
        return;
    if (!m_repo.setStatusMany(ids, Status::PendingCleanup)) {
        setError(tr("Database error: %1").arg(m_repo.errorString()));
        return;
    }
    m_model.removeIds(QSet<QString>(ids.begin(), ids.end()));
    QStringList gone;
    for (const FileItem &item : items) {
        m_indexed.remove(item.id);
        if (FileService::removeManaged(baseDir(item), item.storedName))
            gone << item.id;
        else
            setError(tr("Could not delete the stored copy yet; cleanup will retry."));
    }
    m_repo.removeMany(gone);
}

void NotchController::setDragging(const QStringList &ids, bool dragging)
{
    for (const QString &id : ids) {
        if (dragging)
            m_dragging.insert(id);
        else
            m_dragging.remove(id);
    }
}

QString NotchController::uriList(const QStringList &ids) const
{
    QStringList uris;
    for (const QString &id : ids) {
        const FileItem *item = available(id);
        if (item)
            uris << QString::fromUtf8(QUrl::fromLocalFile(pathOf(*item)).toEncoded());
    }
    return uris.join(QStringLiteral("\r\n"));
}

QStringList NotchController::visibleIds() const
{
    QStringList ids;
    for (int row = 0; row < m_proxy.rowCount(); ++row)
        ids << m_proxy.index(row, 0).data(FileItemModel::IdRole).toString();
    return ids;
}

void NotchController::refreshCategories()
{
    QHash<QString, int> counts;
    for (const FileItem &item : m_model.items())
        if (!item.removedAt.isValid())
            ++counts[item.categoryId];
    const QString shots = m_settings.value(QStringLiteral("screenshotCategory")).toString();
    const QString downloads = m_settings.value(QStringLiteral("downloadCategory")).toString();
    const QString phone = m_settings.value(QStringLiteral("kdeconnectCategory")).toString();
    QVariantList out;
    for (const Category &c : std::as_const(m_categoryList)) {
        const QString special = c.id == shots       ? QStringLiteral("screenshots")
                                : c.id == downloads ? QStringLiteral("downloads")
                                : c.id == phone     ? QStringLiteral("kdeconnect")
                                                    : QString();
        const QVariantMap entry{{QStringLiteral("id"), c.id}, {QStringLiteral("name"), c.name},
                                {QStringLiteral("count"), counts.value(c.id)}, {QStringLiteral("special"), special},
                                {QStringLiteral("locked"), isLockedCategory(c.id)}};
        if (special == QLatin1String("screenshots"))
            out.prepend(entry); // Screenshots always sits right after "All"
        else
            out << entry;
    }
    if (out == m_categories)
        return;
    m_categories = out;
    emit categoriesChanged();
}

void NotchController::reloadCategories()
{
    m_categoryList = m_repo.categories();
    refreshCategories();
}

void NotchController::setCategoryFilter(const QString &categoryId)
{
    if (categoryId == m_categoryFilter)
        return;
    m_categoryFilter = categoryId;
    m_proxy.setCategory(categoryId);
    emit categoryFilterChanged();
}

QString NotchController::createCategory(const QString &name)
{
    const QString clean = name.simplified().left(60);
    if (clean.isEmpty())
        return {};
    if (const QString existing = m_repo.categoryIdByName(clean); !existing.isEmpty())
        return existing;
    const Category c{QUuid::createUuid().toString(QUuid::WithoutBraces), clean};
    if (!m_repo.insertCategory(c)) {
        setError(tr("Cannot create category: %1").arg(m_repo.errorString()));
        return {};
    }
    m_fresh.insert(c.id, QDateTime::currentDateTimeUtc());
    reloadCategories();
    return c.id;
}

void NotchController::renameCategory(const QString &id, const QString &name)
{
    const QString clean = name.simplified().left(60);
    if (clean.isEmpty())
        return;
    const QString existing = m_repo.categoryIdByName(clean);
    if (!existing.isEmpty() && existing != id) {
        setError(tr("A category named “%1” already exists.").arg(clean));
        return;
    }
    if (!m_repo.renameCategory(id, clean))
        setError(tr("Cannot rename category: %1").arg(m_repo.errorString()));
    reloadCategories();
}

void NotchController::deleteCategory(const QString &id)
{
    if (isLockedCategory(id)) {
        setError(tr("Remove the lock first (right-click the category)"));
        return;
    }
    if (!m_repo.deleteCategory(id)) {
        setError(tr("Cannot delete category: %1").arg(m_repo.errorString()));
        return;
    }
    m_model.clearCategory(id);
    if (m_categoryFilter == id)
        setCategoryFilter({});
    reloadCategories();
}

void NotchController::setCategory(const QStringList &ids, const QString &categoryId)
{
    const bool toVault = isLockedCategory(categoryId);
    if (toVault && !m_vault.isUnlocked()) {
        setError(tr("Unlock the category first"));
        return;
    }
    QStringList done;
    for (const QString &id : ids) {
        const FileItem *item = m_model.find(id);
        if (!item)
            continue;
        if (item->vault != toVault && (!m_vault.isUnlocked() || !relocate(*item, toVault)))
            continue; // a vault file can only leave while the vault is open
        if (m_repo.setCategory(id, categoryId))
            done << id;
    }
    m_model.setCategory(done, categoryId);
    if (done.size() != ids.size())
        setError(tr("Some files could not be moved: %1").arg(m_repo.errorString()));
    m_fresh.remove(categoryId); // it has its files now
    expireEmptyCategories();    // and the ones they left may be empty
}

void NotchController::runCleanup()
{
    for (const QString &id : CleanupService::purge(m_repo, m_filesDir, QDateTime::currentDateTimeUtc(), m_retentionDays, m_dragging))
        m_model.removeId(id);
    expireEmptyCategories();
}

// A category with no file in it goes away at once (its files are the point of it); files in Recently removed still count, they
// can come back. Kept: locked ones, those a "sort new files by type" rule files into, and one made a moment ago (it is made first,
// its files are put in next). The ones Notch makes itself (Screenshots, Downloads, Nearby, ...) come back when next needed.
void NotchController::expireEmptyCategories()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (auto it = m_fresh.begin(); it != m_fresh.end();)
        it = it.value().secsTo(now) > FreshCategorySeconds ? m_fresh.erase(it) : std::next(it);
    const QVariantMap rules = typeRules();
    QStringList keep;
    for (auto it = rules.cbegin(); it != rules.cend(); ++it)
        keep << it.value().toString();
    for (const QString &id : m_repo.staleCategories(now, now))
        if (!keep.contains(id) && !m_fresh.contains(id) && !isLockedCategory(id))
            deleteCategory(id);
}

void NotchController::setFilter(const QString &text)
{
    if (text == m_filter)
        return;
    m_filter = text;
    m_proxy.setText(text);
    if (text.trimmed().isEmpty()) {
        m_searchTimer.stop();
        m_proxy.setContentIds({});
    } else {
        m_searchTimer.start(); // the names match at once; the text inside files a moment after the last key
    }
    emit filterChanged();
}

void NotchController::setRetentionDays(int days)
{
    days = qBound(1, days, 365);
    if (days == m_retentionDays)
        return;
    m_retentionDays = days;
    m_settings.setValue(QStringLiteral("retentionDays"), days);
    m_model.setRetentionDays(days);
    emit retentionDaysChanged();
    if (m_ready)
        runCleanup();
}

bool NotchController::setStorageDir(const QUrl &folder)
{
    const QString dir = QDir::cleanPath(folder.toLocalFile());
    if (dir.isEmpty() || dir == m_filesDir)
        return false;
    if (m_model.rowCount() > 0 || m_copying > 0 || !m_repo.withStatus(Status::PendingCleanup).isEmpty()) {
        setError(tr("Empty the shelf before changing the storage folder."));
        return false;
    }
    if (!QDir().mkpath(dir) || !QFileInfo(dir).isWritable()) {
        setError(tr("Cannot write to %1").arg(dir));
        return false;
    }
    m_filesDir = dir;
    m_model.setFilesDir(dir);
    m_settings.setValue(QStringLiteral("storageDir"), dir);
    emit storageDirChanged();
    return true;
}

bool NotchController::autostart() const
{
#ifdef Q_OS_WIN
    return QSettings(RunKey, QSettings::NativeFormat).contains(QStringLiteral("Notch"));
#else
    return QFile::exists(autostartPath());
#endif
}

void NotchController::setAutostart(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings run(RunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue(QStringLiteral("Notch"), QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"'));
    else
        run.remove(QStringLiteral("Notch"));
    emit autostartChanged();
    return;
#endif
    const QString path = autostartPath();
    if (enabled) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setError(tr("Cannot write %1").arg(path));
            return;
        }
        f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Notch\nComment=Temporary file shelf\n"
                               "Exec=\"%1\"\nIcon=folder-download\n")
                    .arg(QCoreApplication::applicationFilePath())
                    .toUtf8());
    } else if (QFile::exists(path) && !QFile::remove(path)) {
        setError(tr("Cannot remove %1").arg(path));
        return;
    }
    emit autostartChanged();
}

QVariantMap NotchController::itemInfo(const QString &id) const
{
    QVariantMap info;
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const QModelIndex i = m_model.index(row);
        if (i.data(FileItemModel::IdRole).toString() != id)
            continue;
        for (const auto &[role, key] : {std::pair{FileItemModel::NameRole, "name"}, {FileItemModel::SizeTextRole, "sizeText"},
                                        {FileItemModel::AddedTextRole, "addedText"}, {FileItemModel::IconNameRole, "iconName"},
                                        {FileItemModel::IsTextRole, "isText"}, {FileItemModel::VaultRole, "vault"},
                                        {FileItemModel::SizeRole, "size"}})
            info.insert(QLatin1String(key), i.data(role));
    }
    return info;
}

QString NotchController::quickLookImage(const QString &id)
{
    const FileItem *item = available(id);
    if (!item)
        return {};
    const QString path = pathOf(*item);
    if (item->mimeType.startsWith(QLatin1String("image/")))
        return QUrl::fromLocalFile(path).toString();
    if (item->mimeType != QLatin1String("application/pdf"))
        return {};
    const QString cacheDir = scratchDir(item->vault) + QStringLiteral("/cache");
    const QString cache = cacheDir + u'/' + id + QStringLiteral(".png");
    if (!QFileInfo::exists(cache) || QFileInfo(cache).lastModified() < QFileInfo(path).lastModified()) {
        QDir().mkpath(cacheDir);
        // pdftoppm -singlefile writes <prefix>.png
        const int rc = QProcess::execute(QStringLiteral("pdftoppm"),
                                         {QStringLiteral("-f"), QStringLiteral("1"), QStringLiteral("-l"), QStringLiteral("1"),
                                          QStringLiteral("-r"), QStringLiteral("110"), QStringLiteral("-png"),
                                          QStringLiteral("-singlefile"), path, cache.chopped(4)});
        if (rc != 0)
            return {};
    }
    return QUrl::fromLocalFile(cache).toString();
}

QString NotchController::previewText(const QString &id) const
{
    const FileItem *item = available(id);
    if (!item)
        return {};
    QFile f(pathOf(*item));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    constexpr qint64 Limit = 64 * 1024;
    const QByteArray data = f.read(Limit);
    if (data.contains('\0'))
        return {};
    QString text = QString::fromUtf8(data);
    if (f.size() > Limit)
        text += QStringLiteral("\n…");
    return text;
}

void NotchController::setAlerts(bool enabled)
{
    if (!storeSetting(QStringLiteral("alerts"), enabled))
        return;
    emit alertsChanged();
}

void NotchController::setSortMode(const QString &mode)
{
    if (mode == sortMode())
        return;
    m_settings.setValue(QStringLiteral("sort"), mode);
    m_proxy.setSortMode(mode);
    emit sortModeChanged();
}

QVariantMap NotchController::typeRules() const
{
    QVariantMap rules;
    for (const QString &group : {QStringLiteral("pdf"), QStringLiteral("image"), QStringLiteral("video"), QStringLiteral("audio"),
                                 QStringLiteral("document"), QStringLiteral("archive"), QStringLiteral("text")})
        rules.insert(group, m_settings.value(QStringLiteral("typeRules/") + group).toString());
    return rules;
}

void NotchController::setTypeRule(const QString &group, const QString &categoryId)
{
    if (categoryId.isEmpty())
        m_settings.remove(QStringLiteral("typeRules/") + group);
    else
        m_settings.setValue(QStringLiteral("typeRules/") + group, categoryId);
    emit typeRulesChanged();
}

QString NotchController::ruledCategory(const QString &categoryId, const QString &mimeType)
{
    if (!categoryId.isEmpty())
        return categoryId;
    const QString group = FileService::typeGroup(mimeType);
    if (group.isEmpty())
        return {};
    const QString target = m_settings.value(QStringLiteral("typeRules/") + group).toString();
    if (isLockedCategory(target) && !m_vault.isUnlocked())
        return {}; // can't file into a closed vault
    for (const Category &c : std::as_const(m_categoryList)) // the category may have been deleted since
        if (c.id == target)
            return target;
    return {};
}

int NotchController::applyRulesToExisting()
{
    QHash<QString, QStringList> moves; // category -> ids
    for (const FileItem &item : m_model.items())
        if (item.categoryId.isEmpty() && !item.removedAt.isValid())
            if (const QString target = ruledCategory({}, item.mimeType); !target.isEmpty())
                moves[target] << item.id;
    int moved = 0;
    for (auto it = moves.cbegin(); it != moves.cend(); ++it) {
        setCategory(it.value(), it.key());
        moved += it.value().size();
    }
    emit notice(moved == 1 ? tr("Sorted 1 file") : tr("Sorted %1 files").arg(moved), QString(), QStringLiteral("view-sort"), QString());
    return moved;
}

void NotchController::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

bool NotchController::rtssHooked() const
{
#ifdef Q_OS_WIN
    return WinPrivacy::rtssHooked();
#else
    return false;
#endif
}

QString NotchController::exePath() const
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

void NotchController::addColor(const QString &hex)
{
    QStringList colors = recentColors();
    colors.removeAll(hex);
    colors.prepend(hex);
    m_settings.setValue(QStringLiteral("colors"), colors.mid(0, 60));
    copyText(hex);
    emit recentColorsChanged();
    emit notice(tr("Color copied"), hex, QStringLiteral("color"), QString()); // "color" icon: QML shows a swatch
}

void NotchController::removeColor(const QString &hex)
{
    QStringList colors = recentColors();
    colors.removeAll(hex);
    m_settings.setValue(QStringLiteral("colors"), colors);
    emit recentColorsChanged();
}

void NotchController::setThemeMode(const QString &mode)
{
    if (!storeSetting(QStringLiteral("theme"), mode))
        return;
    emit themeModeChanged();
}

void NotchController::setUiStyle(const QString &style)
{
    if (!storeSetting(QStringLiteral("style"), style))
        return;
    emit uiStyleChanged();
}

void NotchController::setAutoScreenshots(bool enabled)
{
    if (!storeSetting(QStringLiteral("autoScreenshots"), enabled))
        return;
    updateWatchers();
    emit autoScreenshotsChanged();
}

void NotchController::setAutoDownloads(bool enabled)
{
    if (!storeSetting(QStringLiteral("autoDownloads"), enabled))
        return;
    updateWatchers();
    emit autoDownloadsChanged();
}

QString NotchController::screenshotDir() const
{
    if (const QString custom = m_settings.value(QStringLiteral("screenshotDir")).toString(); !custom.isEmpty())
        return custom;
    // Spectacle's own setting when the user changed it, else its default.
    const QSettings spectacle(spectacleConfig(), QSettings::IniFormat);
    const QUrl location(spectacle.value(QStringLiteral("ImageSave/imageSaveLocation")).toString());
    if (location.isLocalFile())
        return location.toLocalFile();
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Screenshots");
}

void NotchController::updateWatchers()
{
    if (!m_ready)
        return;
    m_screenshots.watch(autoScreenshots() ? screenshotDir() : QString());
    m_downloads.watch(autoDownloads() ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) : QString());
    qCInfo(lcNotch) << "Watching screenshots:" << (m_screenshots.dir().isEmpty() ? QStringLiteral("off") : m_screenshots.dir())
                    << "downloads:" << (m_downloads.dir().isEmpty() ? QStringLiteral("off") : m_downloads.dir());
}

QString NotchController::autoCategory(const QString &settingKey, const QString &name)
{
    const QString remembered = m_settings.value(settingKey).toString();
    for (const Category &c : std::as_const(m_categoryList))
        if (c.id == remembered)
            return remembered; // may have been renamed: still the target
    const QString id = createCategory(name);
    m_settings.setValue(settingKey, id);
    refreshCategories(); // its "special" mark (📷, ⬇, 📱) depends on the setting just stored
    return id;
}

bool NotchController::claimAutoAdd(const QString &path)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_autoAdded.begin(); it != m_autoAdded.end();)
        it = now - it.value() > 120000 ? m_autoAdded.erase(it) : std::next(it);
    const QString key = QFileInfo(path).canonicalFilePath();
    if (m_autoAdded.contains(key))
        return false;
    m_autoAdded.insert(key, now);
    return true;
}

void NotchController::setAutoKdeConnect(bool enabled)
{
    if (!storeSetting(QStringLiteral("autoKdeConnect"), enabled))
        return;
    emit autoKdeConnectChanged();
}

QString NotchController::readText(const QString &id)
{
    const FileItem *item = available(id);
    if (!item)
        return {};
    QFile f(pathOf(*item));
    constexpr qint64 Limit = 1 << 20;
    if (f.size() > Limit || !f.open(QIODevice::ReadOnly)) {
        setError(tr("%1 is too large to edit here").arg(item->originalName));
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

bool NotchController::saveNote(const QString &id, const QString &name, const QString &text)
{
    const QString clean = FileService::safeFileName(name.trimmed(), QStringLiteral("Note.txt"));
    const QByteArray bytes = text.toUtf8();
    if (id.isEmpty()) {
        const QString mime = QMimeDatabase().mimeTypeForFile(clean, QMimeDatabase::MatchExtension).name();
        return addBytes(clean, mime, bytes, tr("Note"), m_categoryFilter);
    }
    const FileItem *item = available(id);
    if (!item)
        return false;
    if (!FileService::writeFile(baseDir(*item) + u'/' + id, item->originalName, bytes) || !m_repo.markAvailable(id, bytes.size())) {
        setError(tr("Could not save %1").arg(item->originalName));
        return false;
    }
    FileItem saved = *item;
    saved.sizeBytes = bytes.size();
    m_model.upsert(saved);
    m_indexed.remove(id); // its text changed: indexed again
    scheduleIndexing();
    renameItem(id, clean); // no-op when the name is unchanged
    return true;
}

// Which files have their text searched: plain text, on the shelf for good (not copying, not locked away).
bool NotchController::indexable(const FileItem &item)
{
    if (item.status != Status::Available || item.vault || item.mimeType == QLatin1String("application/x-desktop"))
        return false;
    auto it = m_textMime.find(item.mimeType);
    if (it == m_textMime.end())
        it = m_textMime.insert(item.mimeType, QMimeDatabase().mimeTypeForName(item.mimeType).inherits(QStringLiteral("text/plain")));
    return it.value();
}

void NotchController::scheduleIndexing()
{
    if (m_ready && m_repo.ftsReady() && !m_indexTimer.isActive())
        m_indexTimer.start(m_indexed.isEmpty() ? 0 : 200);
}

// A few files at a time (the first 64 KB of each, one transaction), until none is left; then the timer is not started again.
void NotchController::indexStep()
{
    constexpr int PerStep = 10;
    constexpr qint64 MaxBytes = 64 * 1024;
    QList<QPair<QString, QString>> batch;
    bool more = false;
    for (const FileItem &item : m_model.items()) {
        if (m_indexed.contains(item.id) || !indexable(item))
            continue;
        if (batch.size() >= PerStep) {
            more = true;
            break;
        }
        QFile f(pathOf(item));
        batch.append({item.id, f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.read(MaxBytes)) : QString()}); // unreadable: empty text, not asked again
    }
    if (!batch.isEmpty() && m_repo.indexTexts(batch)) {
        for (const auto &t : batch)
            m_indexed.insert(t.first);
        if (!m_filter.trimmed().isEmpty())
            m_searchTimer.start(); // a search is open: what was just indexed may match it
    }
    if (more)
        m_indexTimer.start(50);
}

void NotchController::runSearch()
{
    m_proxy.setContentIds(m_repo.searchText(m_filter));
}

bool NotchController::adoptFile(const QString &path, const QString &name, const QString &source, const QString &categoryId, QString *idOut)
{
    FileItem item = newItem(name, categoryId);
    item.mimeType = QMimeDatabase().mimeTypeForFile(path).name();
    item.categoryId = ruledCategory(item.categoryId, item.mimeType);
    if (QString why; !assignStorage(item, &why)) {
        setError(name + QStringLiteral(": ") + why);
        return false;
    }
    item.sizeBytes = QFileInfo(path).size();
    item.sourcePath = source;
    item.status = Status::Available;
    const QString dir = baseDir(item) + u'/' + item.id;
    const QString target = dir + u'/' + item.originalName;
    // Rename when on the same filesystem, else copy (a custom storage folder may live elsewhere).
    if (!QDir().mkpath(dir) || !(QFile::rename(path, target) || QFile::copy(path, target)) || !m_repo.insert(item)) {
        FileService::removeManaged(baseDir(item), item.storedName);
        setError(tr("%1: cannot save").arg(name));
        return false;
    }
    m_model.upsert(item);
    if (idOut)
        *idOut = item.id;
    return true;
}

QString NotchController::adoptLocalFile(const QString &path, const QString &name, const QString &source, const QString &categoryId)
{
    QString id;
    return adoptFile(path, name, source, categoryId, &id) ? id : QString();
}

QStringList NotchController::mimeTypes(const QStringList &ids) const
{
    QStringList out;
    for (const QString &id : ids)
        if (const FileItem *item = m_model.find(id))
            out << item->mimeType;
    return out;
}

QVariantList NotchController::convertTargets(const QStringList &ids) const
{
    QList<ConvertModel::Pick> picks;
    for (const QString &id : ids)
        if (const FileItem *item = m_model.find(id))
            picks.append({ConvertModel::kindOf(item->mimeType, item->originalName), ConvertModel::suffixOf(item->originalName)});
    QVariantList out;
    for (const ConvertModel::Target &t : ConvertModel::targetsForSelection(picks)) {
        const bool have = ConvertModel::have(t.engine);
        out.append(QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("label"), t.label}, {QStringLiteral("group"), t.group},
                               {QStringLiteral("available"), have}, {QStringLiteral("hint"), have ? QString() : tr("needs %1").arg(ConvertModel::needs(t.engine))}});
    }
    return out;
}

int NotchController::pageCount(const QString &id) const
{
    const FileItem *item = m_model.find(id);
    return item ? DocTools::pdfPageCount(pathOf(*item)) : -1;
}

void NotchController::runTool(const QString &kind, const QStringList &ids, const QString &option)
{
    QList<DocTools::ToolInput> inputs;
    bool fromVault = false;
    for (const QString &id : ids)
        if (const FileItem *item = available(id)) {
            inputs << DocTools::ToolInput{pathOf(*item), QFileInfo(item->originalName).completeBaseName(),
                                          item->originalName};
            fromVault = fromVault || item->vault;
        }
    if (inputs.isEmpty())
        return;
    const QString category = m_categoryFilter == QLatin1String(RemovedView) ? QString() : m_categoryFilter;
    const QString work = scratchDir(fromVault) + QStringLiteral("/tools/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(work);

    auto job = [kind, inputs, option, work] { return DocTools::runTool(kind, inputs, option, work); };

    ++m_copying; // shows the busy pill while the tool runs
    emit copyingChanged();
    auto *watcher = new QFutureWatcher<DocTools::ToolResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, kind, category, work, inputs] {
        const DocTools::ToolResult r = watcher->result();
        watcher->deleteLater();
        --m_copying;
        emit copyingChanged();
        int added = 0;
        for (const auto &[path, name] : r.outputs)
            if (QFileInfo::exists(path) && adoptFile(path, name, tr("Made in Olyzano Notch"), category))
                ++added;
        if (kind.startsWith(QLatin1String("ocr")) && r.error.isEmpty()) {
            if (kind == QLatin1String("ocr-copy")) {
                copyText(r.text);
                emit notice(tr("Text copied"), tr("%n characters", nullptr, int(r.text.size())), QStringLiteral("edit-copy"), QString());
            } else if (addBytes(inputs.first().base + QStringLiteral(" (text).txt"), QStringLiteral("text/plain"), r.text.toUtf8(),
                                tr("Text from %1").arg(inputs.first().name), category)) {
                emit notice(tr("Text saved as a note"), inputs.first().base + QStringLiteral(" (text).txt"),
                            QStringLiteral("text-plain"), QString());
            }
        }
        QDir(work).removeRecursively(); // our scratch folder inside Notch's data dir
        if (!r.error.isEmpty())
            setError(r.error);
        else if (added > 0)
            emit notice(r.summary, QString(), QStringLiteral("application-pdf"), QString());
    });
    watcher->setFuture(QtConcurrent::run(job));
}

QVariantMap NotchController::transformText(const QString &kind, const QString &text) const
{
    const TextResult r = ::transformText(kind, text);
    return {{QStringLiteral("ok"), r.ok}, {QStringLiteral("text"), r.text}, {QStringLiteral("error"), r.error}};
}

QString NotchController::createVault(const QString &password)
{
    const QString err = m_vault.create(password);
    if (err.isEmpty())
        emit vaultChanged();
    return err;
}

QString NotchController::unlockVault(const QString &password)
{
    const QString err = m_vault.unlock(password);
    if (!err.isEmpty())
        return err;
    // Finish deletions of vault files that were interrupted while it was closed.
    for (const FileItem &item : m_repo.withStatus(Status::PendingCleanup))
        if (item.vault && FileService::removeManaged(m_vault.mountDir(), item.storedName))
            m_repo.remove(item.id);
    vaultStateChanged();
    return {};
}

void NotchController::lockVault()
{
    if (!m_vault.isUnlocked())
        return;
    if (const QString err = m_vault.lock(); !err.isEmpty())
        setError(tr("Could not lock: %1").arg(err));
    QDir(scratchDir(true)).removeRecursively(); // decrypted previews/tool files, on tmpfs anyway
    vaultStateChanged();
}

void NotchController::vaultStateChanged()
{
    const bool open = m_vault.isUnlocked();
    m_model.vaultStateChanged();
    m_proxy.setVaultUnlocked(open);
    emit vaultChanged();
    emit countChanged();
}

void NotchController::setCategoryLocked(const QString &categoryId, bool locked)
{
    if (!m_vault.exists() || !m_vault.isUnlocked()) {
        setError(tr("Unlock first"));
        return;
    }
    QStringList list = m_settings.value(QStringLiteral("lockedCategories")).toStringList();
    list.removeAll(categoryId);
    if (locked)
        list << categoryId;
    m_settings.setValue(QStringLiteral("lockedCategories"), list);
    // Move the category's files across; removed ones come back (the vault has no Recently removed).
    int moved = 0;
    const QList<FileItem> items = m_model.items();
    for (const FileItem &item : items) {
        if (item.categoryId != categoryId || item.status != Status::Available)
            continue;
        if (item.removedAt.isValid() && m_repo.setRemoved(item.id, QDateTime()))
            m_model.setRemoved(QSet<QString>{item.id}, QDateTime());
        if (const FileItem *current = m_model.find(item.id); current && relocate(*current, locked))
            ++moved;
    }
    refreshCategories();
    emit countChanged();
    emit notice(locked ? tr("Category locked") : tr("Lock removed"), tr("%n file(s) moved", nullptr, moved),
                locked ? QStringLiteral("object-locked") : QStringLiteral("object-unlocked"), QString());
}

bool NotchController::spectacleAutoSave() const
{
#ifndef Q_OS_LINUX
    return true; // nothing to switch on: Windows saves Win+PrtSc screenshots to Pictures\\Screenshots itself
#endif
    return QSettings(spectacleConfig(), QSettings::IniFormat).value(QStringLiteral("General/autoSaveImage"), false).toBool();
}

void NotchController::enableSpectacleAutoSave()
{
    // kwriteconfig6 edits the one key and leaves the rest of Spectacle's file as KDE wrote it.
    const int rc = QProcess::execute(QStringLiteral("kwriteconfig6"),
                                     {QStringLiteral("--file"), spectacleConfig(), QStringLiteral("--group"), QStringLiteral("General"),
                                      QStringLiteral("--key"), QStringLiteral("autoSaveImage"), QStringLiteral("--type"),
                                      QStringLiteral("bool"), QStringLiteral("true")});
    if (rc != 0)
        setError(tr("Could not change Spectacle's settings (kwriteconfig6 exit %1)").arg(rc));
    emit spectacleAutoSaveChanged();
}

QStringList NotchController::fileUrls(const QStringList &ids) const
{
    QStringList urls;
    for (const QString &id : ids)
        if (const FileItem *item = available(id))
            urls << QUrl::fromLocalFile(pathOf(*item)).toString();
    return urls;
}

void NotchController::addReceived(const QString &url, const QString &deviceName)
{
    if (!m_ready || !autoKdeConnect())
        return;
    const QUrl u(url);
    const QString category = autoCategory(QStringLiteral("kdeconnectCategory"), tr("KDE Connect"));
    const QString title = deviceName.isEmpty() ? tr("Received") : tr("From %1").arg(deviceName);
    emit sound(QStringLiteral("receive"));
    if (!u.isLocalFile()) { // a shared link
        addUrls({u}, category);
        emit notice(title, u.toDisplayString(), QStringLiteral("text-html"), QString());
        return;
    }
    if (!claimAutoAdd(u.toLocalFile()))
        return;
    QStringList errors;
    addLocalFile(u.toLocalFile(), category, errors);
    if (!errors.isEmpty()) {
        setError(errors.join(u'\n'));
        return;
    }
    const QMimeType mt = QMimeDatabase().mimeTypeForFile(u.toLocalFile());
    emit notice(title, QFileInfo(u.toLocalFile()).fileName(), mt.iconName(),
                mt.name().startsWith(QLatin1String("image/")) ? url : QString());
}

void NotchController::onWatchedFile(const QString &path, const QString &settingKey, const QString &categoryName,
                                    const QString &title)
{
    if (!claimAutoAdd(path))
        return;
    QStringList errors;
    addLocalFile(path, autoCategory(settingKey, categoryName), errors);
    if (!errors.isEmpty()) {
        setError(errors.join(u'\n'));
        return;
    }
    const QMimeType mt = QMimeDatabase().mimeTypeForFile(path);
    const bool image = mt.name().startsWith(QLatin1String("image/"));
    emit notice(title, QFileInfo(path).fileName(), mt.iconName(), image ? QUrl::fromLocalFile(path).toString() : QString());
}

QStringList NotchController::categoryItemIds(const QString &categoryId) const
{
    QStringList ids;
    for (const FileItem &item : m_model.items())
        if ((categoryId.isEmpty() || item.categoryId == categoryId) && item.status == Status::Available
            && !item.removedAt.isValid())
            ids << item.id;
    return ids;
}

void NotchController::renameItem(const QString &id, const QString &name)
{
    const FileItem *item = available(id);
    if (!item)
        return;
    const QString wanted = FileService::safeFileName(name.trimmed(), QString());
    if (wanted.isEmpty() || wanted == item->originalName)
        return;
    const QString clean = freeName(wanted, id);
    const QString storedName = id + u'/' + clean;
    const QString from = pathOf(*item);
    const QString to = baseDir(*item) + u'/' + storedName;
    if (!FileService::isValidStoredName(storedName) || !QFile::rename(from, to)) {
        setError(tr("Cannot rename %1").arg(item->originalName));
        return;
    }
    const QString mime = QMimeDatabase().mimeTypeForFile(to).name(); // the extension may have changed
    if (!m_repo.renameItem(id, clean, storedName, mime)) {
        QFile::rename(to, from); // keep disk and database in step
        setError(tr("Cannot rename: %1").arg(m_repo.errorString()));
        return;
    }
    FileItem renamed = *item;
    renamed.originalName = clean;
    renamed.storedName = storedName;
    renamed.mimeType = mime;
    m_model.upsert(renamed);
}

void NotchController::saveTo(const QStringList &ids, const QUrl &folder)
{
    const QString dir = folder.toLocalFile();
    QList<QPair<QString, QString>> jobs; // stored path, name
    for (const QString &id : ids)
        if (const FileItem *item = available(id))
            jobs << qMakePair(pathOf(*item), item->originalName);
    if (dir.isEmpty() || jobs.isEmpty())
        return;
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, dir, total = jobs.size()] {
        const QStringList failed = watcher->result();
        watcher->deleteLater();
        if (!failed.isEmpty())
            setError(tr("Could not save: %1").arg(failed.join(QStringLiteral(", "))));
        const int saved = total - failed.size();
        if (saved > 0)
            emit notice(saved == 1 ? tr("Saved 1 file") : tr("Saved %1 files").arg(saved), QDir(dir).dirName(),
                        QStringLiteral("folder"), QString());
    });
    watcher->setFuture(QtConcurrent::run([jobs, dir] {
        QStringList failed;
        for (const auto &[src, name] : jobs) {
            // QFile::copy never overwrites; if another save took the name meanwhile, take the next one.
            bool ok = false;
            for (int attempt = 0; attempt < 5 && !ok; ++attempt)
                ok = QFile::copy(src, dir + u'/' + FileService::uniqueName(dir, name));
            if (!ok)
                failed << name;
        }
        return failed;
    }));
}

void NotchController::zipItems(const QStringList &ids)
{
    QString base = QStringLiteral("Olyzano Notch");
    for (const Category &c : std::as_const(m_categoryList))
        if (c.id == m_categoryFilter)
            base = c.name;
    startZip(ids, base, m_categoryFilter);
}

void NotchController::zipCategory(const QString &categoryId)
{
    QString base = tr("Folder");
    for (const Category &c : std::as_const(m_categoryList))
        if (c.id == categoryId)
            base = c.name;
    startZip(categoryItemIds(categoryId), base, categoryId);
}

void NotchController::startZip(const QStringList &ids, const QString &base, const QString &categoryId)
{
    QList<const FileItem *> items;
    for (const QString &id : ids)
        if (const FileItem *item = available(id))
            items << item;
    if (items.isEmpty())
        return;
    FileItem zip = newItem(QStringLiteral("%1 (%2 %3).zip").arg(base).arg(items.size())
                               .arg(items.size() == 1 ? tr("file") : tr("files")),
                           categoryId);
    zip.mimeType = QStringLiteral("application/zip");
    zip.sourcePath = tr("Compressed in Olyzano Notch");
    if (QString why; !assignStorage(zip, &why)) {
        setError(why);
        return;
    }
    const QString itemDir = baseDir(zip) + u'/' + zip.id;
    const QString out = itemDir + QStringLiteral("/archive.zip");
    if (!QDir().mkpath(itemDir) || !m_repo.insert(zip)) {
        QDir(itemDir).removeRecursively();
        setError(tr("Cannot create the zip file"));
        return;
    }
    // Two files may share a name (report.pdf from two folders): the second becomes "report (1).pdf".
    QList<ZipWriter::Entry> entries;
    QStringList used;
    qint64 totalBytes = 0;
    for (const FileItem *item : items) {
        totalBytes += item->sizeBytes;
        const QString name = FileService::uniqueAmong(item->originalName, [&](const QString &n) { return used.contains(n); });
        used << name;
        entries.append({pathOf(*item), name});
    }
    m_model.upsert(zip);
    ++m_copying;
    emit copyingChanged();
    Transfer t;
    t.done = std::make_shared<std::atomic<qint64>>(0);
    t.cancel = std::make_shared<std::atomic_bool>(false);
    t.total = totalBytes;
    t.zip = true;
    t.title = zip.originalName;
    beginTransfer(zip.id, t);

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, zip, out] {
        const QString why = watcher->result();
        watcher->deleteLater();
        --m_copying;
        emit copyingChanged();
        endTransfer(zip.id);
        const QString final = pathOf(zip);
        const qint64 size = QFileInfo(out).size();
        if (why.isEmpty() && QFile::rename(out, final) && m_repo.markAvailable(zip.id, size)) {
            FileItem ready = zip;
            ready.status = Status::Available;
            ready.sizeBytes = size;
            m_model.upsert(ready);
            emit notice(tr("Zip ready: drag it out"), zip.originalName, QStringLiteral("application-zip"), QString());
            return;
        }
        QFile::remove(out);
        FileService::removeManaged(baseDir(zip), zip.storedName);
        m_repo.remove(zip.id);
        m_model.removeId(zip.id);
        if (why != QLatin1String("cancelled"))
            setError(tr("Could not create zip: %1").arg(why));
        expireEmptyCategories();
    });
    watcher->setFuture(QtConcurrent::run(&m_copyPool, [entries, out, done = t.done, cancel = t.cancel] {
        return ZipWriter::write(out, entries, done.get(), cancel.get());
    }));
}

void NotchController::clearError()
{
    setError({});
}

void NotchController::setError(const QString &message)
{
    if (!message.isEmpty())
        qCWarning(lcNotch).noquote() << message;
    if (m_error == message)
        return;
    m_error = message;
    emit errorMessageChanged();
}

QString NotchController::storedPath(const QString &id) const
{
    const FileItem *item = m_model.find(id);
    return item && item->status == Status::Available ? pathOf(*item) : QString();
}
