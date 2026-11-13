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
    QNetworkRequest request(const QString &path) const // `path` may carry a query: "/api/v1/shares/x?since=3"
    {
        QUrl url = base;
        const QString full = url.path() + path;
        url.setPath(full.section(QLatin1Char('?'), 0, 0));
        if (full.contains(QLatin1Char('?')))
            url.setQuery(full.section(QLatin1Char('?'), 1));
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", "Bearer " + apiKey);
        req.setTransferTimeout(120000);
        return req;
    }
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
        int maxDownloads = 0;    // the whole share may be downloaded this many times, then it ends (0 = no limit)
        int maxPeople = 0;       // this many different devices may start downloading (0 = no limit)
        bool relayPeopleLimit = true; // the relay understands maxPeople (an older one would ignore it)
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
    int maxDownloads() const { return m_maxDownloads; }
    int people() const { return m_people; }
    int maxPeople() const { return m_maxPeople; }
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
    // Something happened on the link: kind "download" (a device took the whole share), "turned_away" (a device came after the places were used up)
    // or "limit" (the downloads allowed were all taken and the link ended). `device` is DeviceLabel's words; `what` the name to say.
    void activity(const QString &kind, const QString &device, const QString &what);

private:
    struct Prepared {
        QString error, workDir, encryptedPath, fragment;
        QList<ShareServer::Entry> entries; // local: what to serve (names made unique)
    };
    static Prepared prepare(const QList<NotchController::ShareSource> &sources, const Options &options, const QString &workRoot);

    QString takenWhat() const; // what a device took: the file, or "all 3 files"
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
    int m_eventSeen = -1; // relay: the last event of the server that has been told (-1: not known yet, the first answer only gives the place)
    int m_fileCount = 0, m_downloads = 0, m_ttl = 3600, m_maxDownloads = 0, m_maxPeople = 0, m_people = 0;
    qint64 m_expiresAt = 0;
    bool m_singleUse = false;
};
