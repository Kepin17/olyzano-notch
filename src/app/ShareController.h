#pragma once

#include "app/NotchController.h"
#include "app/ShareSession.h"

#include <QAbstractListModel>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QTimer>

// "Share via QR", exposed to QML as `share`: a list of sessions (ShareSession), several at once, plus the
// servers (relays) the user has saved. Relay sessions are remembered in sessions.json (owner-only, the links
// hold keys) and come back after a restart; local ones end with Notch.
// Servers: servers.json (id, name, address) with each API key in its own owner-only file servers/<id>.key.
class ShareController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged) // sessions listed, running or finished
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged) // {id, name, url, hasKey, ok, tested, error, member, limits...}
    Q_PROPERTY(QString defaultServerId READ defaultServerId WRITE setDefaultServerId NOTIFY serversChanged)
    Q_PROPERTY(QString lastServerId READ lastServerId NOTIFY serversChanged) // the one the last relay share used
    Q_PROPERTY(bool relayConfigured READ relayConfigured NOTIFY serversChanged)
    Q_PROPERTY(int defaultTtl READ defaultTtl WRITE setDefaultTtl NOTIFY settingsChanged) // seconds
    Q_PROPERTY(QString lastMode READ lastMode WRITE setLastMode NOTIFY settingsChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ModeRole,
        StatusRole,
        UrlRole,
        ProgressRole,
        ExpiresAtRole,
        FileCountRole,
        DownloadsRole,
        MessageRole,
        EndReasonRole,
        ProtectionRole,
        ActiveRole,
        SingleUseRole,
        ServerNameRole,
    };

    explicit ShareController(NotchController *notch, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_sessions.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeCount() const;
    QVariantList servers() const;
    QString defaultServerId() const;
    QString lastServerId() const { const QString id = m_settings.value(QStringLiteral("lastServer")).toString(); return serverIndex(id) >= 0 ? id : defaultServerId(); }
    void setDefaultServerId(const QString &id);
    bool relayConfigured() const { return !m_servers.isEmpty(); }
    int defaultTtl() const { return m_settings.value(QStringLiteral("ttl"), 3600).toInt(); }
    void setDefaultTtl(int seconds);
    QString lastMode() const { return m_settings.value(QStringLiteral("mode"), QStringLiteral("local")).toString(); }
    void setLastMode(const QString &mode);

    // Starts a new session and returns its id. mode: local | relay. protection: "" | pin | password with
    // `secret` as the PIN / password (a relay PIN is checked by the server, a relay password locks the key in
    // the link, local ones are checked by Notch). oneDownload applies to a single file only. serverId: which
    // saved server a relay share goes to ("" = the default).
    Q_INVOKABLE QString start(const QStringList &ids, const QString &mode, int ttlSeconds, const QString &protection,
                              const QString &secret, bool oneDownload, const QString &address, const QString &serverId = {});
    Q_INVOKABLE void stop(const QString &id);         // ends a running session
    Q_INVOKABLE void dismiss(const QString &id);      // removes a finished / failed one from the list
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE QString checkSecret(const QString &protection, const QString &secret) const { return ShareSession::checkSecret(protection, secret); }
    Q_INVOKABLE QStringList lanAddresses() const;

    // Servers. addServer returns "" or why it was refused. joinServer takes an invitation link from a host
    // (https://host/join#code), trades it for a key of its own and saves the server; it answers through joined.
    Q_INVOKABLE QString addServer(const QString &name, const QString &url, const QString &key);
    Q_INVOKABLE void removeServer(const QString &id);
    Q_INVOKABLE void renameServer(const QString &id, const QString &name);
    Q_INVOKABLE void joinServer(const QString &invitation);
    Q_INVOKABLE void testServer(const QString &id);   // answers through serverTested, and refreshes the limits shown
    Q_INVOKABLE void testAllServers();

signals:
    void countChanged();
    void settingsChanged();
    void serversChanged();
    void serverTested(const QString &id, bool ok, const QString &message);
    void joined(bool ok, const QString &message);

private:
    struct Server {
        QString id, name;
        QUrl url;
    };
    int serverIndex(const QString &id) const;
    QString keyOf(const QString &id) const;
    bool storeKey(const QString &id, const QString &key);
    RelayAccess accessFor(const QString &id) const; // the default server for an unknown / empty id
    void loadServers();
    void saveServers();
    QString saveNewServer(const QString &name, const QUrl &url, const QString &key); // -> id
    void migrateOldServer();
    QString serversFolder() const;
    void add(ShareSession *s);
    int rowOf(const QObject *session) const;
    void scheduleSave() { m_saveTimer.start(); }
    void saveNow();
    void restore();

    NotchController *m_notch;
    QSettings m_settings;
    QNetworkAccessManager m_nam;
    QList<ShareSession *> m_sessions;
    QList<Server> m_servers;
    QHash<QString, QVariantMap> m_status; // server id -> what the last test found (member, limits, usage)
    QTimer m_saveTimer;
};
