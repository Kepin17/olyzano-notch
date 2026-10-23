#pragma once

#include "models/FileItem.h"

#include <QList>
#include <QVariantList>

struct Category {
    QString id;
    QString name; // unique, case-insensitive
};

// SQLite metadata store. Main-thread only; one connection per instance.
class FileRepository
{
public:
    ~FileRepository();

    bool open(const QString &dbPath);
    QString errorString() const { return m_error; }

    bool insert(const FileItem &item);
    bool setStatus(const QString &id, const QString &status);
    bool markAvailable(const QString &id, qint64 sizeBytes);
    bool remove(const QString &id);

    QList<FileItem> visible();                      // what the shelf shows, newest first
    QList<FileItem> withStatus(const QString &status);
    QList<FileItem> expired(const QDateTime &cutoff); // unpinned available/error added at or before cutoff
    bool setPinned(const QString &itemId, bool pinned);
    bool setRemoved(const QString &itemId, const QDateTime &at); // invalid at = restore
    QList<FileItem> removedBefore(const QDateTime &cutoff);
    bool renameItem(const QString &itemId, const QString &originalName, const QString &storedName,
                    const QString &mimeType);

    bool setCategory(const QString &itemId, const QString &categoryId); // empty = none
    QList<Category> categories();
    QString categoryIdByName(const QString &name);
    bool insertCategory(const Category &c);
    bool renameCategory(const QString &id, const QString &name);
    bool deleteCategory(const QString &id); // its items become uncategorized

    static QString toIso(const QDateTime &dt);

private:
    bool migrate(const QString &dbPath);
    bool exec(const QString &sql, const QVariantList &binds);
    QList<FileItem> select(const QString &where, const QVariantList &binds);

    QString m_conn;
    QString m_error;
};
