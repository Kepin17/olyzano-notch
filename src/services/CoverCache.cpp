#include "CoverCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QMutexLocker>
#include <QQuickImageProvider>
#include <QStandardPaths>

namespace {
constexpr qsizetype MaxBytes = 8 * 1024 * 1024; // the decoded pictures together
constexpr int MaxEntries = 6;

// The provider behind image://cover/<md5>; the engine owns it and only holds a pointer to the cache.
class CoverProvider : public QQuickImageProvider
{
public:
    explicit CoverProvider(CoverCache *cache) : QQuickImageProvider(QQuickImageProvider::Image), m_cache(cache) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        const QImage img = m_cache->load(id, requestedSize);
        if (size)
            *size = img.size();
        return img;
    }

private:
    CoverCache *m_cache;
};
} // namespace

CoverCache::CoverCache(QObject *parent) : QObject(parent)
{
    // a fresh folder every start: the copies are a cache, not a store
    QDir(dir()).removeRecursively();
    QDir().mkpath(dir());
}

QString CoverCache::dir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/media-cover");
}

QUrl CoverCache::cover(const QUrl &source)
{
    if (source.scheme() != QLatin1String("file"))
        return source;
    const QString path = source.toLocalFile();
    QFile in(path);
    if (path.isEmpty() || !in.open(QIODevice::ReadOnly))
        return source;
    const QByteArray bytes = in.readAll(); // covers are small, a few hundred KB at most
    if (bytes.isEmpty())
        return source;
    const QString key = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
    const QString target = dir() + QLatin1Char('/') + key;
    if (!QFile::exists(target))
        QFile::copy(path, target); // same bytes → same address, from anywhere
    return QUrl::fromLocalFile(target);
}

QImage CoverCache::load(const QString &key, const QSize &requestedSize)
{
    QMutexLocker lock(&m_lock);
    for (int i = 0; i < m_lru.size(); ++i) {
        if (m_lru.at(i).key == key && m_lru.at(i).size == requestedSize) {
            m_lru.move(i, 0); // newest first; eviction takes from the back
            return m_lru.first().image;
        }
    }
    QImageReader r(dir() + QLatin1Char('/') + key);
    r.setAutoTransform(true); // the cover lying on its side, like Qt's own loading
    const QSize natural = r.size();
    if (natural.isValid() && requestedSize.isValid() && !requestedSize.isEmpty()) {
        QSize fit = natural;
        fit.scale(requestedSize, Qt::KeepAspectRatio); // sourceSize means "at most this", never stretched
        if (fit != natural)
            r.setScaledSize(fit);
    }
    const QImage img = r.read();
    if (img.isNull())
        return {}; // nothing readable: the Image shows its placeholder
    m_lru.prepend({key, requestedSize, img, img.sizeInBytes()});
    m_bytes += img.sizeInBytes();
    while (m_lru.size() > MaxEntries || m_bytes > MaxBytes) {
        m_bytes -= m_lru.takeLast().bytes;
        // the copy on disk stays: it is wiped on the next start and lets a revisit decode again instead of failing
    }
    return img;
}

QQuickImageProvider *CoverCache::makeProvider(CoverCache *cache)
{
    return new CoverProvider(cache);
}
