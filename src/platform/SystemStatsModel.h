#pragma once

#include <QByteArray>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// What the System section of Quick Settings reads from the kernel's text files, as pure functions so the tests can feed
// them small fixtures: memory, processor load, network and disk traffic (all counters: the rate comes from two reads).
namespace SystemStatsModel {

struct CpuTimes {
    quint64 idle = 0, total = 0;
    bool ok = false;
};

// The first line of /proc/stat: "cpu  user nice system idle iowait irq softirq steal ...". Idle includes iowait.
inline CpuTimes parseCpu(const QByteArray &stat)
{
    CpuTimes t;
    const QList<QByteArray> lines = stat.split('\n');
    if (lines.isEmpty() || !lines.first().startsWith("cpu "))
        return t;
    const QList<QByteArray> f = lines.first().simplified().split(' ');
    if (f.size() < 5)
        return t;
    for (int i = 1; i < f.size() && i <= 8; ++i) // guest time is already inside user: the first eight fields only
        t.total += f.at(i).toULongLong();
    t.idle = f.at(4).toULongLong() + (f.size() > 5 ? f.at(5).toULongLong() : 0);
    t.ok = true;
    return t;
}

// Load between two reads, 0..100.
inline double cpuPercent(const CpuTimes &a, const CpuTimes &b)
{
    if (!a.ok || !b.ok || b.total <= a.total)
        return 0;
    const double total = double(b.total - a.total), idle = double(b.idle >= a.idle ? b.idle - a.idle : 0);
    return qBound(0.0, 100.0 * (total - idle) / total, 100.0);
}

struct Memory {
    qint64 total = 0, available = 0, swapTotal = 0, swapFree = 0; // bytes
    qint64 used() const { return qMax<qint64>(0, total - available); }
    qint64 swapUsed() const { return qMax<qint64>(0, swapTotal - swapFree); }
};

// /proc/meminfo: "MemTotal:  16314648 kB". "Used" is what is not available, the way free(1) and the task managers count it.
inline Memory parseMeminfo(const QByteArray &text)
{
    Memory m;
    for (const QByteArray &line : text.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray key = line.left(colon);
        const qint64 bytes = line.mid(colon + 1).simplified().split(' ').value(0).toLongLong() * 1024;
        if (key == "MemTotal") m.total = bytes;
        else if (key == "MemAvailable") m.available = bytes;
        else if (key == "SwapTotal") m.swapTotal = bytes;
        else if (key == "SwapFree") m.swapFree = bytes;
    }
    return m;
}

struct Traffic {
    quint64 in = 0, out = 0; // bytes received / sent (network), read / written (disk)
};

// Interfaces that carry real traffic: not the loopback, containers' virtual ones, bridges and tunnels.
inline bool isRealInterface(const QString &name)
{
    static const QStringList skip = {QStringLiteral("lo"), QStringLiteral("docker"), QStringLiteral("veth"), QStringLiteral("br-"),
                                     QStringLiteral("virbr"), QStringLiteral("vmnet"), QStringLiteral("tun"), QStringLiteral("tap")};
    for (const QString &p : skip)
        if (name == p || (p.size() > 2 && name.startsWith(p)))
            return false;
    return true;
}

// /proc/net/dev: "  eth0: rxbytes rxpackets ... (8 fields) txbytes ...".
inline Traffic parseNetDev(const QByteArray &text)
{
    Traffic t;
    for (const QByteArray &line : text.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        if (!isRealInterface(QString::fromLatin1(line.left(colon).trimmed())))
            continue;
        const QList<QByteArray> f = line.mid(colon + 1).simplified().split(' ');
        if (f.size() < 9)
            continue;
        t.in += f.at(0).toULongLong();
        t.out += f.at(8).toULongLong();
    }
    return t;
}

// Whole disks only (sda, nvme0n1, vda, mmcblk0), not their partitions, so nothing is counted twice.
inline bool isPhysicalDisk(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^(sd[a-z]+|vd[a-z]+|xvd[a-z]+|nvme\\d+n\\d+|mmcblk\\d+)$"));
    return re.match(name).hasMatch();
}

// /proc/diskstats: "major minor name reads merged sectorsRead ms writes merged sectorsWritten ...", sectors of 512 bytes.
inline Traffic parseDiskstats(const QByteArray &text)
{
    Traffic t;
    for (const QByteArray &line : text.split('\n')) {
        const QList<QByteArray> f = line.simplified().split(' ');
        if (f.size() < 10 || !isPhysicalDisk(QString::fromLatin1(f.at(2))))
            continue;
        t.in += f.at(5).toULongLong() * 512;
        t.out += f.at(9).toULongLong() * 512;
    }
    return t;
}

// Bytes per second between two counter reads `ms` apart. A counter that went backwards (an interface restarted) is 0, not huge.
inline double ratePerSecond(quint64 before, quint64 now, qint64 ms)
{
    if (ms <= 0 || now < before)
        return 0;
    return double(now - before) * 1000.0 / double(ms);
}

// "512 B", "1.5 KB", "23 MB", "1.2 GB": three significant digits at most.
inline QString formatBytes(double bytes)
{
    static const char *const unit[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    while (bytes >= 1024 && u < 4) {
        bytes /= 1024;
        ++u;
    }
    return QString::number(bytes, 'f', u == 0 || bytes >= 100 ? 0 : bytes >= 10 ? 1 : 2) + QLatin1Char(' ') + QLatin1String(unit[u]);
}

inline QString formatRate(double bytesPerSecond) { return formatBytes(bytesPerSecond) + QStringLiteral("/s"); }

}
