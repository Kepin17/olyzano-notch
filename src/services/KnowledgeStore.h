#pragma once

#include "Chunker.h"

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// What can be searched and asked about: the passages of the PDFs, text files and recordings, in a SQLite file of their own (knowledge.db: it
// can be thrown away and made again, and it never gets in the way of the shelf's database). Words are found with FTS5 (BM25); the vectors of
// the optional embedding model sit beside them. Blocking: use one KnowledgeStore per thread.
class KnowledgeStore
{
public:
    struct Hit {
        qint64 chunkId = 0, docId = 0;
        QString kind, ref, name; // shelf | meeting, the file's id or the meeting's id, its name
        int page = 0;
        double startSec = -1;
        QString who, text;
        double score = 0;        // higher is better
        QDateTime addedAt;
    };
    struct Filter {
        QStringList kinds;       // shelf | meeting (empty: both)
        QStringList refs;        // only these documents (empty: all)
        QDateTime from, to;      // when the document was added (invalid: no limit)
        QStringList mimeGroups;  // reserved
    };
    struct Stats {
        int docs = 0, chunks = 0, vectors = 0;
        qint64 bytes = 0;
    };

    explicit KnowledgeStore(const QString &connectionName = QStringLiteral("knowledge"));
    ~KnowledgeStore();
    KnowledgeStore(const KnowledgeStore &) = delete;
    KnowledgeStore &operator=(const KnowledgeStore &) = delete;

    QString open(const QString &path); // "" or why not (a SQLite without FTS5 cannot do this)
    bool isOpen() const { return m_open; }

    // The document's passages, replacing what was there. `fingerprint` says what they were made from.
    bool setDoc(const QString &kind, const QString &ref, const QString &name, const QDateTime &addedAt, const QString &fingerprint, const QList<Knowledge::Chunk> &chunks);
    bool removeDoc(const QString &kind, const QString &ref);
    QString fingerprint(const QString &kind, const QString &ref) const; // "" when it is not there
    QSet<QString> refs(const QString &kind) const;
    bool clear();
    Stats stats() const;

    // `match`: an FTS5 query (see matchQuery). Best first.
    QList<Hit> search(const QString &match, const Filter &filter, int limit = 40) const;
    // The documents whose NAME has all of these words (a part of a word counts), newest first: {kind, ref, name}.
    struct NameHit {
        QString kind, ref, name;
    };
    QList<NameHit> searchNames(const QStringList &words, const QStringList &kinds, int limit = 10) const;
    // The text of passages by id (for ranking by vectors and for context)
    QList<Hit> chunks(const QList<qint64> &ids) const;
    QList<Hit> docChunks(const QString &kind, const QString &ref, int limit = 200) const; // the beginning of one document

    // vectors (float16, normalised) of the optional embedding model
    bool setVector(qint64 chunkId, const QByteArray &f16);
    QList<qint64> chunksWithoutVector(int limit) const;
    QList<QPair<qint64, QByteArray>> vectors(const Filter &filter, int limit = 100000) const;

    // "kuitansi OR listrik*" from words: each as a prefix, any of them (BM25 puts the passages that have more of them first)
    static QString matchQuery(const QStringList &words);

private:
    QString m_conn;
    bool m_open = false;
};
