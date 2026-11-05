#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QLocale>
#include <QMimeDatabase>

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
    enum Role { IdRole = Qt::UserRole + 1, NameRole, SizeTextRole, MimeRole, IconNameRole, MineRole, AddedTextRole, LockedRole, PercentRole, BusyRole, CategoryRole };

    explicit CloudModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{IdRole, "cloudId"}, {NameRole, "name"}, {SizeTextRole, "sizeText"}, {MimeRole, "mimeType"}, {IconNameRole, "iconName"},
                {MineRole, "mine"}, {AddedTextRole, "addedText"}, {LockedRole, "locked"}, {PercentRole, "percent"}, {BusyRole, "busy"},
                {CategoryRole, "category"}};
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
        std::sort(rows.begin(), rows.end(), [](const Entry &a, const Entry &b) { return a.added > b.added; });
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
