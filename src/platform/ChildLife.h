#pragma once

#include <QProcess>

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// A helper that runs as long as Notch does (pactl subscribe, parec) must end with it. If Notch crashes, such a child would
// otherwise live on, keep Notch's systemd unit "running" and show up as another "Olyzano Notch" in System Monitor.
namespace ChildLife {

inline void dieWithParent(QProcess &process)
{
#ifdef Q_OS_LINUX
    process.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
#elif defined(Q_OS_WIN)
    // One job for all of them, set to end its processes when it is closed, which is when Notch ends (a crash included): the child joins it
    // as soon as it has started.
    static HANDLE job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (j) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        return j;
    }();
    QObject::connect(&process, &QProcess::started, &process, [&process] {
        if (!job)
            return;
        HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process.processId()));
        if (h) {
            AssignProcessToJobObject(job, h);
            CloseHandle(h);
        }
    });
#else
    Q_UNUSED(process)
#endif
}

} // namespace ChildLife
