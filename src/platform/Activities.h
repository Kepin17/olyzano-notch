#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include "platform/DiscordCall.h"
#include "platform/VoiceModel.h"
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
#define NOTCH_DISCORD_RPC // mute / deafen from Discord's own RPC pipe
#include "platform/DiscordRpc.h"
#endif
#ifdef Q_OS_WIN
#include "platform/WinAudioSessions.h"
#endif

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
    Q_PROPERTY(int micSensitivity READ micSensitivity WRITE setMicSensitivity NOTIFY settingsChanged) // how clearly a voice must stand out: 0 low, 1 normal, 2 high
    Q_PROPERTY(bool micSpeaking READ micSpeaking NOTIFY micSpeakingChanged) // Linux: an app has the microphone, it is live, and there is a voice
    Q_PROPERTY(int micMuted READ micMuted NOTIFY privacyChanged) // default microphone: 1 muted, 0 live, -1 unknown
    // Discord on Windows: "off" | "call" (in a voice call) | "talking" (the microphone is picking up a voice)
    // | "muted" | "deafened" (the last two need discordRpc: Discord says so itself).
    Q_PROPERTY(QString discordState READ discordState NOTIFY discordChanged)
    // Ask the running Discord whether you are muted or deafened (Settings; off until the person turns it on).
    Q_PROPERTY(bool discordRpc READ discordRpc WRITE setDiscordRpc NOTIFY settingsChanged)
    Q_PROPERTY(bool discordAvailable READ discordAvailable CONSTANT) // this build carries a Discord app
    Q_PROPERTY(QString discordStatus READ discordStatus NOTIFY discordChanged)
    // The small clock pill shown while nothing else is (Settings).
    Q_PROPERTY(bool idleClock READ idleClock WRITE setIdleClock NOTIFY settingsChanged)
    // How the notch sits on the screen: "hidden" (a line until the pointer rests there), "capsule" (a small
    // capsule always shown, like a Dynamic Island), "widget" (a floating card you drag anywhere) or "open" (the panel stays open).
    Q_PROPERTY(QString capsuleMode READ capsuleMode WRITE setCapsuleMode NOTIFY settingsChanged)
    // The spectrum beside the song: "bars" | "thin" | "dots" | "wave" | "pulse" | "off"; the capsule's size, 0.8 to 1.3.
    Q_PROPERTY(QString eqStyle READ eqStyle WRITE setEqStyle NOTIFY settingsChanged)
    Q_PROPERTY(qreal capsuleScale READ capsuleScale WRITE setCapsuleScale NOTIFY settingsChanged)
    // Appearance: the open panel's size (0.9 to 1.25, text and spacing together) and how round the corners are ("sharp" | "normal" | "round").
    Q_PROPERTY(qreal uiScale READ uiScale WRITE setUiScale NOTIFY settingsChanged)
    Q_PROPERTY(QString cornerStyle READ cornerStyle WRITE setCornerStyle NOTIFY settingsChanged)
    Q_PROPERTY(bool glass READ glass WRITE setGlass NOTIFY settingsChanged)           // see-through island and surfaces (Theme.h)
    Q_PROPERTY(int glassStrength READ glassStrength WRITE setGlassStrength NOTIFY settingsChanged) // 0-100
    Q_PROPERTY(QString capsuleLook READ capsuleLook WRITE setCapsuleLook NOTIFY settingsChanged) // "edge" (fused to the top edge) or "pill" (a floating capsule)
    Q_PROPERTY(bool reserveSpace READ reserveSpace WRITE setReserveSpace NOTIFY settingsChanged) // keep other windows clear of the capsule
    Q_PROPERTY(bool borderMatchesSystem READ borderMatchesSystem WRITE setBorderMatchesSystem NOTIFY settingsChanged) // capsule / card edge in the system accent
    Q_PROPERTY(bool animations READ animations WRITE setAnimations NOTIFY settingsChanged) // transitions and movement on or off (Settings → Appearance)
    Q_PROPERTY(bool taskInCapsule READ taskInCapsule WRITE setTaskInCapsule NOTIFY settingsChanged) // the next task in the idle capsule
    // Settings
    Q_PROPERTY(bool showDownloads READ showDownloads WRITE setShowDownloads NOTIFY settingsChanged)
    Q_PROPERTY(bool privacyIndicators READ privacyIndicators WRITE setPrivacyIndicators NOTIFY settingsChanged)
    Q_PROPERTY(bool phoneAlerts READ phoneAlerts WRITE setPhoneAlerts NOTIFY settingsChanged)
    Q_PROPERTY(int focusMinutes READ focusMinutes WRITE setFocusMinutes NOTIFY settingsChanged)
    Q_PROPERTY(int breakMinutes READ breakMinutes WRITE setBreakMinutes NOTIFY settingsChanged)

public:
    explicit Activities(QObject *parent = nullptr);
    ~Activities() override;

    QString timerKind() const { return m_kind; }
    bool timerRunning() const { return m_running; }
    qint64 timerTotalMs() const { return m_totalMs; }
    QString timerLabel() const { return m_label; }
    int pomodoroRound() const { return m_round; }
    QVariantList downloads() const { return m_downloadList; }
    QStringList micApps() const { return m_mic; }
    QStringList cameraApps() const { return m_camera; }
    int micMuted() const { return m_micMuted; }
    bool micSpeaking() const { return m_speaking; }
    int micSensitivity() const { return qBound(0, m_settings.value(QStringLiteral("micSensitivity"), 1).toInt(), 2); }
    void setMicSensitivity(int level) { store(QStringLiteral("micSensitivity"), qBound(0, level, 2)); m_voice.level = micSensitivity(); }
    QString discordState() const
    {
        if (m_call.state == CallState::Off)
            return QStringLiteral("off");
#ifdef NOTCH_DISCORD_RPC
        // Only inside a call: Discord can be muted all day, and that is not worth an icon.
        if (m_rpc.mute() == DiscordIpc::Mute::Deafened)
            return QStringLiteral("deafened");
        if (m_rpc.mute() == DiscordIpc::Mute::Muted)
            return QStringLiteral("muted");
#endif
        return m_call.state == CallState::Talking ? QStringLiteral("talking") : QStringLiteral("call");
    }
    bool discordRpc() const { return m_settings.value(QStringLiteral("discordRpc"), false).toBool(); }
    void setDiscordRpc(bool on);
    static bool discordAvailable()
    {
#ifdef NOTCH_DISCORD_RPC
        return DiscordRpc::built();
#else
        return false;
#endif
    }
    QString discordStatus() const
    {
#ifdef NOTCH_DISCORD_RPC
        return m_rpc.status();
#else
        return QString();
#endif
    }
    bool idleClock() const { return m_settings.value(QStringLiteral("idleClock"), true).toBool(); }
    void setIdleClock(bool on);
    // Widget mode: where the floating card is, per screen: its middle (x) and top (y) in pixels of that screen.
    Q_INVOKABLE QPointF widgetPos(const QString &screen, qreal screenW, qreal screenH) const;
    Q_INVOKABLE void setWidgetPos(const QString &screen, qreal x, qreal y);
    Q_INVOKABLE void resetWidgetPos();
    QString eqStyle() const;
    void setEqStyle(const QString &style);
    qreal capsuleScale() const { return qBound(0.8, m_settings.value(QStringLiteral("capsuleScale"), 1.0).toDouble(), 1.3); }
    qreal uiScale() const { return qBound(0.9, m_settings.value(QStringLiteral("uiScale"), 1.0).toDouble(), 1.25); }
    void setUiScale(qreal scale) { store(QStringLiteral("uiScale"), qRound(qBound(0.9, scale, 1.25) * 100) / 100.0); }
    QString cornerStyle() const
    {
        const QString v = m_settings.value(QStringLiteral("cornerStyle")).toString();
        return v == QLatin1String("sharp") || v == QLatin1String("round") ? v : QStringLiteral("normal");
    }
    void setCornerStyle(const QString &style) { store(QStringLiteral("cornerStyle"), style == QLatin1String("sharp") || style == QLatin1String("round") ? style : QStringLiteral("normal")); }
    // What the corner style multiplies every radius by.
    qreal cornerScale() const { const QString v = cornerStyle(); return v == QLatin1String("sharp") ? 0.4 : v == QLatin1String("round") ? 1.6 : 1.0; }
    bool glass() const { return m_settings.value(QStringLiteral("glass"), false).toBool(); }
    void setGlass(bool on) { store(QStringLiteral("glass"), on); }
    int glassStrength() const { return qBound(0, m_settings.value(QStringLiteral("glassStrength"), 60).toInt(), 100); }
    void setGlassStrength(int percent) { store(QStringLiteral("glassStrength"), qBound(0, percent, 100)); }
    QString capsuleLook() const { return m_settings.value(QStringLiteral("capsuleLook")).toString() == QLatin1String("pill") ? QStringLiteral("pill") : QStringLiteral("edge"); }
    void setCapsuleLook(const QString &look) { store(QStringLiteral("capsuleLook"), look == QLatin1String("pill") ? QStringLiteral("pill") : QStringLiteral("edge")); }
    void setCapsuleScale(qreal scale) { store(QStringLiteral("capsuleScale"), qRound(qBound(0.8, scale, 1.3) * 100) / 100.0); }
    bool reserveSpace() const { return m_settings.value(QStringLiteral("reserveSpace"), false).toBool(); }
    void setReserveSpace(bool on) { store(QStringLiteral("reserveSpace"), on); }
    bool borderMatchesSystem() const { return m_settings.value(QStringLiteral("borderMatchesSystem"), false).toBool(); }
    void setBorderMatchesSystem(bool on) { store(QStringLiteral("borderMatchesSystem"), on); }
    bool animations() const { return m_settings.value(QStringLiteral("animations"), true).toBool(); }
    void setAnimations(bool on) { store(QStringLiteral("animations"), on); }
    bool taskInCapsule() const { return m_settings.value(QStringLiteral("taskInCapsule"), true).toBool(); }
    void setTaskInCapsule(bool on) { store(QStringLiteral("taskInCapsule"), on); }
    QString capsuleMode() const;
    void setCapsuleMode(const QString &mode);

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
    void discordChanged();
    void widgetReset(); // the card goes back to its first place
    void micSpeakingChanged();
    void settingsChanged();

private:
    void store(const QString &key, const QVariant &value);
    void startCountdown(qint64 ms, const QString &label);
    void onCountdownEnd();
    void scanDownloads();
    void pollPrivacy();
    void readMicOutputs();
    void pollDiscord();

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
    QProcess m_micTap; // Linux: loudness of the microphone while an app uses it; the sound is never kept, only a peak per chunk
    QElapsedTimer m_voiceClock;
    VoiceModel::Detector m_voice;
    QByteArray m_tapRest; // an odd byte left over from the last read of 16-bit samples
    bool m_speaking = false;
    void syncMicTap();
    void onMicTap();

    QTimer m_discordPoll; // slow while no call, fast enough to follow speech during one
    CallTracker m_call;
#ifdef Q_OS_WIN
    WinAudioSessions m_sessions;
#endif
#ifdef NOTCH_DISCORD_RPC
    DiscordRpc m_rpc;
#endif
};
