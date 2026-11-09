#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Plays a recording through the computer's own default sound output (Linux): ffmpeg decodes it from where it should start and paplay
// plays it on PulseAudio / PipeWire's default sink. QtMultimedia would be simpler but it does not list every output (a Bluetooth
// headset was missing) and picked another one, so the recording played into nothing. Seeking and speed restart the pair.
class AudioPlayback : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged) // seconds
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY rateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit AudioPlayback(QObject *parent = nullptr);
    ~AudioPlayback() override;

    bool playing() const { return m_playing; }
    double position() const;
    double duration() const { return m_duration; }
    double rate() const { return m_rate; }
    QString error() const { return m_error; }
    void setRate(double rate);

    // The file (a path or file:// URL), its length in seconds, and whether its left and right are two people (a recording made by Notch:
    // microphone and computer sound), which are then played to both ears.
    Q_INVOKABLE void setSource(const QString &pathOrUrl, double duration, bool mixdown);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void toggle() { m_playing ? pause() : play(); }
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void playFrom(double seconds) { seek(seconds); if (!m_playing) play(); }
    Q_INVOKABLE void stop();

signals:
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void rateChanged();
    void errorChanged();

private:
    void start();
    void kill();
    void tick();
    void setError(const QString &e);

    QProcess m_ffmpeg, m_sink;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QString m_path, m_error;
    double m_duration = 0, m_from = 0, m_rate = 1;
    bool m_playing = false, m_mixdown = false, m_restarting = false;
};
