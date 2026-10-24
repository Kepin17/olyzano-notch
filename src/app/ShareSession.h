#pragma once

#include "app/NotchController.h"
#include "services/ShareServer.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

// Where the user's own relay server (server/) is and how to log in to it.
struct RelayAccess {
    QString id, name;  // the saved server profile this is
    QUrl base;         // https://share.example.com (no trailing slash)
    QByteArray apiKey;
    bool valid() const { return base.isValid() && !apiKey.isEmpty(); }
    QNetworkRequest request(const QString &path) const;
};

// One thing being shared with a QR code. Several can run at once (see ShareController).
//   local: a ShareServer offers the files on this LAN (plain HTTP); it ends with Notch.
//   relay: the files are encrypted (ShareCrypto) and uploaded to the relay; the key stays in the link's
//          #fragment. It lives on the server, so it survives Notch restarts (toJson / fromJson).
// status: preparing → [uploading →] ready → ended, or error.
class ShareSession : public QObject
{
    Q_OBJECT
public:
    struct Options {
        QString mode;            // local | relay
        int ttl = 3600;          // seconds the link works
        QString protection;      // "" | pin | password
        QString secret;          // the PIN / password
        bool oneDownload = true; // one file only: the link ends after its first download
        QHostAddress address;    // local: the LAN address to listen on
        QString workRoot;        // where temporary files go (tmpfs when a locked file is shared)
    };

    ShareSession(QNetworkAccessManager *nam, const RelayAccess &relay, QObject *parent = nullptr);

    // "" when fine, else why the PIN / password is not acceptable.
    static QString checkSecret(const QString &protection, const QString &secret);

    const QString &id() const { return m_id; }
    const QString &mode() const { return m_mode; }
    const QString &name() const { return m_name; }
    const QString &status() const { return m_status; }
    const QString &url() const { return m_link; }
    const QString &message() const { return m_message; }
    const QString &endReason() const { return m_endReason; }
    const QString &protection() const { return m_protection; }
    const QString &serverId() const { return m_relay.id; }
    const QString &serverName() const { return m_relay.name; }
    double progress() const { return m_progress; }
    int fileCount() const { return m_fileCount; }
    int downloads() const { return m_downloads; }
    qint64 expiresAt() const { return m_expiresAt; }
    bool singleUse() const { return m_singleUse; }
    bool active() const { return m_status == QLatin1String("preparing") || m_status == QLatin1String("uploading") || m_status == QLatin1String("ready"); }
    bool survivesRestart() const { return m_mode == QLatin1String("relay") && m_status == QLatin1String("ready"); }

    void begin(const QList<NotchController::ShareSource> &sources, const Options &options);
    void stop();    // the user ends it: a relay share is deleted from the server too
    void suspend(); // Notch quits: a local share stops, a relay share is left on the server

    QJsonObject toJson() const;
    static ShareSession *fromJson(const QJsonObject &o, QNetworkAccessManager *nam, const RelayAccess &relay, QObject *parent);

signals:
    void changed();

private:
    struct Prepared {
        QString error, workDir, encryptedPath, fragment;
        QList<ShareServer::Entry> entries; // local: what to serve (names made unique)
    };
    static Prepared prepare(const QList<NotchController::ShareSource> &sources, const Options &options, const QString &workRoot);

    void setStatus(const QString &status);
    void fail(const QString &why);
    void end(const QString &reason);
    void clean();
    void onPrepared(const Prepared &p);
    void upload(const Prepared &p);
    void poll();
    void watchRelay();

    QNetworkAccessManager *m_nam;
    RelayAccess m_relay;
    ShareServer m_server;
    QTimer m_expire, m_poll;
    QNetworkReply *m_upload = nullptr;
    int m_generation = 0; // bumped on every start/stop so late results of an old attempt are dropped

    QString m_id, m_mode, m_name, m_status = QStringLiteral("preparing"), m_link, m_message, m_endReason;
    QString m_protection, m_secret, m_relayId, m_workDir;
    QHostAddress m_bindAddress;
    double m_progress = -1;
    int m_fileCount = 0, m_downloads = 0, m_ttl = 3600;
    qint64 m_expiresAt = 0;
    bool m_singleUse = false;
};
