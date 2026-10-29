#include "MemoryTuner.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QProcess>
#include <QThreadPool>

#ifdef Q_OS_LINUX
#include <malloc.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString choiceFile() { return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/perf.conf"); }
}

MemoryTuner::MemoryTuner(QObject *parent) : QObject(parent)
{
    QThreadPool::globalInstance()->setMaxThreadCount(4); // QtConcurrent jobs (copying, encrypting, speech): a few at a time is plenty
}

void MemoryTuner::applyEarly()
{
#ifdef Q_OS_LINUX
    mallopt(M_ARENA_MAX, 2);
    if (!qEnvironmentVariableIsSet("MESA_GLTHREAD"))
        qputenv("MESA_GLTHREAD", "false"); // Mesa's own helper thread: a thread and a few MB for nothing on a small window
#endif
    if (QSettings(choiceFile(), QSettings::IniFormat).value(QStringLiteral("lowMemory"), false).toBool()
        && !qEnvironmentVariableIsSet("QT_QUICK_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", "software");
#ifdef Q_OS_LINUX
        // With the software renderer the Wayland plug-in must not set up EGL either: that is what loads Mesa and LLVM (some 30 MB).
        if (!qEnvironmentVariableIsSet("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION"))
            qputenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "shm");
#endif
    }
}

bool MemoryTuner::available() const
{
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

bool MemoryTuner::lowMemory() const { return QSettings(choiceFile(), QSettings::IniFormat).value(QStringLiteral("lowMemory"), false).toBool(); }

void MemoryTuner::setLowMemory(bool on)
{
    if (on == lowMemory())
        return;
    QDir().mkpath(QFileInfo(choiceFile()).absolutePath());
    QSettings s(choiceFile(), QSettings::IniFormat);
    s.setValue(QStringLiteral("lowMemory"), on);
    s.sync();
    emit changed();
}

bool MemoryTuner::running() const { return qgetenv("QT_QUICK_BACKEND") == "software"; }

void MemoryTuner::trim()
{
#ifdef Q_OS_LINUX
    malloc_trim(0);
#elif defined(Q_OS_WIN)
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); // pages the working set back out; faults in again on use
#endif
}

void MemoryTuner::restart()
{
    // the new copy waits a moment so this one has let go of the single-instance lock
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("cmd.exe"),
                            {QStringLiteral("/c"),
                             QStringLiteral("timeout /t 1 /nobreak >nul & start \"\" \"%1\"").arg(QCoreApplication::applicationFilePath())});
#else
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 1; exec \"$0\""), QCoreApplication::applicationFilePath()});
#endif
    QCoreApplication::quit();
}
