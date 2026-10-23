#include "FileRepository.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

Q_LOGGING_CATEGORY(lcNotch, "notch")

namespace {
const QStringList Migrations = {
    QStringLiteral(":/migrations/001_initial.sql"),
    QStringLiteral(":/migrations/002_categories.sql"),
    QStringLiteral(":/migrations/003_keep.sql"),
    QStringLiteral(":/migrations/004_recently_removed.sql"),
};
const QString Columns = QStringLiteral(
    "id, original_name, stored_name, mime_type, size_bytes, source_path, added_at, status, category_id, pinned, "
    "removed_at");
}

// Empty string <-> SQL NULL, so "no category" is stored as NULL.
static QVariant nullable(const QString &s)
{
    return s.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(s);
}

FileRepository::~FileRepository()
{
    if (m_conn.isEmpty())
        return;
    QSqlDatabase::database(m_conn, false).close();
    QSqlDatabase::removeDatabase(m_conn);
}

QString FileRepository::toIso(const QDateTime &dt)
{
    // Fixed-width UTC ISO strings compare correctly as text.
    return dt.toUTC().toString(Qt::ISODateWithMs);
}

bool FileRepository::open(const QString &dbPath)
{
    m_conn = QStringLiteral("notch-") + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        m_error = db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA quick_check")) || !q.next() || q.value(0).toString() != QLatin1String("ok")) {
        m_error = QStringLiteral("Database %1 is unreadable or corrupt (%2). Nothing was deleted.")
                      .arg(dbPath, q.lastError().text());
        return false;
    }
    q.finish();
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.finish();
    return migrate(dbPath);
}

bool FileRepository::migrate(const QString &dbPath)
{
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA user_version"));
    q.next();
    const int version = q.value(0).toInt();
    q.finish(); // an active SELECT blocks COMMIT
    if (version > Migrations.size()) {
        m_error = QStringLiteral("Database schema v%1 is newer than this build supports").arg(version);
        return false;
    }
    if (version > 0 && version < Migrations.size()) {
        q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
        QFile::copy(dbPath, dbPath + QStringLiteral(".bak-v%1").arg(version));
    }
    for (int i = version; i < Migrations.size(); ++i) {
        QFile f(Migrations[i]);
        if (!f.open(QIODevice::ReadOnly)) {
            m_error = QStringLiteral("Missing migration %1").arg(Migrations[i]);
            return false;
        }
        db.transaction();
        const QStringList statements = QString::fromUtf8(f.readAll()).split(u';', Qt::SkipEmptyParts);
        for (const QString &stmt : statements) {
            if (stmt.trimmed().isEmpty())
                continue;
            if (!q.exec(stmt)) {
                m_error = QStringLiteral("Migration %1 failed: %2").arg(Migrations[i], q.lastError().text());
                db.rollback();
                return false;
            }
        }
        q.exec(QStringLiteral("PRAGMA user_version = %1").arg(i + 1));
        if (!db.commit()) {
            m_error = db.lastError().text();
            return false;
        }
    }
    return true;
}

bool FileRepository::exec(const QString &sql, const QVariantList &binds)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(sql);
    for (const QVariant &v : binds)
        q.addBindValue(v);
    if (!q.exec()) {
        m_error = q.lastError().text();
        qCWarning(lcNotch) << "SQL failed:" << m_error;
        return false;
    }
    return true;
}

QList<FileItem> FileRepository::select(const QString &where, const QVariantList &binds)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT %1 FROM file_items WHERE %2 ORDER BY added_at DESC").arg(Columns, where));
    for (const QVariant &v : binds)
        q.addBindValue(v);
    QList<FileItem> items;
    if (!q.exec()) {
        m_error = q.lastError().text();
        qCWarning(lcNotch) << "SQL failed:" << m_error;
        return items;
    }
    while (q.next()) {
        FileItem i;
        i.id = q.value(0).toString();
        i.originalName = q.value(1).toString();
        i.storedName = q.value(2).toString();
        i.mimeType = q.value(3).toString();
        i.sizeBytes = q.value(4).toLongLong();
        i.sourcePath = q.value(5).toString();
        i.addedAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
        i.status = q.value(7).toString();
        i.categoryId = q.value(8).toString();
        i.pinned = q.value(9).toBool();
        if (!q.value(10).isNull())
            i.removedAt = QDateTime::fromString(q.value(10).toString(), Qt::ISODateWithMs);
        items << i;
    }
    return items;
}

bool FileRepository::insert(const FileItem &i)
{
    return exec(QStringLiteral("INSERT INTO file_items (%1) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)").arg(Columns),
                {i.id, i.originalName, i.storedName, i.mimeType, i.sizeBytes, i.sourcePath, toIso(i.addedAt), i.status,
                 nullable(i.categoryId), i.pinned ? 1 : 0,
                 i.removedAt.isValid() ? QVariant(toIso(i.removedAt)) : QVariant(QMetaType::fromType<QString>())});
}

bool FileRepository::setStatus(const QString &id, const QString &status)
{
    return exec(QStringLiteral("UPDATE file_items SET status = ? WHERE id = ?"), {status, id});
}

bool FileRepository::markAvailable(const QString &id, qint64 sizeBytes)
{
    return exec(QStringLiteral("UPDATE file_items SET status = ?, size_bytes = ? WHERE id = ?"),
                {Status::Available, sizeBytes, id});
}

bool FileRepository::remove(const QString &id)
{
    return exec(QStringLiteral("DELETE FROM file_items WHERE id = ?"), {id});
}

QList<FileItem> FileRepository::visible()
{
    return select(QStringLiteral("status != ?"), {Status::PendingCleanup});
}

QList<FileItem> FileRepository::withStatus(const QString &status)
{
    return select(QStringLiteral("status = ?"), {status});
}

QList<FileItem> FileRepository::expired(const QDateTime &cutoff)
{
    // Removed items are left to the Recently-removed clock instead.
    return select(QStringLiteral("status IN (?, ?) AND pinned = 0 AND removed_at IS NULL AND added_at <= ?"),
                  {Status::Available, Status::Error, toIso(cutoff)});
}

bool FileRepository::setRemoved(const QString &itemId, const QDateTime &at)
{
    return exec(QStringLiteral("UPDATE file_items SET removed_at = ? WHERE id = ?"),
                {at.isValid() ? QVariant(toIso(at)) : QVariant(QMetaType::fromType<QString>()), itemId});
}

QList<FileItem> FileRepository::removedBefore(const QDateTime &cutoff)
{
    return select(QStringLiteral("status IN (?, ?) AND removed_at IS NOT NULL AND removed_at <= ?"),
                  {Status::Available, Status::Error, toIso(cutoff)});
}

bool FileRepository::renameItem(const QString &itemId, const QString &originalName, const QString &storedName,
                                const QString &mimeType)
{
    return exec(QStringLiteral("UPDATE file_items SET original_name = ?, stored_name = ?, mime_type = ? WHERE id = ?"),
                {originalName, storedName, mimeType, itemId});
}

bool FileRepository::setPinned(const QString &itemId, bool pinned)
{
    return exec(QStringLiteral("UPDATE file_items SET pinned = ? WHERE id = ?"), {pinned ? 1 : 0, itemId});
}

bool FileRepository::setCategory(const QString &itemId, const QString &categoryId)
{
    return exec(QStringLiteral("UPDATE file_items SET category_id = ? WHERE id = ?"), {nullable(categoryId), itemId});
}

QList<Category> FileRepository::categories()
{
    QList<Category> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    if (!q.exec(QStringLiteral("SELECT id, name FROM categories ORDER BY name COLLATE NOCASE"))) {
        m_error = q.lastError().text();
        return out;
    }
    while (q.next())
        out << Category{q.value(0).toString(), q.value(1).toString()};
    return out;
}

QString FileRepository::categoryIdByName(const QString &name)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT id FROM categories WHERE name = ?")); // column is COLLATE NOCASE
    q.addBindValue(name);
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

bool FileRepository::insertCategory(const Category &c)
{
    return exec(QStringLiteral("INSERT INTO categories (id, name, created_at) VALUES (?, ?, ?)"),
                {c.id, c.name, toIso(QDateTime::currentDateTimeUtc())});
}

bool FileRepository::renameCategory(const QString &id, const QString &name)
{
    return exec(QStringLiteral("UPDATE categories SET name = ? WHERE id = ?"), {name, id});
}

bool FileRepository::deleteCategory(const QString &id)
{
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    db.transaction();
    if (exec(QStringLiteral("UPDATE file_items SET category_id = NULL WHERE category_id = ?"), {id})
        && exec(QStringLiteral("DELETE FROM categories WHERE id = ?"), {id}))
        return db.commit();
    db.rollback();
    return false;
}
