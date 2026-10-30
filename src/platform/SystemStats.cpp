#include "SystemStats.h"

#include <QDir>
#include <QRegularExpression>
#include <QFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace SystemStatsModel;

namespace {

QByteArray slurp(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

double number(const QString &path)
{
    return slurp(path).trimmed().toDouble();
}

}

SystemStats::SystemStats(QObject *parent)
    : QObject(parent)
    , m_cores(qMax(1, QThread::idealThreadCount()))
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &SystemStats::poll);
}

void SystemStats::setActive(bool on)
{
    if (on == m_timer.isActive())
        return;
    if (on) {
        m_haveNetPrev = m_haveDiskPrev = false; // the first read after a pause has nothing to compare with
        m_cpuPrev = {};
        m_ticksPrev.clear();
        m_clock.start();
        m_lastMs = 0;
        m_timer.start();
        poll();
    } else {
        m_timer.stop();
    }
    emit activeChanged();
}

void SystemStats::setTopWanted(bool on)
{
    if (on == m_topWanted)
        return;
    m_topWanted = on;
    if (!on) {
        m_ticksPrev.clear();
        m_topCpu.clear();
        m_topRam.clear();
    }
    emit activeChanged();
}

void SystemStats::poll()
{
    const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
    const qint64 ms = now - m_lastMs;
    m_lastMs = now;
    ++m_tick;

#ifdef Q_OS_LINUX
    const CpuTimes cpu = parseCpu(slurp(QStringLiteral("/proc/stat")));
    m_cpu = cpuPercent(m_cpuPrev, cpu);
    m_cpuPrev = cpu;
    m_mem = parseMeminfo(slurp(QStringLiteral("/proc/meminfo")));

    const Traffic net = parseNetDev(slurp(QStringLiteral("/proc/net/dev")));
    m_netAvailable = true;
    if (m_haveNetPrev) {
        m_netDown = ratePerSecond(m_netPrev.in, net.in, ms);
        m_netUp = ratePerSecond(m_netPrev.out, net.out, ms);
    }
    m_netPrev = net;
    m_haveNetPrev = true;

    const Traffic disk = parseDiskstats(slurp(QStringLiteral("/proc/diskstats")));
    m_diskIo = true;
    if (m_haveDiskPrev) {
        m_diskRead = ratePerSecond(m_diskPrev.in, disk.in, ms);
        m_diskWrite = ratePerSecond(m_diskPrev.out, disk.out, ms);
    }
    m_diskPrev = disk;
    m_haveDiskPrev = true;
    if (m_tick % 2 == 1)
        readTemp();
    readGpu();
    if (m_topWanted)
        readTop();
#elif defined(Q_OS_WIN)
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const auto u64 = [](const FILETIME &f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
        CpuTimes t;
        t.idle = u64(idle);
        t.total = u64(kernel) + u64(user); // kernel time already includes the idle time
        t.ok = true;
        m_cpu = cpuPercent(m_cpuPrev, t);
        m_cpuPrev = t;
    }
    MEMORYSTATUSEX ms2{};
    ms2.dwLength = sizeof(ms2);
    if (GlobalMemoryStatusEx(&ms2)) {
        m_mem.total = qint64(ms2.ullTotalPhys);
        m_mem.available = qint64(ms2.ullAvailPhys);
        m_mem.swapTotal = qint64(ms2.ullTotalPageFile > ms2.ullTotalPhys ? ms2.ullTotalPageFile - ms2.ullTotalPhys : 0);
        m_mem.swapFree = qint64(ms2.ullAvailPageFile > ms2.ullAvailPhys ? ms2.ullAvailPageFile - ms2.ullAvailPhys : 0);
    }
#endif
    if (m_tick % 5 == 1) { // the system drive's space changes slowly
        const QStorageInfo root = QStorageInfo::root();
        if (root.isValid()) {
            m_diskTotal = double(root.bytesTotal());
            m_diskUsed = double(root.bytesTotal() - root.bytesFree()); // what df calls Used (the reserved part is not counted)
        }
    }
    emit changed();
}

// The temperature of the processor, from the kernel's hwmon sensors (k10temp: AMD, coretemp: Intel).
void SystemStats::readTemp()
{
#ifdef Q_OS_LINUX
    const QDir hw(QStringLiteral("/sys/class/hwmon"));
    for (const QString &d : hw.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString name = QString::fromUtf8(slurp(hw.filePath(d) + QStringLiteral("/name")).trimmed());
        if (name != QLatin1String("k10temp") && name != QLatin1String("coretemp") && name != QLatin1String("zenpower"))
            continue;
        const double milli = number(hw.filePath(d) + QStringLiteral("/temp1_input"));
        m_cpuTemp = milli > 0 ? qRound(milli / 1000.0) : -1;
        return;
    }
    m_cpuTemp = -1;
#endif
}

// Graphics: an AMD card's own counters, or NVIDIA's tool; an Intel one has no cheap load figure, so only its name shows.
void SystemStats::findGpu()
{
    m_gpuSearched = true;
#ifdef Q_OS_LINUX
    const QDir drm(QStringLiteral("/sys/class/drm"));
    for (const QString &c : drm.entryList({QStringLiteral("card?")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dev = drm.filePath(c) + QStringLiteral("/device");
        if (QFile::exists(dev + QStringLiteral("/gpu_busy_percent"))) {
            m_gpuDir = dev;
            m_gpuAvailable = true;
            break;
        }
    }
    if (!m_gpuAvailable) {
        m_nvidia = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
        m_gpuAvailable = !m_nvidia.isEmpty();
    }
    // the name, once: lspci -mm lines are  slot "class" "vendor" "device" ...
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p] {
        const QString out = QString::fromUtf8(p->readAllStandardOutput());
        p->deleteLater();
        for (const QString &line : out.split(u'\n')) {
            if (!line.contains(QLatin1String("VGA")) && !line.contains(QLatin1String("3D controller")) && !line.contains(QLatin1String("Display controller")))
                continue;
            QStringList q;
            int i = 0;
            while ((i = line.indexOf(u'"', i)) >= 0) {
                const int j = line.indexOf(u'"', i + 1);
                if (j < 0)
                    break;
                q << line.mid(i + 1, j - i - 1);
                i = j + 1;
            }
            if (q.size() >= 3) {
                QString vendor = q.at(1), device = q.at(2);
                const int b = device.indexOf(u'[');
                if (b >= 0 && device.endsWith(u']'))
                    device = device.mid(b + 1, device.size() - b - 2); // "Phoenix1 [Radeon 780M]" -> "Radeon 780M"
                vendor.replace(QRegularExpression(QStringLiteral("\\s*\\[.*\\]|,? Inc\\.?|Corporation|\\(R\\)")), QString());
                vendor.replace(QStringLiteral("Advanced Micro Devices"), QStringLiteral("AMD")).replace(QStringLiteral("NVIDIA Corporation"), QStringLiteral("NVIDIA"));
                if (m_gpuName.isEmpty() || (m_gpuAvailable && !vendor.isEmpty()))
                    m_gpuName = (vendor.trimmed() + u' ' + device).trimmed();
                if (m_gpuAvailable)
                    break;
            }
        }
        emit changed();
    });
    p->start(QStringLiteral("lspci"), {QStringLiteral("-mm")});
#endif
}

void SystemStats::readGpu()
{
#ifdef Q_OS_LINUX
    if (!m_gpuSearched)
        findGpu();
    if (!m_gpuDir.isEmpty()) {
        m_gpu = number(m_gpuDir + QStringLiteral("/gpu_busy_percent"));
        m_vramUsed = number(m_gpuDir + QStringLiteral("/mem_info_vram_used"));
        m_vramTotal = number(m_gpuDir + QStringLiteral("/mem_info_vram_total"));
    } else if (!m_nvidia.isEmpty() && m_tick % 2 == 0 && (!m_smi || m_smi->state() == QProcess::NotRunning)) {
        if (!m_smi) {
            m_smi = new QProcess(this);
            connect(m_smi, &QProcess::finished, this, [this] {
                const QStringList f = QString::fromUtf8(m_smi->readAllStandardOutput()).trimmed().split(QLatin1String(", "));
                if (f.size() >= 4) {
                    if (m_gpuName.isEmpty())
                        m_gpuName = f.at(0);
                    m_gpu = f.at(1).toDouble();
                    m_vramUsed = f.at(2).toDouble() * 1048576;
                    m_vramTotal = f.at(3).toDouble() * 1048576;
                    emit changed();
                }
            });
        }
        m_smi->start(m_nvidia, {QStringLiteral("--query-gpu=name,utilization.gpu,memory.used,memory.total"), QStringLiteral("--format=csv,noheader,nounits")});
    }
#endif
}

// The three that use the most of the processor (since the last second) and of the memory, from /proc/<pid>.
void SystemStats::readTop()
{
#ifdef Q_OS_LINUX
    struct Row { QString name; double cpu; double ram; };
    QList<Row> rows;
    QHash<qint64, quint64> ticks;
    const qint64 now = m_clock.elapsed();
    const double secs = m_topPrevMs > 0 ? double(now - m_topPrevMs) / 1000.0 : 0;
    m_topPrevMs = now;
    static const double hz = 100.0, page = 4096.0; // clock ticks per second and bytes per page on Linux
    const QDir proc(QStringLiteral("/proc"));
    for (const QString &e : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const qint64 pid = e.toLongLong(&ok);
        if (!ok)
            continue;
        const QByteArray stat = slurp(proc.filePath(e) + QStringLiteral("/stat"));
        const int close = stat.lastIndexOf(')');
        if (close < 0)
            continue;
        const QList<QByteArray> f = stat.mid(close + 2).split(' ');
        if (f.size() < 13)
            continue;
        const quint64 t = f.at(11).toULongLong() + f.at(12).toULongLong(); // utime + stime
        ticks.insert(pid, t);
        const double cpu = secs > 0 && m_ticksPrev.contains(pid) && t >= m_ticksPrev.value(pid) ? double(t - m_ticksPrev.value(pid)) / hz / secs * 100.0 : 0;
        const double ram = slurp(proc.filePath(e) + QStringLiteral("/statm")).split(' ').value(1).toDouble() * page;
        if (cpu < 0.5 && ram < 50 * 1048576)
            continue; // not worth listing
        rows.append({QString::fromUtf8(slurp(proc.filePath(e) + QStringLiteral("/comm")).trimmed()), cpu, ram});
    }
    m_ticksPrev = ticks;
    const auto top = [&rows](auto key) {
        QList<Row> r = rows;
        std::sort(r.begin(), r.end(), [&](const Row &a, const Row &b) { return key(a) > key(b); });
        // the same program in many processes (a browser) counts once
        QHash<QString, double> sum;
        for (const Row &x : std::as_const(r))
            sum[x.name] += key(x);
        QList<QPair<double, QString>> l;
        for (auto it = sum.cbegin(); it != sum.cend(); ++it)
            l.append({it.value(), it.key()});
        std::sort(l.begin(), l.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        QVariantList out;
        for (int i = 0; i < l.size() && i < 3; ++i)
            if (l.at(i).first > 0)
                out.append(QVariantMap{{QStringLiteral("name"), l.at(i).second}, {QStringLiteral("value"), l.at(i).first}});
        return out;
    };
    m_topCpu = top([](const Row &r) { return r.cpu; });
    m_topRam = top([](const Row &r) { return r.ram; });
#endif
}
