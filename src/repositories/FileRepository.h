#pragma once

#include "models/FileItem.h"

#include <QList>
#include <QPair>
#include <QSet>
#include <QVariantList>
#include <functional>

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
    // Many at once, in one transaction (one disk write instead of one each); all or none.
    bool setRemovedMany(const QStringList &ids, const QDateTime &at);
    bool setStatusMany(const QStringList &ids, const QString &status);
    bool removeMany(const QStringList &ids); // rows of file_items and their search text
    QList<FileItem> removedBefore(const QDateTime &cutoff); // never vault items
    bool setVault(const QString &itemId, bool vault);
    bool renameItem(const QString &itemId, const QString &originalName, const QString &storedName,
                    const QString &mimeType);

    bool setCategory(const QString &itemId, const QString &categoryId); // empty = none
    QList<Category> categories();
    QString categoryIdByName(const QString &name);
    bool insertCategory(const Category &c);
    bool renameCategory(const QString &id, const QString &name);
    bool deleteCategory(const QString &id); // its items become uncategorized
    QStringList staleCategories(const QDateTime &now, const QDateTime &cutoff); // empty since cutoff or earlier

    // Search inside text files: SQLite FTS5, made when the database is opened. Without FTS5 in this SQLite, ftsReady() is false
    // and search matches names only.
    bool ftsReady() const { return m_fts; }
    QSet<QString> indexedIds();                                      // ids that have their text in the index
    bool indexTexts(const QList<QPair<QString, QString>> &texts);    // {id, text}, replacing what was there; one transaction
    QSet<QString> searchText(const QString &words);                  // ids whose text has every word (each as a prefix)
    static QString matchQuery(const QString &words);                 // the FTS5 query for typed words: quoted, never an operator

    static QString toIso(const QDateTime &dt);

private:
    bool migrate(const QString &dbPath);
    void openText();
    bool dropText(const QString &id);
    bool inTransaction(const std::function<bool()> &work);
    bool exec(const QString &sql, const QVariantList &binds);
    QList<FileItem> select(const QString &where, const QVariantList &binds);

    QString m_conn;
    QString m_error;
    bool m_fts = false;
};
