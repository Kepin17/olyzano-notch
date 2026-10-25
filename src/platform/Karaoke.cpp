#include "platform/Karaoke.h"

#include "platform/KaraokeModel.h"

#include <QProcess>
#include <QStandardPaths>

namespace {
// pactl, run to the end (it answers at once); false on a failure or a hang.
bool pactl(const QStringList &args, QString *out = nullptr)
{
    QProcess p;
    p.start(QStringLiteral("pactl"), args);
    if (!p.waitForFinished(2500) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (p.state() != QProcess::NotRunning)
            p.kill();
        return false;
    }
    if (out)
        *out = QString::fromUtf8(p.readAllStandardOutput());
    return true;
}
}

Karaoke::Karaoke(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_LINUX
    m_supported = !QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty();
#endif
    if (m_supported)
        removeStale();
}

Karaoke::~Karaoke()
{
    stop();
}

void Karaoke::removeStale()
{
    QString list;
    if (pactl({QStringLiteral("list"), QStringLiteral("modules"), QStringLiteral("short")}, &list))
        for (const int id : KaraokeModel::staleModules(list))
            pactl({QStringLiteral("unload-module"), QString::number(id)});
}

void Karaoke::toggle()
{
    if (!m_supported)
        return;
    if (active()) {
        stop();
        return;
    }
    QString out;
    if (!pactl(KaraokeModel::loadArgs(), &out) || KaraokeModel::moduleId(out) < 0) {
        m_error = tr("Couldn't start the microphone loop (is the sound server running?)");
        emit changed();
        return;
    }
    m_error.clear();
    m_module = KaraokeModel::moduleId(out);
    emit changed();
}

void Karaoke::stop()
{
    if (m_module < 0)
        return;
    pactl({QStringLiteral("unload-module"), QString::number(m_module)});
    m_module = -1;
    emit changed();
}
