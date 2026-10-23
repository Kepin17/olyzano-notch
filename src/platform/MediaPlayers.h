#pragma once

#include <QDBusMessage>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QVariantMap>

// Now-playing over MPRIS (session bus): every org.mpris.MediaPlayer2.* service, e.g. Chrome/
// Firefox tabs (YouTube), Spotify, Elisa. Exposes the player chosen by pickPlayer() and its
// controls. Position isn't signalled by MPRIS, so QML reads positionMs() and interpolates.
class MediaPlayers : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY changed)
    Q_PROPERTY(qint64 lengthMs READ lengthMs NOTIFY changed)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    Q_PROPERTY(bool canNext READ canNext NOTIFY changed)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY changed)
    Q_PROPERTY(QString identity READ identity NOTIFY changed)
    Q_PROPERTY(int playerCount READ playerCount NOTIFY changed)

public:
    explicit MediaPlayers(QObject *parent = nullptr);

    bool active() const { return current() != nullptr; }
    bool playing() const;
    QString title() const;
    QString artist() const;
    QString artUrl() const;
    qint64 lengthMs() const;
    bool canSeek() const;
    bool canNext() const;
    bool canPrevious() const;
    QString identity() const;
    int playerCount() const;

    Q_INVOKABLE qint64 positionMs() const;
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void raise();       // bring the player (e.g. the browser tab) to front
    Q_INVOKABLE void cyclePlayer(); // switch between players when several are active

signals:
    void changed();
    // The shown track changed while playing (not on startup): for the "now playing" toast.
    void trackChanged(const QString &title, const QString &artist, const QString &artUrl);

private slots:
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void onPropertiesChanged(const QDBusMessage &message);

private:
    struct Player {
        QString service;
        QString owner; // unique bus name; signals arrive from it
        QString identity;
        QString status;
        QVariantMap metadata;
        bool canNext = false, canPrevious = false, canSeek = false;
        qint64 lastActive = 0;
    };

    void addPlayer(const QString &service, const QString &owner);
    void refresh(Player &p);
    void select();
    const Player *current() const;
    void call(const QString &method, const QList<QVariant> &args = {}) const;

    QHash<QString, Player> m_players; // by service name
    QString m_current;
    QString m_manual;
    QString m_lastTrack;
    bool m_ready = false;
    QElapsedTimer m_clock;
};
