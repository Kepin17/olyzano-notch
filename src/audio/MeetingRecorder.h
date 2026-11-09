#pragma once

#include "platform/MicCapture.h"

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QTimer>

// A meeting recorded from Notch, only when the person presses Record: the microphone on the left and the sound of the computer (the other
// side of a call) on the right, 16 kHz, into one stereo WAV (the sides stay apart, which tells "You" from the others without guessing).
// Linux: parec for both; Windows: WASAPI (the microphone, and the loopback of the speakers). Nothing is kept outside the meeting's folder.
class MeetingRecorder : public QObject
{
    Q_OBJECT
public:
    explicit MeetingRecorder(QObject *parent = nullptr);
    ~MeetingRecorder() override;

    static constexpr int Rate = 16000;
    static constexpr int MaxSeconds = 3600;

    // Starts into `dir` (made if needed). "" or why not (not enough disk, no microphone program).
    QString start(const QString &dir);
    // Stops and writes `wavPath` (stereo). "" on success; the raw files are deleted.
    QString stop(QString *wavPath);
    void discard(); // stop and delete everything
    bool recording() const { return m_recording; }
    double seconds() const { return m_recording ? m_clock.elapsed() / 1000.0 : 0; }
    double level() const { return m_level; }

signals:
    void levelChanged();
    void failed(const QString &why);
    void limitReached();
    void micAudio(const QByteArray &s16le, qint64 endMs);  // the same chunks, for a live transcript (endMs: the recording clock)
    void sysAudio(const QByteArray &s16le, qint64 endMs);

private:
    void write(QFile &file, qint64 &samples, const QByteArray &chunk);

    MicCapture m_mic, m_sys;
    QFile m_micFile, m_sysFile;
    qint64 m_micSamples = 0, m_sysSamples = 0;
    QElapsedTimer m_clock;
    QTimer m_limit;
    QString m_dir;
    double m_level = 0;
    bool m_recording = false;
};
