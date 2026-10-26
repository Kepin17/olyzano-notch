#pragma once

#include "platform/LrcParser.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QUrl>
#include <QVariantList>

class QNetworkReply;

// Lyrics for the song that plays, as `lyrics` in QML. Off by default: turning it on sends the artist, title,
// album and length of what plays to lrclib.net (an open lyrics service, no account), and nothing else. Found
// lyrics are cached on disk (a miss for a week), so a song is asked about once. Settings: notch/lyrics.conf.
class Lyrics : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    // It has been switched on once (in Settings, where it says what is sent): the Music page may then toggle it.
    Q_PROPERTY(bool consented READ consented NOTIFY settingsChanged)
    // off | none (nothing playing, or no lyrics) | loading | synced | plain | error
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY changed) // synced: [{ms, text}]
    Q_PROPERTY(QString plain READ plain NOTIFY changed)      // unsynced words
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY indexChanged) // -1 before the first line
    Q_PROPERTY(QString currentText READ currentText NOTIFY indexChanged)
    Q_PROPERTY(int offsetMs READ offsetMs WRITE setOffsetMs NOTIFY indexChanged) // + brings the words sooner

public:
    // Empty paths: the usual config and cache folders.
    explicit Lyrics(const QString &settingsPath = {}, const QString &cacheDir = {}, QObject *parent = nullptr);

    bool enabled() const { return m_settings.value(QStringLiteral("enabled"), true).toBool(); }
    void setEnabled(bool on);
    bool consented() const { return m_settings.value(QStringLiteral("consented"), true).toBool(); }
    QString state() const { return m_state; }
    QVariantList lines() const { return m_lineList; }
    QString plain() const { return m_plain; }
    int currentIndex() const { return m_index; }
    QString currentText() const { return m_index >= 0 && m_index < m_lines.size() ? m_lines[m_index].text : QString(); }
    int offsetMs() const { return m_offset; }
    void setOffsetMs(int ms);

    // The song that plays now (all empty: nothing does). Asks the service only when it is a new song.
    Q_INVOKABLE void track(const QString &artist, const QString &title, const QString &album, qint64 lengthMs);
    // Where the song is, in ms: moves the current line.
    Q_INVOKABLE void setPosition(qint64 ms);
    Q_INVOKABLE void clearCache();

    void setBaseUrl(const QUrl &url) { m_base = url; } // tests
    int requests() const { return m_requests; }        // tests: how many times the service was asked

signals:
    void settingsChanged();
    void changed();
    void indexChanged();

private:
    struct Query {
        QString artist, title, album;
        qint64 lengthMs = 0;
    };
    void lookup();
    void ask(const QUrl &url, int stage);
    static bool hasWords(QNetworkReply *reply); // a search answer with something to show
    void finish(const QString &state, const QString &synced, const QString &plain, bool store);
    void setState(const QString &state);
    QString cachePath(const Query &q) const;
    bool loadCached(const Query &q);
    void reindex();

    QSettings m_settings;
    QString m_cacheDir;
    QNetworkAccessManager m_nam;
    QUrl m_base{QStringLiteral("https://lrclib.net")};
    Query m_query;
    QString m_key;
    QString m_state;
    QList<Lrc::Line> m_lines;
    QVariantList m_lineList;
    QString m_plain;
    int m_index = -1;
    int m_offset = 0;
    qint64 m_position = 0;
    int m_requests = 0;
    QNetworkReply *m_reply = nullptr;
};
