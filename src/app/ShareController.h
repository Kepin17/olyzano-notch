#pragma once

#include "app/NotchController.h"
#include "app/ShareSession.h"

#include <QAbstractListModel>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QTimer>

// "Share via QR", exposed to QML as `share`: a list of sessions (ShareSession), several at once, plus the
// relay settings. Relay sessions are remembered in sessions.json (owner-only, the links hold keys) and come
// back after a restart; local ones end with Notch.
class ShareController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged) // sessions listed, running or finished
    Q_PROPERTY(QString relayUrl READ relayUrl WRITE setRelayUrl NOTIFY settingsChanged)
    Q_PROPERTY(bool relayKeySet READ relayKeySet NOTIFY settingsChanged)
    Q_PROPERTY(bool relayConfigured READ relayConfigured NOTIFY settingsChanged)
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
    };

    explicit ShareController(NotchController *notch, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_sessions.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeCount() const;
    QString relayUrl() const { return m_settings.value(QStringLiteral("relayUrl")).toString(); }
    void setRelayUrl(const QString &url);
    bool relayKeySet() const { return !relayKey().isEmpty(); }
    bool relayConfigured() const { return relayAccess().valid(); }
    int defaultTtl() const { return m_settings.value(QStringLiteral("ttl"), 3600).toInt(); }
    void setDefaultTtl(int seconds);
    QString lastMode() const { return m_settings.value(QStringLiteral("mode"), QStringLiteral("local")).toString(); }
    void setLastMode(const QString &mode);

    // Starts a new session and returns its id. mode: local | relay. protection: "" | pin | password with
    // `secret` as the PIN / password (a relay PIN is checked by the server, a relay password locks the key in
    // the link, local ones are checked by Notch). oneDownload applies to a single file only.
    Q_INVOKABLE QString start(const QStringList &ids, const QString &mode, int ttlSeconds, const QString &protection,
                              const QString &secret, bool oneDownload, const QString &address);
    Q_INVOKABLE void stop(const QString &id);         // ends a running session
    Q_INVOKABLE void dismiss(const QString &id);      // removes a finished / failed one from the list
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE QString checkSecret(const QString &protection, const QString &secret) const { return ShareSession::checkSecret(protection, secret); }
    Q_INVOKABLE QStringList lanAddresses() const;
    Q_INVOKABLE void setRelayKey(const QString &key); // kept in relay.key (0600), never in notch.conf
    Q_INVOKABLE void testRelay();                      // answers through relayTested

signals:
    void countChanged();
    void settingsChanged();
    void relayTested(bool ok, const QString &message);

private:
    QString relayKey() const;
    QUrl normalizedRelay() const; // https only (http for localhost, for testing); invalid when unset
    RelayAccess relayAccess() const { return {normalizedRelay(), relayKey().toUtf8()}; }
    void add(ShareSession *s);
    int rowOf(const QObject *session) const;
    void scheduleSave() { m_saveTimer.start(); }
    void saveNow();
    void restore();

    NotchController *m_notch;
    QSettings m_settings;
    QNetworkAccessManager m_nam;
    QList<ShareSession *> m_sessions;
    QTimer m_saveTimer;
};
