#pragma once

#include "app/NotchController.h"
#include "services/ShareServer.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

// "Share via QR": sends the selected files to another device, exposed to QML as `share`.
//   local: ShareServer offers the file on this LAN (plain HTTP).
//   relay: the file is encrypted (ShareCrypto) and uploaded to the user's own relay server (server/);
//          the key stays in the link's #fragment.
// One share at a time. `status`: idle → preparing → [uploading →] ready → ended, or error.
class ShareController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)         // local | relay
    Q_PROPERTY(QString url READ url NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)  // upload 0..1, -1 = unknown
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY changed)
    Q_PROPERTY(int downloads READ downloads NOTIFY changed)   // completed downloads (local shares)
    Q_PROPERTY(qint64 expiresAt READ expiresAt NOTIFY changed) // epoch seconds
    Q_PROPERTY(QString message READ message NOTIFY changed)   // error text, or why it ended
    Q_PROPERTY(QString endReason READ endReason NOTIFY changed) // downloaded | expired | stopped
    Q_PROPERTY(QString relayUrl READ relayUrl WRITE setRelayUrl NOTIFY settingsChanged)
    Q_PROPERTY(bool relayKeySet READ relayKeySet NOTIFY settingsChanged)
    Q_PROPERTY(bool relayConfigured READ relayConfigured NOTIFY settingsChanged)
    Q_PROPERTY(int defaultTtl READ defaultTtl WRITE setDefaultTtl NOTIFY settingsChanged) // seconds
    Q_PROPERTY(QString lastMode READ lastMode WRITE setLastMode NOTIFY settingsChanged)

public:
    explicit ShareController(NotchController *notch, QObject *parent = nullptr);

    QString status() const { return m_status; }
    QString mode() const { return m_mode; }
    QString url() const { return m_link; }
    double progress() const { return m_progress; }
    QString name() const { return m_name; }
    int fileCount() const { return m_fileCount; }
    int downloads() const { return m_downloads; }
    qint64 expiresAt() const { return m_expiresAt; }
    QString message() const { return m_message; }
    QString endReason() const { return m_endReason; }
    QString relayUrl() const { return m_settings.value(QStringLiteral("relayUrl")).toString(); }
    void setRelayUrl(const QString &url);
    bool relayKeySet() const { return !relayKey().isEmpty(); }
    bool relayConfigured() const { return normalizedRelay().isValid() && relayKeySet(); }
    int defaultTtl() const { return m_settings.value(QStringLiteral("ttl"), 3600).toInt(); }
    void setDefaultTtl(int seconds);
    QString lastMode() const { return m_settings.value(QStringLiteral("mode"), QStringLiteral("local")).toString(); }
    void setLastMode(const QString &mode);

    // ttlSeconds: how long the link works. pin / oneDownload apply to relay shares only, and
    // oneDownload only to a single file. A local share of one file ends after its first download;
    // several files stay available until the time is up or stop().
    Q_INVOKABLE void start(const QStringList &ids, const QString &mode, int ttlSeconds, const QString &pin,
                           bool oneDownload, const QString &address);
    Q_INVOKABLE void stop();  // ends a running share, or clears a finished / failed one
    Q_INVOKABLE QStringList lanAddresses() const;
    Q_INVOKABLE void setRelayKey(const QString &key); // kept in relay.key (0600), never in notch.conf
    Q_INVOKABLE void testRelay();                      // answers through relayTested

signals:
    void changed();
    void settingsChanged();
    void relayTested(bool ok, const QString &message);

private:
    struct Prepared {
        QString error, workDir, encryptedPath;
        QList<ShareServer::Entry> entries; // local: what to serve (names made unique)
        QByteArray key;
    };
    static Prepared prepare(const QList<NotchController::ShareSource> &sources, bool encrypt, const QString &workRoot);

    QString relayKey() const;
    QUrl normalizedRelay() const; // https only (http for localhost, for testing); invalid when unset
    QNetworkRequest relayRequest(const QString &path) const;
    static QString networkError(QNetworkReply *reply);
    void setStatus(const QString &status);
    void fail(const QString &why);
    void end(const QString &reason);
    void clean();
    void onPrepared(const Prepared &p);
    void upload(const Prepared &p);
    void pollRelay();

    NotchController *m_notch;
    QSettings m_settings;
    QNetworkAccessManager m_nam;
    ShareServer m_server;
    QTimer m_expire, m_poll;
    QNetworkReply *m_upload = nullptr;
    int m_generation = 0; // bumped on every start/stop so late results of an old share are dropped

    QString m_status = QStringLiteral("idle"), m_mode, m_link, m_name, m_message, m_endReason, m_workDir;
    QString m_relayId, m_pin;
    QHostAddress m_bindAddress; // local mode: the LAN address the server listens on
    double m_progress = -1;
    int m_fileCount = 0, m_downloads = 0;
    qint64 m_expiresAt = 0;
    int m_ttl = 3600;
    bool m_oneDownload = true;
};
