#include "AudioPlayback.h"

#include "platform/ChildLife.h"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>

AudioPlayback::AudioPlayback(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &AudioPlayback::tick);
    ChildLife::dieWithParent(m_ffmpeg);
    ChildLife::dieWithParent(m_sink);
    // The sink ends when the sound has all been played; that is the end of the recording (unless we stopped it ourselves).
    connect(&m_sink, &QProcess::finished, this, [this] {
        if (m_restarting || !m_playing)
            return;
        m_playing = false;
        m_timer.stop();
        m_from = m_duration > 0 && position() >= m_duration - 1.0 ? 0 : position();
        m_clock.invalidate();
        emit playingChanged();
        emit positionChanged();
    });
    connect(&m_sink, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            setError(tr("paplay is not installed (the pulseaudio-utils package), so the recording cannot be played."));
    });
    connect(&m_ffmpeg, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            setError(tr("ffmpeg is not installed, so the recording cannot be played."));
    });
}

AudioPlayback::~AudioPlayback()
{
    kill();
}

double AudioPlayback::position() const
{
    if (!m_playing || !m_clock.isValid())
        return m_from;
    const double p = m_from + double(m_clock.elapsed()) / 1000.0 * m_rate;
    return m_duration > 0 ? std::min(p, m_duration) : p;
}

void AudioPlayback::setSource(const QString &pathOrUrl, double duration, bool mixdown)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (path == m_path && qFuzzyCompare(duration + 1, m_duration + 1) && mixdown == m_mixdown)
        return;
    stop();
    m_path = path;
    m_duration = duration;
    m_mixdown = mixdown;
    setError({});
    emit durationChanged();
}

void AudioPlayback::setRate(double rate)
{
    rate = std::clamp(rate, 0.5, 2.0);
    if (qFuzzyCompare(rate, m_rate))
        return;
    if (m_playing)
        m_from = position();
    m_rate = rate;
    emit rateChanged();
    if (m_playing)
        start(); // the speed is part of what ffmpeg is asked to make
}

void AudioPlayback::play()
{
    if (m_path.isEmpty() || m_playing)
        return;
    if (m_duration > 0 && m_from >= m_duration - 0.2)
        m_from = 0;
    setError({});
    start();
}

void AudioPlayback::pause()
{
    if (!m_playing)
        return;
    m_from = position();
    m_playing = false;
    kill();
    emit playingChanged();
    emit positionChanged();
}

void AudioPlayback::seek(double seconds)
{
    m_from = std::clamp(seconds, 0.0, m_duration > 0 ? m_duration : seconds);
    if (m_playing)
        start();
    emit positionChanged();
}

void AudioPlayback::stop()
{
    const bool was = m_playing;
    m_playing = false;
    kill();
    m_from = 0;
    if (was)
        emit playingChanged();
    emit positionChanged();
}

void AudioPlayback::start()
{
    kill();
    if (!QFileInfo::exists(m_path)) {
        setError(tr("The audio file is not there any more."));
        return;
    }
    QStringList filters;
    if (m_mixdown) // microphone left, the computer's sound right: both to both ears
        filters << QStringLiteral("pan=stereo|c0=0.5*c0+0.5*c1|c1=0.5*c0+0.5*c1");
    if (!qFuzzyCompare(m_rate, 1.0))
        filters << QStringLiteral("atempo=%1").arg(m_rate, 0, 'f', 2);
    QStringList args = {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-nostdin"), QStringLiteral("-ss"), QString::number(m_from, 'f', 3),
                        QStringLiteral("-i"), m_path, QStringLiteral("-vn")};
    if (!filters.isEmpty())
        args << QStringLiteral("-af") << filters.join(QLatin1Char(','));
    args << QStringLiteral("-f") << QStringLiteral("s16le") << QStringLiteral("-ar") << QStringLiteral("44100") << QStringLiteral("-ac") << QStringLiteral("2") << QStringLiteral("-");
    m_ffmpeg.setProgram(QStringLiteral("ffmpeg"));
    m_ffmpeg.setArguments(args);
    m_sink.setProgram(QStringLiteral("paplay"));
    m_sink.setArguments({QStringLiteral("--raw"), QStringLiteral("--rate=44100"), QStringLiteral("--channels=2"), QStringLiteral("--format=s16le"),
                         QStringLiteral("--latency-msec=60"), QStringLiteral("--client-name=Olyzano Notch"), QStringLiteral("--stream-name=Audio")});
    m_ffmpeg.setStandardOutputProcess(&m_sink);
    m_restarting = true;
    m_sink.start();
    m_ffmpeg.start();
    m_restarting = false;
    m_playing = true;
    m_clock.start();
    m_timer.start();
    emit playingChanged();
}

void AudioPlayback::kill()
{
    m_restarting = true; // our own stop is not "the recording ended"
    m_timer.stop();
    for (QProcess *p : {&m_ffmpeg, &m_sink}) {
        if (p->state() != QProcess::NotRunning) {
            p->terminate();
            if (!p->waitForFinished(300)) {
                p->kill();
                p->waitForFinished(300);
            }
        }
    }
    m_restarting = false;
    m_clock.invalidate();
}

void AudioPlayback::tick()
{
    emit positionChanged();
    if (m_duration > 0 && position() >= m_duration) // ffmpeg may be done a little before paplay has played the last of it
        m_timer.stop();
}

void AudioPlayback::setError(const QString &e)
{
    if (m_error == e)
        return;
    m_error = e;
    emit errorChanged();
}
