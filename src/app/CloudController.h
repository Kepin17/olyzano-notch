#pragma once

#include "app/NotchController.h"
#include "app/ShareController.h"
#include "models/CloudModel.h"

#include <QHash>
#include <QNetworkRequest>
#include <QObject>
#include <QSettings>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

// The cloud shelf: a shelf shared by the members of a Share server, kept (encrypted) on the S3 store behind that server's relay.
// The relay is the gate: the host decides who may use it and how much space each has, and can pause or remove a member with a reason
// (which this class shows). Names and contents are encrypted here with the shelf key (ShareController::shelfKey), which the host makes and
// members get inside the invitation link: neither the relay nor the store can read them. Settings → Files switches it on.
class CloudController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString serverId READ serverId WRITE setServerId NOTIFY changed)
    // off | needsServer | needsKey | connecting | ready | syncing | offline | disabled | removed | unsupported
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString stateMessage READ stateMessage NOTIFY changed) // what to tell the person (with the host's reason)
    Q_PROPERTY(qlonglong used READ used NOTIFY changed)               // bytes this member has on the shelf
    Q_PROPERTY(qlonglong quota READ quota NOTIFY changed)             // how many the host allows
    Q_PROPERTY(bool isHost READ isHost NOTIFY changed)
    Q_PROPERTY(bool usable READ usable NOTIFY changed)                // can send and fetch right now
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *entries READ entries CONSTANT)

public:
    CloudController(NotchController *notch, ShareController *share, QObject *parent = nullptr);

    bool enabled() const { return m_settings.value(QStringLiteral("enabled"), false).toBool(); }
    void setEnabled(bool on);
    QString serverId() const;
    void setServerId(const QString &id);
    QString state() const { return m_state; }
    QString stateMessage() const { return m_message; }
    qlonglong used() const { return m_used; }
    qlonglong quota() const { return m_quota; }
    bool isHost() const;
    bool usable() const;
    int count() const { return int(m_model.rowCount()); }
    QAbstractItemModel *entries() { return &m_model; }

    Q_INVOKABLE void syncNow();
    // Shelf files (by id) or files from anywhere (paths) go to the cloud shelf; one at a time to two at a time, each with its progress in the pill.
    Q_INVOKABLE void uploadItems(const QStringList &itemIds);
    Q_INVOKABLE void uploadUrls(const QList<QUrl> &urls);
    // thenShare: fetch into the local shelf, then readyToShare with the new item's id (for Share via QR).
    Q_INVOKABLE void download(const QString &cloudId, bool thenShare = false);
    Q_INVOKABLE void remove(const QString &cloudId);
    Q_INVOKABLE void rename(const QString &cloudId, const QString &name);

signals:
    void changed();
    void notice(const QString &title, const QString &detail);
    void failed(const QString &message);
    void readyToShare(const QString &itemId); // a cloud file finished downloading for Share via QR

private:
    struct Raw {
        qint64 owner = 0, size = 0, updated = 0;
        QByteArray meta; // ciphertext
    };
    struct Job {
        QString path, name, mime, category;
    };
    QByteArray key() const;
    QNetworkRequest request(const QString &path) const;
    QString failure(QNetworkReply *reply); // sets the state when the host says so; the words for the person
    void setState(const QString &state, const QString &message = {});
    void evaluate(); // the state from the settings (off, needsServer, needsKey...) when nothing has gone wrong
    void rebuild();
    void loadCache();
    void saveCache() const;
    QString cachePath() const;
    void pump();
    void startUpload(const Job &job);
    QByteArray metaFor(const Job &job, qint64 size, qint64 added = 0) const;

    NotchController *m_notch;
    ShareController *m_share;
    QSettings m_settings;
    CloudModel m_model;
    QThreadPool m_pool;
    QTimer m_poll;
    QHash<QString, Raw> m_raw;
    QList<Job> m_queue;
    int m_active = 0;
    qint64 m_seq = 0, m_me = -1;
    qlonglong m_used = 0, m_quota = 0;
    QString m_state = QStringLiteral("off"), m_message, m_loadedFor;
    QString m_net; // "" | offline | disabled | removed | unsupported
    bool m_syncing = false, m_again = false;
};
