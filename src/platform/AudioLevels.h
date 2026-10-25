#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QTimer>

#ifdef Q_OS_WIN
#include <atomic>
#include <thread>
#endif

// Live equalizer levels of what's playing: reads the default output's monitor through `parec`
// (PulseAudio/PipeWire; on Windows a WASAPI loopback capture of the default output, on a thread of its
// own) and turns it into BandCount levels. Audio stays in memory; only levels
// leave this class. Capture runs only while someone holds it (acquire/release), so it is off
// whenever no equalizer is on screen.
class AudioLevels : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<qreal> levels READ levels NOTIFY levelsChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged) // false: use a canned animation

public:
    explicit AudioLevels(QObject *parent = nullptr);
    ~AudioLevels() override;

    QList<qreal> levels() const { return m_levels; }
    bool available() const { return m_available; }

    Q_INVOKABLE void acquire();
    Q_INVOKABLE void release();

signals:
    void levelsChanged();
    void availableChanged();

private:
    void start();
    void stop();
    void onData();
    void apply(const QList<double> &fresh);
    void setAvailable(bool available);
#ifdef Q_OS_WIN
    void runLoopback(); // AudioLevels_win.cpp, on m_loopback

    std::thread m_loopback;
    std::atomic<bool> m_capturing{false};
#endif

    QProcess m_process;
    QByteArray m_buffer;
    QList<qreal> m_levels;
    QTimer m_stopTimer;
    int m_users = 0;
    bool m_available = true;
};
