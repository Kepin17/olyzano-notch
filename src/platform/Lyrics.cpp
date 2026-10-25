#include "platform/Lyrics.h"

#include "platform/LyricsQuery.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrlQuery>

namespace {
QString defaultSettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/lyrics.conf");
}
QString defaultCacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/lyrics");
}
constexpr qint64 MissDays = 7;
}

Lyrics::Lyrics(const QString &settingsPath, const QString &cacheDir, QObject *parent)
    : QObject(parent)
    , m_settings(settingsPath.isEmpty() ? defaultSettingsPath() : settingsPath, QSettings::IniFormat)
    , m_cacheDir(cacheDir.isEmpty() ? defaultCacheDir() : cacheDir)
    , m_state(QStringLiteral("off"))
{
    if (enabled())
        m_state = QStringLiteral("none");
}

void Lyrics::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit changed();
}

void Lyrics::setEnabled(bool on)
{
    if (on == enabled())
        return;
    m_settings.setValue(QStringLiteral("enabled"), on);
    if (on)
        m_settings.setValue(QStringLiteral("consented"), true);
    if (!on) {
        if (m_reply) {
            m_reply->abort(); // finished() runs with the reply gone
            m_reply = nullptr;
        }
        m_lines.clear();
        m_lineList.clear();
        m_plain.clear();
        m_key.clear();
        m_index = -1;
        m_state = QStringLiteral("off");
        emit changed();
        emit indexChanged();
    } else {
        setState(QStringLiteral("none"));
        lookup();
    }
    emit settingsChanged();
}

void Lyrics::setOffsetMs(int ms)
{
    ms = qBound(-10000, ms, 10000);
    if (ms == m_offset)
        return;
    m_offset = ms;
    reindex();
    emit indexChanged();
}

void Lyrics::track(const QString &artist, const QString &title, const QString &album, qint64 lengthMs)
{
    const QString cleanArtist = LyricsQuery::cleanArtist(artist);
    Query q{cleanArtist, LyricsQuery::cleanTitle(title.trimmed(), cleanArtist), album.trimmed(), lengthMs};
    const QString key = (q.artist + u'\x1f' + q.title + u'\x1f' + QString::number(lengthMs / 1000)).toLower();
    if (key == m_key)
        return; // the same song: nothing to ask again
    m_query = q;
    m_key = key;
    lookup();
}

void Lyrics::lookup()
{
    if (m_reply) {
        m_reply->abort();
        m_reply = nullptr;
    }
    m_lines.clear();
    m_lineList.clear();
    m_plain.clear();
    m_index = -1;
    if (!enabled()) {
        setState(QStringLiteral("off"));
        emit changed();
        emit indexChanged();
        return;
    }
    emit changed();
    emit indexChanged();
    if (m_query.title.isEmpty()) {
        setState(QStringLiteral("none"));
        return;
    }
    if (loadCached(m_query))
        return;
    setState(QStringLiteral("loading"));
    QUrl url(m_base);
    url.setPath(QStringLiteral("/api/get"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("artist_name"), m_query.artist);
    params.addQueryItem(QStringLiteral("track_name"), m_query.title);
    params.addQueryItem(QStringLiteral("album_name"), m_query.album);
    params.addQueryItem(QStringLiteral("duration"), QString::number(m_query.lengthMs / 1000));
    url.setQuery(params);
    ask(url, 0);
}

// stage 0: the exact lookup; 1: a search by artist and title; 2: a search for the words, keeping the right artist.
void Lyrics::ask(const QUrl &url, int stage)
{
    ++m_requests;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Olyzano Notch/%1 (https://github.com/Kepin17/olyzano-notch)")
                          .arg(QCoreApplication::applicationVersion().isEmpty() ? QStringLiteral("dev") : QCoreApplication::applicationVersion()));
    request.setTransferTimeout(8000);
    const QString key = m_key;
    QNetworkReply *reply = m_nam.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, stage, key] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        if (key != m_key || reply->error() == QNetworkReply::OperationCanceledError)
            return; // another song since, or switched off
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404 || (stage > 0 && reply->error() == QNetworkReply::NoError && !hasWords(reply))) {
            if (stage < 2) { // the exact lookup needs the right album and length: search; then search wider
                QUrl url(m_base);
                url.setPath(QStringLiteral("/api/search"));
                QUrlQuery params;
                if (stage == 0) {
                    params.addQueryItem(QStringLiteral("artist_name"), m_query.artist);
                    params.addQueryItem(QStringLiteral("track_name"), m_query.title);
                } else {
                    params.addQueryItem(QStringLiteral("q"), m_query.title + u' ' + m_query.artist);
                }
                url.setQuery(params);
                ask(url, stage + 1);
            } else {
                finish(QStringLiteral("none"), {}, {}, true);
            }
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            setState(QStringLiteral("error")); // not cached: a later play tries again
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject best;
        const bool wide = stage == 2;
        if (doc.isObject()) {
            best = doc.object();
        } else if (doc.isArray()) { // search: the closest in length that has words, synced first
            qint64 gap = -1;
            for (const QJsonValue &v : doc.array()) {
                const QJsonObject o = v.toObject();
                if (wide && !LyricsQuery::sameArtist(o.value(QLatin1String("artistName")).toString(), m_query.artist))
                    continue; // a search by words finds other songs with those words
                const bool synced = !o.value(QLatin1String("syncedLyrics")).toString().isEmpty();
                const bool plain = !o.value(QLatin1String("plainLyrics")).toString().isEmpty();
                if (!synced && !plain)
                    continue;
                const qint64 g = std::abs(qint64(o.value(QLatin1String("duration")).toDouble() * 1000) - m_query.lengthMs) + (synced ? 0 : 1000000);
                if (gap < 0 || g < gap) {
                    gap = g;
                    best = o;
                }
            }
        }
        if (best.value(QLatin1String("instrumental")).toBool()) {
            finish(QStringLiteral("none"), {}, {}, true);
            return;
        }
        finish({}, best.value(QLatin1String("syncedLyrics")).toString(), best.value(QLatin1String("plainLyrics")).toString(), true);
    });
}

// state "none" with no words, or the words themselves (synced preferred).
void Lyrics::finish(const QString &state, const QString &synced, const QString &plain, bool store)
{
    QList<Lrc::Line> lines = synced.isEmpty() ? QList<Lrc::Line>() : Lrc::parse(synced);
    if (store) {
        QDir().mkpath(m_cacheDir);
        QFile f(cachePath(m_query));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(QJsonObject{{QStringLiteral("at"), QDateTime::currentSecsSinceEpoch()},
                                              {QStringLiteral("synced"), synced},
                                              {QStringLiteral("plain"), plain}}).toJson(QJsonDocument::Compact));
    }
    m_lines = lines;
    m_lineList.clear();
    for (const Lrc::Line &l : std::as_const(m_lines))
        m_lineList << QVariantMap{{QStringLiteral("ms"), l.ms}, {QStringLiteral("text"), l.text}};
    m_plain = lines.isEmpty() ? plain.trimmed() : QString();
    m_index = -1;
    reindex();
    if (!state.isEmpty())
        m_state = state;
    else
        m_state = !m_lines.isEmpty() ? QStringLiteral("synced") : !m_plain.isEmpty() ? QStringLiteral("plain") : QStringLiteral("none");
    emit changed();
    emit indexChanged();
}

bool Lyrics::hasWords(QNetworkReply *reply)
{
    const QJsonDocument doc = QJsonDocument::fromJson(reply->peek(reply->bytesAvailable())); // read again later
    const auto words = [](const QJsonObject &o) {
        return !o.value(QLatin1String("syncedLyrics")).toString().isEmpty() || !o.value(QLatin1String("plainLyrics")).toString().isEmpty();
    };
    if (doc.isObject())
        return words(doc.object());
    for (const QJsonValue &v : doc.array())
        if (words(v.toObject()))
            return true;
    return false;
}

QString Lyrics::cachePath(const Query &q) const
{
    const QByteArray key = (q.artist + u'\x1f' + q.title + u'\x1f' + QString::number(q.lengthMs / 1000)).toLower().toUtf8();
    return m_cacheDir + u'/' + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex()) + QStringLiteral(".json");
}

bool Lyrics::loadCached(const Query &q)
{
    QFile f(cachePath(q));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QString synced = o.value(QLatin1String("synced")).toString(), plain = o.value(QLatin1String("plain")).toString();
    if (synced.isEmpty() && plain.isEmpty()
        && QDateTime::currentSecsSinceEpoch() - o.value(QLatin1String("at")).toInteger() > MissDays * 86400)
        return false; // a miss is asked about again after a week
    finish({}, synced, plain, false);
    return true;
}

void Lyrics::setPosition(qint64 ms)
{
    m_position = ms;
    const int before = m_index;
    reindex();
    if (m_index != before)
        emit indexChanged();
}

void Lyrics::reindex()
{
    m_index = Lrc::indexAt(m_lines, m_position + m_offset);
}

void Lyrics::clearCache()
{
    if (!m_cacheDir.isEmpty()) // QDir("") is the current directory
        QDir(m_cacheDir).removeRecursively();
}
