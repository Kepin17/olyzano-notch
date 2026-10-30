#include "platform/Sounds.h"

#include "models/FileItem.h"
#include "platform/SoundsModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QtEndian>

#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#endif

namespace {
QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/sounds.conf");
}

#ifdef Q_OS_WIN
// The slider reads as loudness; amplitude follows its cube, as PulseAudio's volume (paplay --volume) does.
double gain(int volume)
{
    const double v = volume / 100.0;
    return v * v * v;
}
#endif
}

Sounds::Sounds(QObject *parent)
    : QObject(parent)
    , m_settings(configPath(), QSettings::IniFormat)
{
    m_ring.setInterval(2000);
    connect(&m_ring, &QTimer::timeout, this, [this] {
        if (++m_rings >= 3)
            m_ring.stop();
        play(QStringLiteral("ring"));
    });
#ifndef Q_OS_WIN
    // paplay needs files: the resources are copied once to the cache (again when they change).
    m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/sounds");
    QDir().mkpath(m_dir);
    const QStringList names = QDir(QStringLiteral(":/qt/qml/Notch/src/qml/sounds")).entryList({QStringLiteral("*.wav")});
    for (const QString &file : names) {
        const QString to = m_dir + u'/' + file;
        QFile from(QStringLiteral(":/qt/qml/Notch/src/qml/sounds/") + file);
        if (QFileInfo(to).size() == from.size())
            continue;
        QFile::remove(to);
        from.copy(to);
    }
#endif
}

Sounds::~Sounds()
{
#ifdef Q_OS_WIN
    PlaySoundW(nullptr, nullptr, 0); // stop before m_playing goes
#endif
}

void Sounds::store(const char *key, const QVariant &v)
{
    if (m_settings.value(QLatin1String(key)) == v)
        return;
    m_settings.setValue(QLatin1String(key), v);
    emit settingsChanged();
}

void Sounds::setBusyChecks(std::function<bool()> micInUse, std::function<bool()> fullScreen)
{
    m_micInUse = std::move(micInUse);
    m_fullScreen = std::move(fullScreen);
}

bool Sounds::fullScreenNow() const
{
#ifdef Q_OS_WIN
    QUERY_USER_NOTIFICATION_STATE state;
    if (SUCCEEDED(SHQueryUserNotificationState(&state))
        && (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE))
        return true;
#endif
    return m_fullScreen && m_fullScreen();
}

void Sounds::play(const QString &name)
{
    if (m_silent && m_silent())
        return;
    const SoundsModel::Group group = SoundsModel::groupOf(name);
    if (group == SoundsModel::Unknown)
        return;
    SoundsModel::State s;
    s.enabled = enabled();
    s.groupOn = value(SoundsModel::groupKey(group), true);
    s.quietWhenBusy = quietWhenBusy();
    s.micInUse = s.quietWhenBusy && quietOnMic() && m_micInUse && m_micInUse();
    s.fullScreen = s.quietWhenBusy && fullScreenNow();
    const auto last = m_last.constFind(name);
    s.sinceSameMs = last == m_last.cend() ? -1 : last->elapsed();
    if (!SoundsModel::shouldPlay(name, s))
        return;
    m_last[name].start();
    // Secret: one notification in a thousand neighs. Prayer times have their own name so the adhan never does.
    if (name == QLatin1String("prayer"))
        output(QStringLiteral("notify"));
    else if (name == QLatin1String("notify") && QRandomGenerator::global()->bounded(1000) == 0)
        output(QStringLiteral("neigh"));
    else
        output(name);
}

void Sounds::preview(const QString &name)
{
    if (SoundsModel::groupOf(name) != SoundsModel::Unknown)
        output(name);
}

void Sounds::playRing()
{
    m_rings = 0;
    play(QStringLiteral("ring"));
    m_ring.start();
}

void Sounds::stopRing()
{
    m_ring.stop();
}

void Sounds::output(const QString &name)
{
#ifdef Q_OS_WIN
    QFile f(QStringLiteral(":/qt/qml/Notch/src/qml/sounds/%1.wav").arg(name));
    if (!f.open(QIODevice::ReadOnly))
        return;
    QByteArray wav = f.readAll();
    // Scale the 16-bit samples of the data chunk: PlaySound has no volume of its own.
    const qsizetype data = wav.indexOf("data", 12);
    if (data < 0 || data + 8 > wav.size())
        return;
    const double g = gain(volume());
    auto *samples = reinterpret_cast<qint16 *>(wav.data() + data + 8);
    const qsizetype count = (wav.size() - data - 8) / 2;
    for (qsizetype i = 0; i < count; ++i)
        samples[i] = qToLittleEndian(qint16(std::lround(qFromLittleEndian(samples[i]) * g)));
    PlaySoundW(nullptr, nullptr, 0); // stop the one playing before its buffer is replaced
    m_playing = wav;
    PlaySoundW(reinterpret_cast<LPCWSTR>(m_playing.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    const QString file = m_dir + u'/' + name + QStringLiteral(".wav");
    const bool started = QProcess::startDetached(QStringLiteral("paplay"),
                                                 {QStringLiteral("--volume=%1").arg(qRound(65536 * volume() / 100.0)),
                                                  QStringLiteral("--client-name=Olyzano Notch"), file});
    if (!started && !m_warned) {
        m_warned = true;
        qCWarning(lcNotch) << "paplay not available: no sound effects";
    }
#endif
}
