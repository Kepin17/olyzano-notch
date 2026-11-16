#include "ModelUse.h"

#include <QFile>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace Audio::ModelUse {
qint64 rssBytes(qint64 pid)
{
    if (pid <= 0)
        return 0;
#ifdef Q_OS_LINUX
    QFile f(QStringLiteral("/proc/%1/status").arg(pid));
    return f.open(QIODevice::ReadOnly) ? parseVmRss(f.readAll()) : 0;
#elif defined(Q_OS_WIN)
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h)
        return 0;
    PROCESS_MEMORY_COUNTERS c = {};
    const bool ok = GetProcessMemoryInfo(h, &c, sizeof(c));
    CloseHandle(h);
    return ok ? qint64(c.WorkingSetSize) : 0;
#else
    return 0;
#endif
}
}
