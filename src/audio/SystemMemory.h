#pragma once

#include <QByteArray>
#include <QFile>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// How much memory is free right now, to say "this model will not fit" before loading it instead of freezing the computer. -1 when unknown.
namespace Audio {

// "MemAvailable:   12345678 kB" in /proc/meminfo -> bytes
inline qint64 parseMemAvailable(const QByteArray &meminfo)
{
    for (const QByteArray &line : meminfo.split('\n')) {
        if (line.startsWith("MemAvailable:")) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() >= 2)
                return parts[1].toLongLong() * 1024;
        }
    }
    return -1;
}

inline qint64 availableMemory()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX m;
    m.dwLength = sizeof(m);
    return GlobalMemoryStatusEx(&m) ? qint64(m.ullAvailPhys) : -1;
#else
    QFile f(QStringLiteral("/proc/meminfo"));
    return f.open(QIODevice::ReadOnly) ? parseMemAvailable(f.readAll()) : -1;
#endif
}

} // namespace Audio
