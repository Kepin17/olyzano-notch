#pragma once

#include "platform/NotchPaths.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QString>

// What the language models cost while they run: how long they are kept after the last use (one setting for all of them), how much memory each
// running program holds, and which ones run. Pure where it can be, so the tests drive the parts.
namespace Audio {
namespace ModelUse {

// The choices of "Free models after" in minutes; anything else becomes the default of 2.
inline int validIdleMinutes(int m) { return m == 1 || m == 2 || m == 5 || m == 10 ? m : 2; }

inline int idleMinutes()
{
    QSettings s(NotchPaths::conf(QStringLiteral("audio")), QSettings::IniFormat);
    return validIdleMinutes(s.value(QStringLiteral("idleMinutes"), 2).toInt());
}
inline qint64 idleMs() { return qint64(idleMinutes()) * 60 * 1000; }

// "VmRSS:   123456 kB" out of /proc/<pid>/status -> bytes (0 when it is not there).
inline qint64 parseVmRss(const QByteArray &status)
{
    for (const QByteArray &line : status.split('\n')) {
        const QList<QByteArray> p = line.simplified().split(' ');
        if (p.size() >= 2 && p[0] == "VmRSS:")
            return p[1].toLongLong() * 1024;
    }
    return 0;
}

// The resident memory of a process in bytes; 0 when it cannot be read (it ended, or no permission). Defined in AudioEngines.cpp (Windows headers stay out of here).
qint64 rssBytes(qint64 pid);

// "3.1 GB", "420 MB".
inline QString sizeText(qint64 bytes)
{
    if (bytes >= (qint64(1) << 30) * 0.95)
        return QStringLiteral("%1 GB").arg(double(bytes) / double(qint64(1) << 30), 0, 'f', 1);
    return QStringLiteral("%1 MB").arg((bytes + (1 << 19)) >> 20);
}

// The language model programs running now (pid -> name): each one adds itself when it starts and takes itself out when it stops.
class Running
{
public:
    static Running &instance()
    {
        static Running r;
        return r;
    }
    void add(qint64 pid, const QString &name) { QMutexLocker l(&m_mutex); m_list.insert(pid, name); }
    void remove(qint64 pid) { QMutexLocker l(&m_mutex); m_list.remove(pid); }
    QHash<qint64, QString> list() const { QMutexLocker l(&m_mutex); return m_list; }

private:
    mutable QMutex m_mutex;
    QHash<qint64, QString> m_list;
};

}
}
