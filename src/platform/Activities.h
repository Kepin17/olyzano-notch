#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

// What the notch shows while something is going on, as `activities` in QML:
// - a timer, stopwatch or Pomodoro (shared by every monitor's notch),
// - downloads still being written to the Downloads folder (browsers' .part / .crdownload files),
// - which apps are using the microphone or a camera right now (Linux: PulseAudio/PipeWire and /dev/video*;
//   Windows: the registry list behind its taskbar mic icon).
// Switches live in notch/activities.conf.
class Activities : public QObject
{
    Q_OBJECT
    // Timer: kind "" (none) | "timer" | "stopwatch" | "pomodoro"; QML reads timerMs() to draw the time.
    Q_PROPERTY(QString timerKind READ timerKind NOTIFY timerChanged)
    Q_PROPERTY(bool timerRunning READ timerRunning NOTIFY timerChanged)  // false while paused
    Q_PROPERTY(qint64 timerTotalMs READ timerTotalMs NOTIFY timerChanged) // countdown length, 0 for a stopwatch
    Q_PROPERTY(QString timerLabel READ timerLabel NOTIFY timerChanged)    // "Focus", "Break", or the timer's name
    Q_PROPERTY(int pomodoroRound READ pomodoroRound NOTIFY timerChanged)
    // [{name, bytes, speed}] — bytes written so far and bytes per second.
    Q_PROPERTY(QVariantList downloads READ downloads NOTIFY downloadsChanged)
    Q_PROPERTY(QStringList micApps READ micApps NOTIFY privacyChanged)
    Q_PROPERTY(QStringList cameraApps READ cameraApps NOTIFY privacyChanged)
    Q_PROPERTY(int micMuted READ micMuted NOTIFY privacyChanged) // default microphone: 1 muted, 0 live, -1 unknown
    // The small clock pill shown while nothing else is (Settings).
    Q_PROPERTY(bool idleClock READ idleClock WRITE setIdleClock NOTIFY settingsChanged)
    // Settings
    Q_PROPERTY(bool showDownloads READ showDownloads WRITE setShowDownloads NOTIFY settingsChanged)
    Q_PROPERTY(bool privacyIndicators READ privacyIndicators WRITE setPrivacyIndicators NOTIFY settingsChanged)
    Q_PROPERTY(bool phoneAlerts READ phoneAlerts WRITE setPhoneAlerts NOTIFY settingsChanged)
    Q_PROPERTY(int focusMinutes READ focusMinutes WRITE setFocusMinutes NOTIFY settingsChanged)
    Q_PROPERTY(int breakMinutes READ breakMinutes WRITE setBreakMinutes NOTIFY settingsChanged)

public:
    explicit Activities(QObject *parent = nullptr);

    QString timerKind() const { return m_kind; }
    bool timerRunning() const { return m_running; }
    qint64 timerTotalMs() const { return m_totalMs; }
    QString timerLabel() const { return m_label; }
    int pomodoroRound() const { return m_round; }
    QVariantList downloads() const { return m_downloadList; }
    QStringList micApps() const { return m_mic; }
    QStringList cameraApps() const { return m_camera; }
    int micMuted() const { return m_micMuted; }
    bool idleClock() const { return m_settings.value(QStringLiteral("idleClock"), true).toBool(); }
    void setIdleClock(bool on);

    bool showDownloads() const { return m_settings.value(QStringLiteral("downloads"), true).toBool(); }
    void setShowDownloads(bool on);
    bool privacyIndicators() const { return m_settings.value(QStringLiteral("privacy"), true).toBool(); }
    void setPrivacyIndicators(bool on);
    bool phoneAlerts() const { return m_settings.value(QStringLiteral("phone"), true).toBool(); }
    void setPhoneAlerts(bool on);
    int focusMinutes() const { return m_settings.value(QStringLiteral("focus"), 25).toInt(); }
    void setFocusMinutes(int minutes);
    int breakMinutes() const { return m_settings.value(QStringLiteral("break"), 5).toInt(); }
    void setBreakMinutes(int minutes);

    // Remaining time of a countdown, or elapsed time of a stopwatch, in ms.
    Q_INVOKABLE qint64 timerMs() const;
    Q_INVOKABLE void startTimer(int seconds, const QString &label = QString());
    Q_INVOKABLE void startStopwatch();
    Q_INVOKABLE void startPomodoro();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void addMinute();
    Q_INVOKABLE void toggleMicMute();

signals:
    void timerChanged();
    void timerFinished(const QString &title, const QString &detail);
    void downloadsChanged();
    void privacyChanged();
    void settingsChanged();

private:
    void store(const QString &key, const QVariant &value);
    void startCountdown(qint64 ms, const QString &label);
    void onCountdownEnd();
    void scanDownloads();
    void pollPrivacy();
    void readMicOutputs();

    QSettings m_settings;

    QString m_kind;
    QString m_label;
    bool m_running = false;
    qint64 m_totalMs = 0;
    qint64 m_baseMs = 0;     // countdown: remaining when last resumed; stopwatch: elapsed before last resume
    QElapsedTimer m_since;   // since the last resume
    QTimer m_end;            // fires when a countdown reaches zero
    bool m_breakPhase = false;
    int m_round = 0;

    QString m_downloadDir;
    QFileSystemWatcher m_watcher;
    QTimer m_downloadPoll;
    struct Download { qint64 bytes = 0; double speed = 0; qint64 at = 0; };
    QHash<QString, Download> m_downloads; // partial file path -> last reading
    QVariantList m_downloadList;

    QTimer m_privacyPoll;
    QProcess m_pactl;
    QByteArray m_pactlOut;
    QStringList m_mic, m_camera;
    int m_micMuted = -1;
};
