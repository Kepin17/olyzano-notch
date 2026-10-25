#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QVariantMap>
#ifdef NOTCH_HAVE_DBUS
#include <QDBusMessage>
#endif

#if !defined(NOTCH_HAVE_DBUS) && defined(Q_OS_WIN)
#include <memory>
// Windows: the media sessions apps register with the system (what the volume flyout shows: browser
// tabs, Spotify, Media Player), read on a worker thread of their own and mirrored here. Same
// QML-facing API and the same player choice (PlayerSelection.h) as the MPRIS class below.
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
    ~MediaPlayers() override;

    // One session as the worker last read it.
    struct Session {
        QString key;      // AUMID#n: an app can have several (one per browser tab)
        QString aumid;
        QString status;   // "Playing" | "Paused" | "Stopped"
        QString title, artist, artUrl;
        qint64 lengthMs = 0;
        qint64 positionMs = 0; // at positionAt (ms since the epoch)
        qint64 positionAt = 0;
        bool canNext = false, canPrevious = false, canSeek = false;
    };

    bool active() const { return current() != nullptr; }
    bool playing() const;
    QString title() const { return current() ? current()->title : QString(); }
    QString artist() const { return current() ? current()->artist : QString(); }
    QString artUrl() const { return current() ? current()->artUrl : QString(); }
    qint64 lengthMs() const { return current() ? current()->lengthMs : 0; }
    bool canSeek() const { return current() && current()->canSeek; }
    bool canNext() const { return current() && current()->canNext; }
    bool canPrevious() const { return current() && current()->canPrevious; }
    QString identity() const;
    int playerCount() const;

    Q_INVOKABLE qint64 positionMs() const;
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void raise();
    Q_INVOKABLE void cyclePlayer();

    void sessionsRead(const QList<Session> &sessions); // from the worker, on this thread

signals:
    void changed();
    void trackChanged(const QString &title, const QString &artist, const QString &artUrl);

private:
    struct Player {
        Session s;
        qint64 lastActive = 0;
    };
    const Session *current() const;
    void select();
    void command(int which, qint64 arg = 0) const;

    struct Worker;
    std::unique_ptr<Worker> m_worker;
    QHash<QString, Player> m_players; // by Session::key
    QString m_current;
    QString m_manual;
    QString m_lastTrack;
    bool m_ready = false;
    QElapsedTimer m_clock;
};
#elif !defined(NOTCH_HAVE_DBUS)
// No MPRIS here: nothing is ever "playing", so the music card never shows. Same QML-facing API.
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
    explicit MediaPlayers(QObject *parent = nullptr) : QObject(parent) {}
    bool active() const { return false; }
    bool playing() const { return false; }
    QString title() const { return {}; }
    QString artist() const { return {}; }
    QString artUrl() const { return {}; }
    qint64 lengthMs() const { return 0; }
    bool canSeek() const { return false; }
    bool canNext() const { return false; }
    bool canPrevious() const { return false; }
    QString identity() const { return {}; }
    int playerCount() const { return 0; }
    Q_INVOKABLE qint64 positionMs() const { return 0; }
    Q_INVOKABLE void playPause() {}
    Q_INVOKABLE void next() {}
    Q_INVOKABLE void previous() {}
    Q_INVOKABLE void seek(qint64) {}
    Q_INVOKABLE void raise() {}
    Q_INVOKABLE void cyclePlayer() {}

signals:
    void changed();
    void trackChanged(const QString &title, const QString &artist, const QString &artUrl);
};
#else
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
#endif
