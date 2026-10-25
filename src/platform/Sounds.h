#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QTimer>

#include <functional>

// Short sound effects for moments worth hearing (a file dropped, a timer done, a call, …), as `sounds` in QML.
// One object for every screen's notch, so an event plays once. Linux plays through `paplay` (PulseAudio or
// PipeWire), Windows through PlaySound; the sounds themselves are made by tools/make_sounds.py. Settings live in
// notch/sounds.conf: a master switch, volume, one switch per group (SoundsModel.h) and quiet-when-busy.
class Sounds : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY settingsChanged) // 0-100
    Q_PROPERTY(bool shelf READ shelf WRITE setShelf NOTIFY settingsChanged)
    Q_PROPERTY(bool timer READ timer WRITE setTimer NOTIFY settingsChanged)
    Q_PROPERTY(bool phone READ phone WRITE setPhone NOTIFY settingsChanged)
    Q_PROPERTY(bool system READ system WRITE setSystem NOTIFY settingsChanged)
    Q_PROPERTY(bool quietWhenBusy READ quietWhenBusy WRITE setQuietWhenBusy NOTIFY settingsChanged)
    Q_PROPERTY(bool quietOnMic READ quietOnMic WRITE setQuietOnMic NOTIFY settingsChanged)

public:
    explicit Sounds(QObject *parent = nullptr);
    ~Sounds() override;

    bool enabled() const { return value("enabled", true); }
    void setEnabled(bool on) { store("enabled", on); }
    int volume() const { return m_settings.value(QStringLiteral("volume"), 50).toInt(); }
    void setVolume(int v) { store("volume", qBound(0, v, 100)); }
    bool shelf() const { return value("shelf", true); }
    void setShelf(bool on) { store("shelf", on); }
    bool timer() const { return value("timer", true); }
    void setTimer(bool on) { store("timer", on); }
    bool phone() const { return value("phone", true); }
    void setPhone(bool on) { store("phone", on); }
    bool system() const { return value("system", true); }
    void setSystem(bool on) { store("system", on); }
    bool quietWhenBusy() const { return value("quietWhenBusy", true); }
    void setQuietWhenBusy(bool on) { store("quietWhenBusy", on); }
    // Off by default: Discord, a browser tab or a voice assistant keep a recording stream open all day, which
    // is not a call, and it used to silence every sound.
    bool quietOnMic() const { return value("quietOnMic", false); }
    void setQuietOnMic(bool on) { store("quietOnMic", on); }

    // What "busy" means is learnt from elsewhere: a microphone in use, a full-screen window.
    void setBusyChecks(std::function<bool()> micInUse, std::function<bool()> fullScreen);

    Q_INVOKABLE void play(const QString &name);    // obeys the settings
    Q_INVOKABLE void preview(const QString &name); // for Settings: plays even when switched off
    void playRing();                               // the call sound, again every 2 s, three times at most
    void stopRing();

signals:
    void settingsChanged();

private:
    bool value(const char *key, bool fallback) const { return m_settings.value(QLatin1String(key), fallback).toBool(); }
    void store(const char *key, const QVariant &v);
    void output(const QString &name);
    bool fullScreenNow() const;

    QSettings m_settings;
    std::function<bool()> m_micInUse, m_fullScreen;
    QHash<QString, QElapsedTimer> m_last; // name -> since it last played
    QTimer m_ring;
    int m_rings = 0;
#ifdef Q_OS_WIN
    QByteArray m_playing; // PlaySound reads the sound from here while it plays
#else
    QString m_dir;        // the sounds, copied out of the resources for paplay
    bool m_warned = false;
#endif
};
