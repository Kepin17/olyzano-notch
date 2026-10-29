#pragma once

#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QString>
#include <QUrl>

class QQuickImageProvider;

// One decode per cover. `cover()` gives a local cover file a content address (the same bytes always get the same address, so a
// browser's one reused temp file stays correct), and `load()` keeps the decoded picture in a small bounded LRU that the image
// provider serves: the capsule, the toast and the card share one copy, a song revisited costs no decode, and Low memory mode
// stays bounded (6 entries / 8 MB at most). The content-addressed copies live in the cache folder and are wiped on the next
// start, like the Windows media-art files. Anything not local (http(s)) is left alone: its address already is its identity.
class CoverCache : public QObject
{
    Q_OBJECT
public:
    explicit CoverCache(QObject *parent = nullptr);

    Q_INVOKABLE QUrl cover(const QUrl &source); // file: → image://cover/<md5>; anything else: unchanged

    // served to QML on the image loader threads: a remembered size is returned at once, otherwise it is decoded and remembered
    QImage load(const QString &key, const QSize &requestedSize);

    static QQuickImageProvider *makeProvider(CoverCache *cache); // the engine owns the result

private:
    QString dir() const;

    mutable QMutex m_lock;
    struct Entry {
        QString key; // md5 of the cover's bytes
        QSize size;  // the size it was asked for
        QImage image;
        qsizetype bytes = 0;
    };
    QList<Entry> m_lru; // front = newest
    qsizetype m_bytes = 0;
};
