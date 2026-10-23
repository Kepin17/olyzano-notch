#include "AudioLevels.h"

#include "models/FileItem.h"
#include "platform/AudioBands.h"

namespace {
constexpr int Rate = 11025;
constexpr int Window = 512; // ~46 ms per update
constexpr qsizetype WindowBytes = Window * qsizetype(sizeof(float));
}

AudioLevels::AudioLevels(QObject *parent)
    : QObject(parent)
    , m_levels(BandCount, 0.0)
{
    m_stopTimer.setSingleShot(true);
    m_stopTimer.setInterval(1000); // brief grace so flicking between views doesn't restart capture
    connect(&m_stopTimer, &QTimer::timeout, this, &AudioLevels::stop);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AudioLevels::onData);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qCWarning(lcNotch) << "parec not available: equalizer uses an animation instead";
            setAvailable(false);
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (m_users > 0 && status == QProcess::NormalExit && code != 0) {
            qCWarning(lcNotch) << "parec exited with" << code << "- equalizer uses an animation instead";
            setAvailable(false);
        }
    });
}

AudioLevels::~AudioLevels()
{
    stop();
}

void AudioLevels::acquire()
{
    ++m_users;
    m_stopTimer.stop();
    start();
}

void AudioLevels::release()
{
    m_users = qMax(0, m_users - 1);
    if (m_users == 0)
        m_stopTimer.start();
}

void AudioLevels::start()
{
    if (!m_available || m_process.state() != QProcess::NotRunning)
        return;
    m_buffer.clear();
    m_process.start(QStringLiteral("parec"),
                    {QStringLiteral("-d"), QStringLiteral("@DEFAULT_MONITOR@"), QStringLiteral("--rate=%1").arg(Rate),
                     QStringLiteral("--channels=1"), QStringLiteral("--format=float32le"),
                     QStringLiteral("--latency-msec=25"), QStringLiteral("--raw"),
                     QStringLiteral("--client-name=Notch"), QStringLiteral("--stream-name=Equalizer")});
}

void AudioLevels::stop()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(300))
            m_process.kill();
    }
    m_buffer.clear();
    m_levels.fill(0.0);
    emit levelsChanged();
}

void AudioLevels::onData()
{
    m_buffer += m_process.readAllStandardOutput();
    if (m_buffer.size() > 8 * WindowBytes) // fell behind: only the newest audio matters
        m_buffer.remove(0, m_buffer.size() - WindowBytes - m_buffer.size() % WindowBytes);
    bool updated = false;
    while (m_buffer.size() >= WindowBytes) {
        const QList<double> fresh = bandLevels(reinterpret_cast<const float *>(m_buffer.constData()), Window, Rate);
        for (int i = 0; i < BandCount; ++i) // jump up, fall back slowly: reads like a VU meter
            m_levels[i] = fresh[i] > m_levels[i] ? fresh[i] : m_levels[i] * 0.8 + fresh[i] * 0.2;
        m_buffer.remove(0, WindowBytes);
        updated = true;
    }
    if (updated)
        emit levelsChanged();
}

void AudioLevels::setAvailable(bool available)
{
    if (available == m_available)
        return;
    m_available = available;
    emit availableChanged();
}
