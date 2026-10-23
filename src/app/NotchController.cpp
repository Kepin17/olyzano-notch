#include "NotchController.h"

#include "services/CleanupService.h"
#include "services/FileService.h"
#include "services/TextTools.h"
#include "services/DocTools.h"

#include <QBuffer>
#include <QClipboard>
#include <QCollator>
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
#include <unistd.h>

// ponytail: folder drops are walked on the main thread; the cap keeps a huge tree from stalling the UI.
constexpr int MaxFolderFiles = 1000;

void FileFilter::setText(const QString &text)
{
    beginFilterChange();
    m_text = text;
    endFilterChange(Direction::Rows);
}

void FileFilter::setCategory(const QString &categoryId)
{
    beginFilterChange();
    m_category = categoryId;
    endFilterChange(Direction::Rows);
}

void FileFilter::setSortMode(const QString &mode)
{
    m_sort = mode;
    invalidate();
    sort(0);
}

bool FileFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    static const QCollator natural = [] {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    const auto name = [](const QModelIndex &i) { return i.data(FileItemModel::NameRole).toString(); };
    if (m_sort == QLatin1String("name"))
        return natural.compare(name(left), name(right)) < 0;
    if (m_sort == QLatin1String("size"))
        return left.data(FileItemModel::SizeRole).toLongLong() > right.data(FileItemModel::SizeRole).toLongLong();
    if (m_sort == QLatin1String("type")) {
        const int byType = QString::compare(left.data(FileItemModel::MimeTypeRole).toString(),
                                            right.data(FileItemModel::MimeTypeRole).toString());
        return byType != 0 ? byType < 0 : natural.compare(name(left), name(right)) < 0;
    }
    return left.data(FileItemModel::AddedAtRole).toDateTime() > right.data(FileItemModel::AddedAtRole).toDateTime();
}

bool FileFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    const bool match = i.data(FileItemModel::NameRole).toString().contains(m_text, Qt::CaseInsensitive)
                       || (m_contents && !m_text.isEmpty()
                           && m_contents->value(i.data(FileItemModel::IdRole).toString()).contains(m_text, Qt::CaseInsensitive));
    if (!match)
        return false;
    const bool removed = i.data(FileItemModel::RemovedRole).toBool();
    if (m_category == QLatin1String(RemovedView))
        return removed;
    return !removed && (m_category.isEmpty() || i.data(FileItemModel::CategoryIdRole).toString() == m_category);
}

static QString configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

// Spectacle runs with the session's config dir, not ours (ours may be redirected for testing).
static QString spectacleConfig()
{
    return QDir::homePath() + QStringLiteral("/.config/spectaclerc");
}

static QString autostartPath()
{
    return configDir() + QStringLiteral("/autostart/notch.desktop");
}

NotchController::NotchController(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_settings(configDir() + QStringLiteral("/notch/notch.conf"), QSettings::IniFormat)
    , m_dataDir(dataDir)
    , m_filesDir(m_settings.value(QStringLiteral("storageDir"), dataDir + QStringLiteral("/files")).toString())
    , m_retentionDays(qBound(1, m_settings.value(QStringLiteral("retentionDays"), DefaultRetentionDays).toInt(), 365))
    , m_model(m_filesDir)
{
    m_model.setRetentionDays(m_retentionDays);
    m_proxy.setSourceModel(&m_model);
    m_proxy.setContents(&m_contents);
    const auto stale = [this] { m_contentsStale = true; };
    connect(&m_model, &QAbstractItemModel::rowsInserted, this, stale);
    connect(&m_model, &QAbstractItemModel::rowsRemoved, this, stale);
    connect(&m_model, &QAbstractItemModel::modelReset, this, stale);
    m_proxy.setSortMode(sortMode());
    connect(&m_screenshots, &FolderWatcher::fileReady, this, [this](const QString &path) {
        onWatchedFile(path, QStringLiteral("screenshotCategory"), tr("Screenshots"), tr("Screenshot added"));
    });
    connect(&m_downloads, &FolderWatcher::fileReady, this, [this](const QString &path) {
        onWatchedFile(path, QStringLiteral("downloadCategory"), tr("Downloads"), tr("Downloaded"));
    });

    // Per-category counts only change when files come, go, move or get removed; not on the
    // minute tick or a copy's status change. refreshCategories() also skips no-op updates.
    for (auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved}) {
        connect(&m_model, signal, this, &NotchController::countChanged);
        connect(&m_model, signal, this, &NotchController::refreshCategories);
    }
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
    connect(&m_model, &QAbstractItemModel::dataChanged, this, &NotchController::countChanged);
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
    m_model.setItems(m_repo.visible());
    updateWatchers();
    m_clock.start();
    m_cleanup.start();
    return true;
}

const FileItem *NotchController::available(const QString &id) const
{
    const FileItem *item = m_model.find(id);
    return item && item->status == Status::Available ? item : nullptr;
}

bool NotchController::storeSetting(const QString &key, const QVariant &value)
{
    if (m_settings.contains(key) && m_settings.value(key) == value)
        return false;
    m_settings.setValue(key, value);
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
    item.originalName = freeName(name);
    item.storedName = item.id + u'/' + item.originalName;
    item.addedAt = QDateTime::currentDateTimeUtc();
    item.status = Status::Copying;
    item.categoryId = categoryId;
    return item;
}

void NotchController::addUrls(const QList<QUrl> &urls, const QString &categoryId)
{
    if (!m_ready)
        return;
    QStringList errors;
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
        const QString path = url.toLocalFile();
        if (!QFileInfo(path).isDir()) {
            addLocalFile(path, categoryId, errors);
            continue;
        }
        bool truncated = false;
        const auto entries = FileService::expandFolder(path, MaxFolderFiles, &truncated);
        if (truncated)
            errors << tr("%1: only the first %2 files were added").arg(QFileInfo(path).fileName()).arg(MaxFolderFiles);
        if (entries.isEmpty())
            errors << tr("%1: folder has no files").arg(QFileInfo(path).fileName());
        for (const auto &e : entries)
            addLocalFile(e.path, createCategory(e.category), errors);
    }
    if (!errors.isEmpty())
        setError(errors.join(u'\n'));
}

void NotchController::addDrop(const QList<QUrl> &urls, const QString &html, const QString &categoryId)
{
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
    if (fi.canonicalFilePath().startsWith(QDir(m_filesDir).canonicalPath() + u'/'))
        return; // dropped back from Notch itself

    FileItem item = newItem(fi.fileName(), categoryId);
    item.mimeType = QMimeDatabase().mimeTypeForFile(fi).name();
    item.categoryId = ruledCategory(item.categoryId, item.mimeType);
    item.sizeBytes = fi.size();
    item.sourcePath = fi.absoluteFilePath();
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
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, item] {
        const FileService::CopyResult r = watcher->result();
        watcher->deleteLater();
        --m_copying;
        emit copyingChanged();
        if (r.ok && m_repo.markAvailable(item.id, r.bytes)) {
            FileItem done = item;
            done.status = Status::Available;
            done.sizeBytes = r.bytes;
            m_model.upsert(done);
            return;
        }
        const QString reason = r.ok ? m_repo.errorString() : r.error;
        if (r.ok)
            FileService::removeManaged(m_filesDir, item.storedName);
        m_repo.remove(item.id);
        m_model.removeId(item.id);
        setError(item.originalName + QStringLiteral(": ") + reason);
    });
    const QString src = item.sourcePath, dir = m_filesDir + u'/' + item.id, name = item.originalName;
    watcher->setFuture(QtConcurrent::run([src, dir, name] { return FileService::copyInto(src, dir, name); }));
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
        const QString dir = m_filesDir + u'/' + item.id;
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
            ok = d->out->flush() && ::fsync(d->out->handle()) == 0;
            bytes = d->out->size();
            d->out->close();
            ok = ok && QFile::rename(d->out->fileName(), FileService::storedPath(m_filesDir, d->item.storedName))
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
            FileService::removeManaged(m_filesDir, d->item.storedName);
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
                               const QString &source, const QString &categoryId)
{
    FileItem item = newItem(FileService::safeFileName(name), categoryId);
    item.mimeType = mimeType;
    item.categoryId = ruledCategory(item.categoryId, mimeType);
    item.sizeBytes = bytes.size();
    item.sourcePath = source;
    item.status = Status::Available;
    if (!FileService::writeFile(m_filesDir + u'/' + item.id, item.originalName, bytes) || !m_repo.insert(item)) {
        FileService::removeManaged(m_filesDir, item.storedName);
        setError(tr("%1: cannot save").arg(item.originalName));
        return false;
    }
    m_model.upsert(item);
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
    setError(tr("The clipboard has nothing Notch can keep."));
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
        const QUrl url = QUrl::fromLocalFile(FileService::storedPath(m_filesDir, item->storedName));
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
    QFile f(FileService::storedPath(m_filesDir, item->storedName));
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
            paths << FileService::storedPath(m_filesDir, item->storedName);
    if (!paths.isEmpty())
        QGuiApplication::clipboard()->setText(paths.join(u'\n'));
}

void NotchController::setPinned(const QStringList &ids, bool pinned)
{
    QStringList done;
    for (const QString &id : ids)
        if (m_repo.setPinned(id, pinned))
            done << id;
    m_model.setPinned(done, pinned);
}

void NotchController::open(const QString &id)
{
    const FileItem *item = available(id);
    if (!item)
        return;
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(FileService::storedPath(m_filesDir, item->storedName))))
        setError(tr("%1: no application could open this file").arg(item->originalName));
}

void NotchController::remove(const QString &id)
{
    removeItems({id});
}

void NotchController::removeItems(const QStringList &ids)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList moved;
    for (const QString &id : ids) {
        const FileItem *item = m_model.find(id);
        if (!item || item->status == Status::Copying || item->removedAt.isValid())
            continue;
        if (m_repo.setRemoved(id, now))
            moved << id;
    }
    if (moved.isEmpty())
        return;
    m_model.setRemoved(moved, now);
    m_lastRemoved = moved;
    m_trashTimer.start(); // the Undo toast's lifetime
    emit undoCountChanged();
    emit countChanged();
}

void NotchController::undoRemove()
{
    restoreItems(m_lastRemoved);
}

void NotchController::restoreItems(const QStringList &ids)
{
    QStringList back;
    for (const QString &id : ids)
        if (m_repo.setRemoved(id, QDateTime()))
            back << id;
    m_model.setRemoved(back, QDateTime());
    if (!m_lastRemoved.isEmpty()) {
        m_lastRemoved.clear();
        m_trashTimer.stop();
        emit undoCountChanged();
    }
    emit countChanged();
}

void NotchController::deleteForever(const QStringList &ids)
{
    for (const QString &id : ids) {
        if (const FileItem *item = m_model.find(id); item && item->status != Status::Copying) {
            const FileItem copy = *item;
            m_model.removeId(id);
            deleteNow(copy);
        }
    }
    emit countChanged();
}

void NotchController::emptyRecentlyRemoved()
{
    QStringList ids;
    for (const FileItem &item : m_model.items())
        if (item.removedAt.isValid())
            ids << item.id;
    deleteForever(ids);
}

void NotchController::deleteNow(const FileItem &item)
{
    if (!m_repo.setStatus(item.id, Status::PendingCleanup)) {
        setError(tr("Database error: %1").arg(m_repo.errorString()));
        return;
    }
    if (FileService::removeManaged(m_filesDir, item.storedName))
        m_repo.remove(item.id);
    else
        setError(tr("Could not delete the stored copy yet; cleanup will retry."));
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
            uris << QString::fromUtf8(QUrl::fromLocalFile(FileService::storedPath(m_filesDir, item->storedName)).toEncoded());
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
                                {QStringLiteral("count"), counts.value(c.id)}, {QStringLiteral("special"), special}};
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
    QStringList done;
    for (const QString &id : ids)
        if (m_repo.setCategory(id, categoryId))
            done << id;
    m_model.setCategory(done, categoryId);
    if (done.size() != ids.size())
        setError(tr("Some files could not be moved: %1").arg(m_repo.errorString()));
}

void NotchController::runCleanup()
{
    for (const QString &id : CleanupService::purge(m_repo, m_filesDir, QDateTime::currentDateTimeUtc(), m_retentionDays, m_dragging))
        m_model.removeId(id);
}

void NotchController::setFilter(const QString &text)
{
    if (text == m_filter)
        return;
    m_filter = text;
    if (!text.isEmpty() && m_contentsStale)
        indexContents();
    m_proxy.setText(text);
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
    return QFile::exists(autostartPath());
}

void NotchController::setAutostart(bool enabled)
{
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

QString NotchController::previewText(const QString &id) const
{
    const FileItem *item = available(id);
    if (!item)
        return {};
    QFile f(FileService::storedPath(m_filesDir, item->storedName));
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

void NotchController::addColor(const QString &hex)
{
    QStringList colors = recentColors();
    colors.removeAll(hex);
    colors.prepend(hex);
    m_settings.setValue(QStringLiteral("colors"), colors.mid(0, 12));
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
    QFile f(FileService::storedPath(m_filesDir, item->storedName));
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
    if (!FileService::writeFile(m_filesDir + u'/' + id, item->originalName, bytes) || !m_repo.markAvailable(id, bytes.size())) {
        setError(tr("Could not save %1").arg(item->originalName));
        return false;
    }
    FileItem saved = *item;
    saved.sizeBytes = bytes.size();
    m_model.upsert(saved);
    m_contentsStale = true;
    renameItem(id, clean); // no-op when the name is unchanged
    return true;
}

// ponytail: re-reads every text file on the first search after a change; fine for a shelf of
// files that expire within days. An incremental index is the upgrade if shelves get huge.
void NotchController::indexContents()
{
    m_contents.clear();
    QMimeDatabase db;
    for (const FileItem &item : m_model.items()) {
        if (item.status != Status::Available || item.sizeBytes > (512 << 10)
            || !db.mimeTypeForName(item.mimeType).inherits(QStringLiteral("text/plain"))
            || item.mimeType == QLatin1String("application/x-desktop"))
            continue;
        QFile f(FileService::storedPath(m_filesDir, item.storedName));
        if (f.open(QIODevice::ReadOnly))
            m_contents.insert(item.id, QString::fromUtf8(f.readAll()));
    }
    m_contentsStale = false;
}

bool NotchController::adoptFile(const QString &path, const QString &name, const QString &source, const QString &categoryId)
{
    FileItem item = newItem(name, categoryId);
    item.mimeType = QMimeDatabase().mimeTypeForFile(path).name();
    item.categoryId = ruledCategory(item.categoryId, item.mimeType);
    item.sizeBytes = QFileInfo(path).size();
    item.sourcePath = source;
    item.status = Status::Available;
    const QString dir = m_filesDir + u'/' + item.id;
    const QString target = dir + u'/' + item.originalName;
    // Rename when on the same filesystem, else copy (a custom storage folder may live elsewhere).
    if (!QDir().mkpath(dir) || !(QFile::rename(path, target) || QFile::copy(path, target)) || !m_repo.insert(item)) {
        FileService::removeManaged(m_filesDir, item.storedName);
        setError(tr("%1: cannot save").arg(name));
        return false;
    }
    m_model.upsert(item);
    return true;
}

QStringList NotchController::mimeTypes(const QStringList &ids) const
{
    QStringList out;
    for (const QString &id : ids)
        if (const FileItem *item = m_model.find(id))
            out << item->mimeType;
    return out;
}

int NotchController::pageCount(const QString &id) const
{
    const FileItem *item = m_model.find(id);
    return item ? DocTools::pdfPageCount(FileService::storedPath(m_filesDir, item->storedName)) : -1;
}

void NotchController::runTool(const QString &kind, const QStringList &ids, const QString &option)
{
    QList<DocTools::ToolInput> inputs;
    for (const QString &id : ids)
        if (const FileItem *item = available(id))
            inputs << DocTools::ToolInput{FileService::storedPath(m_filesDir, item->storedName), QFileInfo(item->originalName).completeBaseName(),
                            item->originalName};
    if (inputs.isEmpty())
        return;
    const QString category = m_categoryFilter == QLatin1String(RemovedView) ? QString() : m_categoryFilter;
    const QString work = m_dataDir + QStringLiteral("/tools/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
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
            if (QFileInfo::exists(path) && adoptFile(path, name, tr("Made in Notch"), category))
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

bool NotchController::spectacleAutoSave() const
{
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
            urls << QUrl::fromLocalFile(FileService::storedPath(m_filesDir, item->storedName)).toString();
    return urls;
}

void NotchController::addReceived(const QString &url, const QString &deviceName)
{
    if (!m_ready || !autoKdeConnect())
        return;
    const QUrl u(url);
    const QString category = autoCategory(QStringLiteral("kdeconnectCategory"), tr("KDE Connect"));
    const QString title = deviceName.isEmpty() ? tr("Received") : tr("From %1").arg(deviceName);
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
    const QString from = FileService::storedPath(m_filesDir, item->storedName);
    const QString to = FileService::storedPath(m_filesDir, storedName);
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
            jobs << qMakePair(FileService::storedPath(m_filesDir, item->storedName), item->originalName);
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
    QList<const FileItem *> items;
    for (const QString &id : ids)
        if (const FileItem *item = available(id))
            items << item;
    if (items.isEmpty())
        return;
    QString base = QStringLiteral("Notch");
    for (const Category &c : std::as_const(m_categoryList))
        if (c.id == m_categoryFilter)
            base = c.name;
    FileItem zip = newItem(QStringLiteral("%1 (%2 %3).zip").arg(base).arg(items.size())
                               .arg(items.size() == 1 ? tr("file") : tr("files")),
                           m_categoryFilter);
    zip.mimeType = QStringLiteral("application/zip");
    zip.sourcePath = tr("Compressed in Notch");
    const QString itemDir = m_filesDir + u'/' + zip.id;
    const QString stage = itemDir + QStringLiteral("/stage"); // links under unique names
    const QString out = itemDir + QStringLiteral("/archive.zip");
    if (!QDir().mkpath(stage) || !m_repo.insert(zip)) {
        QDir(itemDir).removeRecursively();
        setError(tr("Cannot create the zip file"));
        return;
    }
    for (const FileItem *item : items)
        QFile::link(FileService::storedPath(m_filesDir, item->storedName),
                    stage + u'/' + FileService::uniqueName(stage, item->originalName));
    m_model.upsert(zip);
    ++m_copying;
    emit copyingChanged();

    auto *process = new QProcess(this);
    process->setWorkingDirectory(stage);
    auto done = [this, process, zip, itemDir, stage, out](bool ok, const QString &why) {
        process->deleteLater();
        --m_copying;
        emit copyingChanged();
        QDir(stage).removeRecursively(); // only our symlinks: their targets are untouched
        const QString final = FileService::storedPath(m_filesDir, zip.storedName);
        const qint64 size = QFileInfo(out).size();
        if (ok && QFile::rename(out, final) && m_repo.markAvailable(zip.id, size)) {
            FileItem ready = zip;
            ready.status = Status::Available;
            ready.sizeBytes = size;
            m_model.upsert(ready);
            emit notice(tr("Zip ready"), zip.originalName, QStringLiteral("application-zip"), QString());
            return;
        }
        QFile::remove(out);
        FileService::removeManaged(m_filesDir, zip.storedName);
        m_repo.remove(zip.id);
        m_model.removeId(zip.id);
        setError(tr("Could not create zip: %1").arg(why));
    };
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit && code == 0,
             QString::fromLocal8Bit(process->readAllStandardError()).trimmed());
    });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(false, tr("the zip program is not installed"));
    });
    process->start(QStringLiteral("zip"), {QStringLiteral("-q"), QStringLiteral("-r"), out, QStringLiteral(".")});
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
