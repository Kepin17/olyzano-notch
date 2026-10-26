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
    , m_target(BandCount, 0.0)
{
    m_tick.setInterval(33); // bars move on a steady ~30 Hz beat, however unevenly the audio arrives
    connect(&m_tick, &QTimer::timeout, this, [this] {
        const double dt = m_clock.restart();
        bool moved = false;
        for (int i = 0; i < BandCount; ++i) {
            const double next = smoothLevel(m_levels[i], m_target[i], dt);
            moved = moved || std::abs(next - m_levels[i]) > 0.002;
            m_levels[i] = next;
        }
        if (moved) // idle bars stay still instead of waking QML 30 times a second
            emit levelsChanged();
    });
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
    if (!m_tick.isActive()) {
        m_clock.start();
        m_tick.start();
    }
}

void AudioLevels::release()
{
    m_users = qMax(0, m_users - 1);
    if (m_users == 0)
        m_stopTimer.start();
}

void AudioLevels::start()
{
#ifdef Q_OS_WIN
    if (!m_available || m_loopback.joinable())
        return;
    m_capturing = true;
    m_loopback = std::thread([this] { runLoopback(); });
#else
    if (!m_available || m_process.state() != QProcess::NotRunning)
        return;
    m_buffer.clear();
    m_process.start(QStringLiteral("parec"),
                    {QStringLiteral("-d"), QStringLiteral("@DEFAULT_MONITOR@"), QStringLiteral("--rate=%1").arg(Rate),
                     QStringLiteral("--channels=1"), QStringLiteral("--format=float32le"),
                     QStringLiteral("--latency-msec=25"), QStringLiteral("--raw"),
                     QStringLiteral("--client-name=Olyzano Notch"), QStringLiteral("--stream-name=Equalizer")});
#endif
}

void AudioLevels::stop()
{
#ifdef Q_OS_WIN
    m_capturing = false;
    if (m_loopback.joinable())
        m_loopback.join();
#endif
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(300))
            m_process.kill();
    }
    m_buffer.clear();
    m_tick.stop();
    m_target.fill(0.0);
    m_levels.fill(0.0);
    emit levelsChanged();
}

void AudioLevels::onData()
{
    m_buffer += m_process.readAllStandardOutput();
    if (m_buffer.size() > 8 * WindowBytes) // fell behind: only the newest audio matters
        m_buffer.remove(0, m_buffer.size() - WindowBytes - m_buffer.size() % WindowBytes);
    while (m_buffer.size() >= WindowBytes) {
        apply(bandLevels(reinterpret_cast<const float *>(m_buffer.constData()), Window, Rate));
        m_buffer.remove(0, WindowBytes);
    }
}

void AudioLevels::apply(const QList<double> &fresh)
{
    if (m_users == 0 && !m_stopTimer.isActive())
        return; // a late reading after stop()
    for (int i = 0; i < BandCount && i < fresh.size(); ++i) // the tick moves the bars toward this
        if (std::abs(fresh[i] - m_target[i]) > 0.01 || fresh[i] == 0.0) // a change under 1 % is noise, not music
            m_target[i] = fresh[i];
}

void AudioLevels::setAvailable(bool available)
{
    if (available == m_available)
        return;
    m_available = available;
    emit availableChanged();
}
