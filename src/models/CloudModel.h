#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QLocale>
#include <QMimeDatabase>
#include <QSortFilterProxyModel>

// The files on the cloud shelf, as the app shows them (already decrypted). Newest first. Updated in place: one row changes, the list is not rebuilt.
class CloudModel : public QAbstractListModel
{
    Q_OBJECT
public:
    struct Entry {
        QString id;
        qint64 owner = 0;
        qint64 size = 0;      // bytes on the server (the file as it is stored: encrypted)
        QString name, mime, category;
        QDateTime added;
        bool locked = false;  // the name could not be read: another key
        int percent = -1;     // 0-100 while it is being sent or fetched here
        bool busy = false;
    };
    enum Role {
        IdRole = Qt::UserRole + 1, NameRole, SizeTextRole, MimeRole, IconNameRole, MineRole, AddedTextRole,
        LockedRole, PercentRole, BusyRole, CategoryRole, AddedRole, SizeRole
    };

    explicit CloudModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{IdRole, "cloudId"}, {NameRole, "name"}, {SizeTextRole, "sizeText"}, {MimeRole, "mimeType"}, {IconNameRole, "iconName"},
                {MineRole, "mine"}, {AddedTextRole, "addedText"}, {LockedRole, "locked"}, {PercentRole, "percent"}, {BusyRole, "busy"},
                {CategoryRole, "category"}, {AddedRole, "added"}, {SizeRole, "size"}};
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_rows.size())
            return {};
        const Entry &e = m_rows.at(index.row());
        switch (role) {
        case IdRole: return e.id;
        case NameRole: return e.locked ? tr("(another key)") : e.name;
        case SizeTextRole: return QLocale().formattedDataSize(e.size);
        case MimeRole: return e.mime;
        case IconNameRole: {
            static const QMimeDatabase mimes;
            const QMimeType t = mimes.mimeTypeForName(e.mime);
            return t.isValid() ? t.iconName() : QStringLiteral("text-x-generic");
        }
        case MineRole: return e.owner == m_me;
        case AddedTextRole: return e.added.isValid() ? QLocale().toString(e.added.toLocalTime(), QLocale::ShortFormat) : QString();
        case LockedRole: return e.locked;
        case PercentRole: return e.percent;
        case BusyRole: return e.busy;
        case CategoryRole: return e.category;
        case AddedRole: return e.added;
        case SizeRole: return e.size;
        }
        return {};
    }

    void setMe(qint64 member) { m_me = member; }
    const QList<Entry> &rows() const { return m_rows; }
    int indexOf(const QString &id) const
    {
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows.at(i).id == id)
                return i;
        return -1;
    }
    const Entry *find(const QString &id) const
    {
        const int i = indexOf(id);
        return i < 0 ? nullptr : &m_rows.at(i);
    }
    void setAll(QList<Entry> rows)
    {
        std::sort(rows.begin(), rows.end(), [](const Entry &a, const Entry &b) {
            return a.added != b.added ? a.added > b.added : a.id < b.id;
        });
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }
    void setProgress(const QString &id, bool busy, int percent)
    {
        const int i = indexOf(id);
        if (i < 0 || (m_rows[i].busy == busy && m_rows[i].percent == percent))
            return;
        m_rows[i].busy = busy;
        m_rows[i].percent = percent;
        emit dataChanged(index(i), index(i), {BusyRole, PercentRole});
    }

private:
    QList<Entry> m_rows;
    qint64 m_me = -1;
};

class CloudFilterModel : public QSortFilterProxyModel
{
public:
    explicit CloudFilterModel(QObject *parent = nullptr) : QSortFilterProxyModel(parent)
    {
        setDynamicSortFilter(true);
        sort(0, Qt::AscendingOrder);
    }

    void setSearchText(const QString &text)
    {
        if (m_searchText == text)
            return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        m_searchText = text;
        endFilterChange(Direction::Rows);
#else
        m_searchText = text;
        invalidateFilter();
#endif
    }

    void setSortMode(const QString &mode)
    {
        if (m_sortMode == mode)
            return;
        m_sortMode = mode;
        invalidate();
        sort(0, Qt::AscendingOrder);
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        if (m_searchText.isEmpty())
            return true;
        const QModelIndex row = sourceModel()->index(sourceRow, 0, sourceParent);
        const QString name = sourceModel()->data(row, CloudModel::NameRole).toString();
        const QString category = sourceModel()->data(row, CloudModel::CategoryRole).toString();
        const QString mime = sourceModel()->data(row, CloudModel::MimeRole).toString();
        return name.contains(m_searchText, Qt::CaseInsensitive) || category.contains(m_searchText, Qt::CaseInsensitive)
               || mime.contains(m_searchText, Qt::CaseInsensitive);
    }

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        if (m_sortMode == QLatin1String("name")) {
            const int byName = QString::localeAwareCompare(sourceModel()->data(left, CloudModel::NameRole).toString(),
                                                           sourceModel()->data(right, CloudModel::NameRole).toString());
            return byName != 0 ? byName < 0 : sourceModel()->data(left, CloudModel::IdRole).toString() < sourceModel()->data(right, CloudModel::IdRole).toString();
        }
        if (m_sortMode == QLatin1String("size")) {
            const qint64 a = sourceModel()->data(left, CloudModel::SizeRole).toLongLong();
            const qint64 b = sourceModel()->data(right, CloudModel::SizeRole).toLongLong();
            return a != b ? a > b : sourceModel()->data(left, CloudModel::IdRole).toString() < sourceModel()->data(right, CloudModel::IdRole).toString();
        }
        const QDateTime a = sourceModel()->data(left, CloudModel::AddedRole).toDateTime();
        const QDateTime b = sourceModel()->data(right, CloudModel::AddedRole).toDateTime();
        return a != b ? (m_sortMode == QLatin1String("oldest") ? a < b : a > b)
                      : sourceModel()->data(left, CloudModel::IdRole).toString() < sourceModel()->data(right, CloudModel::IdRole).toString();
    }

private:
    QString m_searchText;
    QString m_sortMode = QStringLiteral("newest");
};

class CloudUploadModel : public QAbstractListModel
{
    Q_OBJECT
public:
    struct Upload {
        QString id, name, status, error;
        int progress = -1;
    };
    enum Role { UploadIdRole = Qt::UserRole + 1, FileNameRole, StatusRole, ErrorRole, ProgressRole };

    explicit CloudUploadModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{UploadIdRole, "uploadId"}, {FileNameRole, "fileName"}, {StatusRole, "status"}, {ErrorRole, "error"}, {ProgressRole, "progress"}};
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_rows.size())
            return {};
        const Upload &u = m_rows.at(index.row());
        switch (role) {
        case UploadIdRole: return u.id;
        case FileNameRole: return u.name;
        case StatusRole: return u.status;
        case ErrorRole: return u.error;
        case ProgressRole: return u.progress;
        }
        return {};
    }

    const Upload *find(const QString &id) const
    {
        for (const Upload &upload : m_rows)
            if (upload.id == id)
                return &upload;
        return nullptr;
    }
    void append(const Upload &upload)
    {
        const int row = int(m_rows.size());
        beginInsertRows({}, row, row);
        m_rows.append(upload);
        endInsertRows();
    }
    void update(const QString &id, const QString &status, int progress, const QString &error = {})
    {
        for (int i = 0; i < m_rows.size(); ++i) {
            Upload &u = m_rows[i];
            if (u.id != id)
                continue;
            if (u.status == status && u.progress == progress && u.error == error)
                return;
            u.status = status;
            u.progress = progress;
            u.error = error;
            emit dataChanged(index(i), index(i), {StatusRole, ProgressRole, ErrorRole});
            return;
        }
    }
    int pendingCount() const
    {
        int count = 0;
        for (const Upload &upload : m_rows)
            if (upload.status == QLatin1String("Queued") || upload.status == QLatin1String("Preparing") || upload.status == QLatin1String("Uploading"))
                ++count;
        return count;
    }

private:
    QList<Upload> m_rows;
};
