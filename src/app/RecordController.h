#pragma once

#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>

class NotchController;
class QQmlEngine;

// The screen recorder's part that is not Qt Multimedia (that lives in RecordView.qml): a folder for each recording, the sound of the computer
// (`parec` on a monitor of an output, into a WAV), and putting the recordings together with ffmpeg (RecordMix.h) and onto the shelf.
class RecordController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool mixing READ mixing NOTIFY changed)
    Q_PROPERTY(bool canRecordSystem READ canRecordSystem CONSTANT) // parec is there
    Q_PROPERTY(bool canMix READ canMix CONSTANT)                   // ffmpeg is there
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    // What the recorder page (RecordView.qml) says about itself, so the closed notch can show it and the page is kept while it matters.
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)   // idle | picking | countdown | recording | finishing | mixing
    Q_PROPERTY(int elapsed READ elapsed NOTIFY stateChanged)   // seconds of the recording
    Q_PROPERTY(QString target READ target NOTIFY stateChanged)  // the application being recorded ("" = the screen)
    Q_PROPERTY(bool picking READ picking NOTIFY stateChanged)   // the system is asking what to share
    Q_PROPERTY(int countdown READ countdown NOTIFY stateChanged) // seconds until the recording starts, while phase is "countdown"
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged) // a recording is running (the red mark in the notch)
    Q_PROPERTY(bool engaged READ engaged NOTIFY stateChanged)  // something is running or being finished: the page must not be let go of

public:
    explicit RecordController(NotchController *notch, QObject *parent = nullptr);
    ~RecordController() override;

    bool mixing() const { return m_mixing; }
    bool canRecordSystem() const;
    bool canMix() const;
    QString status() const { return m_status; }
    QString lastError() const { return m_error; }
    QString phase() const { return m_phase; }
    int elapsed() const { return m_elapsed; }
    QString target() const { return m_target; }
    bool picking() const { return m_picking; }
    int countdown() const { return m_countdown; }
    bool recording() const { return m_phase == QLatin1String("recording"); }
    bool engaged() const { return m_phase != QLatin1String("idle") || m_previewing || m_held; }
    void setEngine(QQmlEngine *engine) { m_engine = engine; }

    Q_INVOKABLE void publishState(const QString &phase, int elapsed, bool previewing, const QString &target = QString(), bool held = false, bool picking = false,
                                  int countdown = 0);
    Q_INVOKABLE void note(const QString &what) const; // a line in the log (the journal), to follow a recording that went wrong
    // A screen capture (Qt Multimedia's ScreenCapture) that belongs to no page and is never deleted: Qt's PipeWire capture crashes when it
    // is stopped or deleted before the system has answered. null when Qt Multimedia for QML is not there.
    Q_INVOKABLE QObject *newScreenCapture();
    // What the camera panel remembers (quality, brightness, contrast), in notch/camera.conf.
    Q_INVOKABLE QVariant pref(const QString &key, const QVariant &fallback) const { return m_prefs.value(key, fallback); }
    Q_INVOKABLE void setPref(const QString &key, const QVariant &value);

    Q_INVOKABLE QString newSession();                                  // a fresh folder (scratch) for the files of one recording
    Q_INVOKABLE QVariantList soundSources() const;                     // [{id, name}]: what the computer plays, one per output
    Q_INVOKABLE bool startSystemSound(const QString &session, const QString &sourceId);
    Q_INVOKABLE void stopSystemSound();
    // options: session, screen, camera, system (file names in the session folder, "" for none), screenHasSound, cameraOffset, systemOffset,
    // x, y, size, shape ("square"|"rounded"|"circle"), mirror, micVolume, systemVolume. `finished(error)` says how it went ("" = on the shelf).
    Q_INVOKABLE void mix(const QVariantMap &options);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void discard(const QString &session);
    Q_INVOKABLE void clearError();

signals:
    void changed();
    void stateChanged();
    void prefsChanged();
    void finished(const QString &error);

private:
    NotchController *m_notch;
    QQmlEngine *m_engine = nullptr;
    mutable QSettings m_prefs;
    QString m_phase = QStringLiteral("idle");
    int m_elapsed = 0;
    QString m_target;
    bool m_previewing = false, m_held = false, m_picking = false;
    int m_countdown = 0;
    QProcess m_parec;
    QThreadPool m_pool;
    std::atomic_bool m_cancel{false};
    bool m_mixing = false;
    int m_serial = 0;
    QString m_status, m_error;
};
