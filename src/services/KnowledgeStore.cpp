#include "KnowledgeStore.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
bool run(QSqlDatabase db, const QString &sql, const QVariantList &args = {})
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant &a : args)
        q.addBindValue(a);
    return q.exec();
}

KnowledgeStore::Hit hitFrom(QSqlQuery &q)
{
    KnowledgeStore::Hit h;
    h.chunkId = q.value(0).toLongLong();
    h.docId = q.value(1).toLongLong();
    h.page = q.value(2).toInt();
    h.startSec = q.value(3).isNull() ? -1 : q.value(3).toDouble();
    h.who = q.value(4).toString();
    h.text = q.value(5).toString();
    h.kind = q.value(6).toString();
    h.ref = q.value(7).toString();
    h.name = q.value(8).toString();
    h.addedAt = QDateTime::fromSecsSinceEpoch(q.value(9).toLongLong(), Qt::UTC);
    h.score = q.value(10).toDouble();
    return h;
}

const char *HitColumns = "c.id, c.doc_id, c.page, c.start_sec, c.who, f.text, d.kind, d.ref, d.name, d.added_at";

// " AND d.kind IN (?,?) ..." and the values
QString where(const KnowledgeStore::Filter &f, QVariantList *args)
{
    QString sql;
    if (!f.kinds.isEmpty()) {
        sql += QStringLiteral(" AND d.kind IN (%1)").arg(QStringList(f.kinds.size(), QStringLiteral("?")).join(u','));
        for (const QString &k : f.kinds)
            *args << k;
    }
    if (!f.refs.isEmpty()) {
        sql += QStringLiteral(" AND d.ref IN (%1)").arg(QStringList(f.refs.size(), QStringLiteral("?")).join(u','));
        for (const QString &r : f.refs)
            *args << r;
    }
    if (f.from.isValid()) {
        sql += QStringLiteral(" AND d.added_at >= ?");
        *args << f.from.toSecsSinceEpoch();
    }
    if (f.to.isValid()) {
        sql += QStringLiteral(" AND d.added_at <= ?");
        *args << f.to.toSecsSinceEpoch();
    }
    return sql;
}
}

KnowledgeStore::KnowledgeStore(const QString &connectionName) : m_conn(connectionName) {}

KnowledgeStore::~KnowledgeStore()
{
    if (m_open) {
        { QSqlDatabase::database(m_conn).close(); }
        QSqlDatabase::removeDatabase(m_conn);
    }
}

QString KnowledgeStore::open(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    db.setDatabaseName(path);
    if (!db.open())
        return db.lastError().text();
    run(db, QStringLiteral("PRAGMA journal_mode=WAL"));
    run(db, QStringLiteral("PRAGMA synchronous=NORMAL"));
    const bool ok = run(db, QStringLiteral("CREATE TABLE IF NOT EXISTS docs(id INTEGER PRIMARY KEY, kind TEXT NOT NULL, ref TEXT NOT NULL, name TEXT NOT NULL, added_at INTEGER NOT NULL, fingerprint TEXT NOT NULL, UNIQUE(kind, ref))"))
                    && run(db, QStringLiteral("CREATE TABLE IF NOT EXISTS chunks(id INTEGER PRIMARY KEY, doc_id INTEGER NOT NULL, page INTEGER NOT NULL, start_sec REAL, who TEXT NOT NULL)"))
                    && run(db, QStringLiteral("CREATE INDEX IF NOT EXISTS chunks_doc ON chunks(doc_id)"))
                    && run(db, QStringLiteral("CREATE TABLE IF NOT EXISTS vectors(chunk_id INTEGER PRIMARY KEY, v BLOB NOT NULL)"))
                    && run(db, QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(text, tokenize = 'unicode61 remove_diacritics 2')"));
    if (!ok) {
        const QString why = db.lastError().text();
        db.close();
        return why.isEmpty() ? QStringLiteral("this SQLite cannot make the index (FTS5 is missing)") : why;
    }
    m_open = true;
    return {};
}

bool KnowledgeStore::removeDoc(const QString &kind, const QString &ref)
{
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id FROM docs WHERE kind=? AND ref=?"));
    q.addBindValue(kind);
    q.addBindValue(ref);
    if (!q.exec() || !q.next())
        return true;
    const qint64 doc = q.value(0).toLongLong();
    q.finish();
    QList<qint64> ids;
    QSqlQuery c(db);
    c.prepare(QStringLiteral("SELECT id FROM chunks WHERE doc_id=?"));
    c.addBindValue(doc);
    if (c.exec())
        while (c.next())
            ids << c.value(0).toLongLong();
    c.finish();
    bool ok = true;
    for (const qint64 id : std::as_const(ids))
        ok = ok && run(db, QStringLiteral("DELETE FROM chunks_fts WHERE rowid=?"), {id}) && run(db, QStringLiteral("DELETE FROM vectors WHERE chunk_id=?"), {id});
    return ok && run(db, QStringLiteral("DELETE FROM chunks WHERE doc_id=?"), {doc}) && run(db, QStringLiteral("DELETE FROM docs WHERE id=?"), {doc});
}

bool KnowledgeStore::setDoc(const QString &kind, const QString &ref, const QString &name, const QDateTime &addedAt, const QString &fingerprint, const QList<Knowledge::Chunk> &chunks)
{
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    if (!db.transaction())
        return false;
    bool ok = removeDoc(kind, ref) && run(db, QStringLiteral("INSERT INTO docs(kind, ref, name, added_at, fingerprint) VALUES(?,?,?,?,?)"), {kind, ref, name, addedAt.toSecsSinceEpoch(), fingerprint});
    qint64 doc = 0;
    if (ok) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT id FROM docs WHERE kind=? AND ref=?"));
        q.addBindValue(kind);
        q.addBindValue(ref);
        ok = q.exec() && q.next();
        doc = ok ? q.value(0).toLongLong() : 0;
    }
    for (const Knowledge::Chunk &c : chunks) {
        if (!ok)
            break;
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO chunks(doc_id, page, start_sec, who) VALUES(?,?,?,?)"));
        q.addBindValue(doc);
        q.addBindValue(c.page);
        q.addBindValue(c.startSec >= 0 ? QVariant(c.startSec) : QVariant());
        q.addBindValue(c.who.isNull() ? QStringLiteral("") : c.who); // NOT NULL: an empty string, not a null one
        ok = q.exec();
        if (ok)
            ok = run(db, QStringLiteral("INSERT INTO chunks_fts(rowid, text) VALUES(?,?)"), {q.lastInsertId(), c.text});
    }
    if (!ok) {
        db.rollback();
        return false;
    }
    return db.commit();
}

QString KnowledgeStore::fingerprint(const QString &kind, const QString &ref) const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT fingerprint FROM docs WHERE kind=? AND ref=?"));
    q.addBindValue(kind);
    q.addBindValue(ref);
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

QSet<QString> KnowledgeStore::refs(const QString &kind) const
{
    QSet<QString> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT ref FROM docs WHERE kind=?"));
    q.addBindValue(kind);
    if (q.exec())
        while (q.next())
            out.insert(q.value(0).toString());
    return out;
}

bool KnowledgeStore::clear()
{
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    return db.transaction() && run(db, QStringLiteral("DELETE FROM chunks_fts")) && run(db, QStringLiteral("DELETE FROM vectors")) && run(db, QStringLiteral("DELETE FROM chunks"))
           && run(db, QStringLiteral("DELETE FROM docs")) && db.commit();
}

KnowledgeStore::Stats KnowledgeStore::stats() const
{
    Stats s;
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    const auto one = [&](const char *sql) {
        QSqlQuery q(db);
        return q.exec(QString::fromLatin1(sql)) && q.next() ? q.value(0).toLongLong() : 0;
    };
    s.docs = int(one("SELECT COUNT(*) FROM docs"));
    s.chunks = int(one("SELECT COUNT(*) FROM chunks"));
    s.vectors = int(one("SELECT COUNT(*) FROM vectors"));
    s.bytes = QFileInfo(db.databaseName()).size();
    return s;
}

QString KnowledgeStore::matchQuery(const QStringList &words)
{
    QStringList parts;
    QSet<QString> seen;
    for (QString w : words) {
        w.remove(u'"');
        w = w.trimmed().toLower();
        if (w.size() < 2 || seen.contains(w))
            continue;
        seen.insert(w);
        parts << u'"' + w + QStringLiteral("\"*");
    }
    return parts.join(QStringLiteral(" OR "));
}

QList<KnowledgeStore::Hit> KnowledgeStore::search(const QString &match, const Filter &filter, int limit) const
{
    QList<Hit> out;
    if (match.isEmpty() || !m_open)
        return out;
    QVariantList args{match};
    const QString sql = QStringLiteral("SELECT %1, -bm25(chunks_fts) FROM chunks_fts f JOIN chunks c ON c.id = f.rowid JOIN docs d ON d.id = c.doc_id WHERE chunks_fts MATCH ?")
                            .arg(QLatin1String(HitColumns))
                        + where(filter, &args) + QStringLiteral(" ORDER BY bm25(chunks_fts) LIMIT %1").arg(limit);
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(sql);
    for (const QVariant &a : std::as_const(args))
        q.addBindValue(a);
    if (q.exec())
        while (q.next())
            out.append(hitFrom(q));
    return out;
}

QList<KnowledgeStore::Hit> KnowledgeStore::chunks(const QList<qint64> &ids) const
{
    QList<Hit> out;
    if (ids.isEmpty())
        return out;
    QStringList marks(ids.size(), QStringLiteral("?"));
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT %1, 0 FROM chunks c JOIN chunks_fts f ON f.rowid = c.id JOIN docs d ON d.id = c.doc_id WHERE c.id IN (%2)").arg(QLatin1String(HitColumns), marks.join(u',')));
    for (const qint64 id : ids)
        q.addBindValue(id);
    if (q.exec())
        while (q.next())
            out.append(hitFrom(q));
    return out;
}

QList<KnowledgeStore::Hit> KnowledgeStore::docChunks(const QString &kind, const QString &ref, int limit) const
{
    QList<Hit> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT %1, 0 FROM docs d JOIN chunks c ON c.doc_id = d.id JOIN chunks_fts f ON f.rowid = c.id WHERE d.kind = ? AND d.ref = ? ORDER BY c.id LIMIT %2").arg(QLatin1String(HitColumns)).arg(limit));
    q.addBindValue(kind);
    q.addBindValue(ref);
    if (q.exec())
        while (q.next())
            out.append(hitFrom(q));
    return out;
}

bool KnowledgeStore::setVector(qint64 chunkId, const QByteArray &f16)
{
    return run(QSqlDatabase::database(m_conn), QStringLiteral("INSERT OR REPLACE INTO vectors(chunk_id, v) VALUES(?,?)"), {chunkId, f16});
}

QList<qint64> KnowledgeStore::chunksWithoutVector(int limit) const
{
    QList<qint64> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    if (q.exec(QStringLiteral("SELECT c.id FROM chunks c LEFT JOIN vectors v ON v.chunk_id = c.id WHERE v.chunk_id IS NULL LIMIT %1").arg(limit)))
        while (q.next())
            out << q.value(0).toLongLong();
    return out;
}

QList<QPair<qint64, QByteArray>> KnowledgeStore::vectors(const Filter &filter, int limit) const
{
    QList<QPair<qint64, QByteArray>> out;
    QVariantList args;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT v.chunk_id, v.v FROM vectors v JOIN chunks c ON c.id = v.chunk_id JOIN docs d ON d.id = c.doc_id WHERE 1=1") + where(filter, &args)
              + QStringLiteral(" LIMIT %1").arg(limit));
    for (const QVariant &a : std::as_const(args))
        q.addBindValue(a);
    if (q.exec())
        while (q.next())
            out.append({q.value(0).toLongLong(), q.value(1).toByteArray()});
    return out;
}
