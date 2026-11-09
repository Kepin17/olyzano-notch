#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>

#ifdef Q_OS_WIN
#include <atomic>
#include <thread>
#endif

// The default microphone as 16 kHz mono 16-bit chunks of about 60 ms, for the voice assistant. Open only between
// start() and stop(); nothing is stored here and nothing leaves the computer (Linux: `parec`, Windows: WASAPI).
// With SystemSound it is what the computer itself plays instead (the other side of a call), for recording a meeting.
class MicCapture : public QObject
{
    Q_OBJECT
public:
    enum Source { Microphone, SystemSound };
    explicit MicCapture(QObject *parent = nullptr, Source source = Microphone);
    ~MicCapture() override;
    bool start();
    void stop();
    bool running() const;

signals:
    void chunk(const QByteArray &s16le);
    void failed(const QString &why);

private:
    Source m_source = Microphone;
#ifdef Q_OS_WIN
    void run();
    std::atomic<bool> m_run{false};
    std::thread m_thread;
#else
    QProcess m_proc;
    QByteArray m_rest;
    bool m_got = false;
#endif
};
