#include "FileItemModel.h"

#include "services/FileService.h"

#include <QLocale>
#include <QMimeDatabase>
#include <QUrl>

FileItemModel::FileItemModel(const QString &filesDir, QObject *parent)
    : QAbstractListModel(parent)
    , m_filesDir(filesDir)
{
}

int FileItemModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

static QString expiresText(const QDateTime &addedAt, int retentionDays)
{
    const qint64 secs = QDateTime::currentDateTimeUtc().secsTo(addedAt.addDays(retentionDays));
    if (secs <= 0)
        return QStringLiteral("expiring");
    const qint64 days = secs / 86400, hours = (secs % 86400) / 3600;
    if (days > 0)
        return QStringLiteral("%1d %2h left").arg(days).arg(hours);
    return hours > 0 ? QStringLiteral("%1h left").arg(hours) : QStringLiteral("<1h left");
}

QVariant FileItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const FileItem &i = m_items.at(index.row());
    switch (role) {
    case IdRole: return i.id;
    case NameRole: return i.originalName;
    case SizeTextRole: return QLocale().formattedDataSize(i.sizeBytes);
    case AddedTextRole: return QLocale().toString(i.addedAt.toLocalTime(), QLocale::ShortFormat);
    case ExpiresTextRole: return i.pinned ? QStringLiteral("kept, never expires") : expiresText(i.addedAt, m_retentionDays);
    case StatusRole: return i.status;
    case IconNameRole: {
        const QMimeType mt = QMimeDatabase().mimeTypeForName(i.mimeType);
        return mt.isValid() ? mt.iconName() : QStringLiteral("text-x-generic");
    }
    case FileUrlRole: return QUrl::fromLocalFile(FileService::storedPath(m_filesDir, i.storedName));
    case SourcePathRole: return i.sourcePath;
    case IsImageRole: return i.mimeType.startsWith(QLatin1String("image/"));
    case IsTextRole: return QMimeDatabase().mimeTypeForName(i.mimeType).inherits(QStringLiteral("text/plain"));
    case CategoryIdRole: return i.categoryId;
    case PinnedRole: return i.pinned;
    case AddedAtRole: return i.addedAt;
    case SizeRole: return i.sizeBytes;
    case MimeTypeRole: return i.mimeType;
    case RemovedRole: return i.removedAt.isValid();
    case RemovedTextRole: {
        if (!i.removedAt.isValid())
            return QString();
        const qint64 hours = qMax<qint64>(0, QDateTime::currentDateTimeUtc().secsTo(i.removedAt.addSecs(TrashHours * 3600)) / 3600);
        return hours > 0 ? QStringLiteral("deleted in %1h").arg(hours) : QStringLiteral("deleted soon");
    }
    case ExpiringSoonRole:
        return !i.pinned && !i.removedAt.isValid()
               && QDateTime::currentDateTimeUtc().secsTo(i.addedAt.addDays(m_retentionDays)) < 24 * 3600;
    }
    return {};
}

QHash<int, QByteArray> FileItemModel::roleNames() const
{
    return {
        {IdRole, "itemId"},
        {NameRole, "name"},
        {SizeTextRole, "sizeText"},
        {AddedTextRole, "addedText"},
        {ExpiresTextRole, "expiresText"},
        {StatusRole, "status"},
        {IconNameRole, "iconName"},
        {FileUrlRole, "fileUrl"},
        {SourcePathRole, "sourcePath"},
        {IsImageRole, "isImage"},
        {IsTextRole, "isText"},
        {CategoryIdRole, "categoryId"},
        {PinnedRole, "pinned"},
        {AddedAtRole, "addedAt"},
        {SizeRole, "sizeBytes"},
        {MimeTypeRole, "mimeType"},
        {RemovedRole, "removed"},
        {RemovedTextRole, "removedText"},
        {ExpiringSoonRole, "expiringSoon"},
    };
}

int FileItemModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).id == id)
            return i;
    return -1;
}

void FileItemModel::setItems(const QList<FileItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}

void FileItemModel::upsert(const FileItem &item)
{
    const int row = indexOf(item.id);
    if (row >= 0) {
        m_items[row] = item;
        emit dataChanged(index(row), index(row));
        return;
    }
    int at = 0;
    while (at < m_items.size() && m_items.at(at).addedAt > item.addedAt)
        ++at;
    beginInsertRows({}, at, at);
    m_items.insert(at, item);
    endInsertRows();
}

void FileItemModel::removeId(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
}

const FileItem *FileItemModel::find(const QString &id) const
{
    const int row = indexOf(id);
    return row >= 0 ? &m_items.at(row) : nullptr;
}

void FileItemModel::refreshTimes()
{
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1), {ExpiresTextRole, RemovedTextRole, ExpiringSoonRole});
}

void FileItemModel::setRemoved(const QStringList &ids, const QDateTime &at)
{
    for (int row = 0; row < m_items.size(); ++row) {
        if (ids.contains(m_items.at(row).id)) {
            m_items[row].removedAt = at;
            emit dataChanged(index(row), index(row), {RemovedRole, RemovedTextRole, ExpiringSoonRole});
        }
    }
}

int FileItemModel::shelfCount() const
{
    return int(std::count_if(m_items.cbegin(), m_items.cend(), [](const FileItem &i) { return !i.removedAt.isValid(); }));
}

int FileItemModel::removedCount() const
{
    return m_items.size() - shelfCount();
}

void FileItemModel::setFilesDir(const QString &filesDir)
{
    m_filesDir = filesDir;
}

void FileItemModel::setRetentionDays(int days)
{
    m_retentionDays = days;
    refreshTimes();
}

void FileItemModel::setCategory(const QStringList &ids, const QString &categoryId)
{
    for (int row = 0; row < m_items.size(); ++row) {
        if (ids.contains(m_items.at(row).id)) {
            m_items[row].categoryId = categoryId;
            emit dataChanged(index(row), index(row), {CategoryIdRole});
        }
    }
}

void FileItemModel::clearCategory(const QString &from)
{
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).categoryId == from) {
            m_items[row].categoryId.clear();
            emit dataChanged(index(row), index(row), {CategoryIdRole});
        }
    }
}

void FileItemModel::setPinned(const QStringList &ids, bool pinned)
{
    for (int row = 0; row < m_items.size(); ++row) {
        if (ids.contains(m_items.at(row).id)) {
            m_items[row].pinned = pinned;
            emit dataChanged(index(row), index(row), {PinnedRole, ExpiresTextRole, ExpiringSoonRole});
        }
    }
}
