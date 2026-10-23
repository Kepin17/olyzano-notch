#pragma once

#include "FileItem.h"

#include <QAbstractListModel>

// Updated incrementally (never reset after load) so a delegate being dragged out
// is not destroyed when another item changes.
class FileItemModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        SizeTextRole,
        AddedTextRole,
        ExpiresTextRole,
        StatusRole,
        IconNameRole,
        FileUrlRole,
        SourcePathRole,
        IsImageRole,
        IsTextRole,
        CategoryIdRole,
        PinnedRole,
        AddedAtRole,
        SizeRole,
        MimeTypeRole,
        RemovedRole,     // in Recently removed
        RemovedTextRole, // "deleted in 21h"
        ExpiringSoonRole,
        VaultRole,
    };

    explicit FileItemModel(const QString &filesDir, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QList<FileItem> &items);
    void upsert(const FileItem &item);
    void removeId(const QString &id);
    const FileItem *find(const QString &id) const;
    void refreshTimes();
    void setFilesDir(const QString &filesDir); // only while empty
    void setRetentionDays(int days);
    const QList<FileItem> &items() const { return m_items; }
    // Sets categoryId on every item whose id is in ids.
    void setCategory(const QStringList &ids, const QString &categoryId);
    void clearCategory(const QString &from);
    void setPinned(const QStringList &ids, bool pinned);
    void setRemoved(const QStringList &ids, const QDateTime &at); // invalid at = restore
    void setVaultDir(const QString &dir) { m_vaultDir = dir; }
    void vaultStateChanged(); // file URLs of vault items become (in)valid
    int shelfCount() const;   // items on the shelf: not in Recently removed, not in the vault
    int removedCount() const;

private:
    int indexOf(const QString &id) const;

    QString m_filesDir;
    QString m_vaultDir;
    int m_retentionDays = DefaultRetentionDays;
    QList<FileItem> m_items;
};
