#pragma once

#include <QProcess>

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#endif

// A helper that runs as long as Notch does (pactl subscribe, parec) must end with it. If Notch crashes, such a child would
// otherwise live on, keep Notch's systemd unit "running" and show up as another "Olyzano Notch" in System Monitor.
namespace ChildLife {

inline void dieWithParent(QProcess &process)
{
#ifdef Q_OS_LINUX
    process.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
#else
    Q_UNUSED(process)
#endif
}

} // namespace ChildLife
