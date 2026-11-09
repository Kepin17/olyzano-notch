#include "GpuInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <dxgi.h>
#endif

namespace Gpu {

#ifndef Q_OS_WIN
namespace {
qint64 number(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed().toLongLong() : -1;
}
QString text(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}
}
#endif

Info detect()
{
    Info info;
#ifdef Q_OS_WIN
    IDXGIFactory1 *factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))) && factory) {
        IDXGIAdapter1 *a = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(a->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                Device d;
                d.vendor = vendorOf(QStringLiteral("0x%1").arg(desc.VendorId, 4, 16, QLatin1Char('0')));
                d.name = QString::fromWCharArray(desc.Description);
                d.integrated = desc.DedicatedVideoMemory < (Q_INT64_C(1) << 30);
                d.vramTotal = qint64(d.integrated ? desc.SharedSystemMemory : desc.DedicatedVideoMemory);
                d.vramFree = d.vramTotal; // not told by this call
                info.devices.append(d);
            }
            a->Release();
        }
        factory->Release();
    }
    QLibrary vk(QStringLiteral("vulkan-1"));
    info.vulkan = vk.load();
#else
    QList<Device> found;
    const QDir drm(QStringLiteral("/sys/class/drm"));
    for (const QString &card : drm.entryList({QStringLiteral("card*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!QRegularExpression(QStringLiteral("^card\\d+$")).match(card).hasMatch())
            continue; // card1-eDP-1 and the like are outputs
        const QString dev = drm.filePath(card) + QStringLiteral("/device/");
        Sysfs s;
        s.vendor = text(dev + QStringLiteral("vendor"));
        s.device = text(dev + QStringLiteral("device"));
        if (s.vendor.isEmpty())
            continue;
        s.vramTotal = number(dev + QStringLiteral("mem_info_vram_total"));
        s.vramUsed = number(dev + QStringLiteral("mem_info_vram_used"));
        s.gttTotal = number(dev + QStringLiteral("mem_info_gtt_total"));
        s.gttUsed = number(dev + QStringLiteral("mem_info_gtt_used"));
        Device d = fromSysfs(s);
        if (d.integrated && d.vramTotal <= 0) { // Intel and the like: the system's memory
            QFile mem(QStringLiteral("/proc/meminfo"));
            if (mem.open(QIODevice::ReadOnly)) {
                const QRegularExpression re(QStringLiteral("MemTotal:\\s+(\\d+) kB\\s.*MemAvailable:\\s+(\\d+) kB"), QRegularExpression::DotMatchesEverythingOption);
                const auto m = re.match(QString::fromUtf8(mem.readAll()));
                if (m.hasMatch()) {
                    d.vramTotal = m.captured(1).toLongLong() * 1024 / 2; // half of it, as drivers allow
                    d.vramFree = m.captured(2).toLongLong() * 1024 / 2;
                }
            }
        }
        found.append(d);
    }
    // NVIDIA tells its marketing name and memory through its own tool: that replaces what the file system gave for it
    const QString smi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    if (!smi.isEmpty()) {
        QProcess p;
        p.start(smi, {QStringLiteral("--query-gpu=name,memory.total,memory.used"), QStringLiteral("--format=csv,noheader,nounits")});
        if (p.waitForFinished(4000) && p.exitCode() == 0) {
            const QList<Device> nv = parseNvidiaSmi(p.readAllStandardOutput());
            if (!nv.isEmpty()) {
                found.erase(std::remove_if(found.begin(), found.end(), [](const Device &d) { return d.vendor == QLatin1String("nvidia"); }), found.end());
                found += nv;
            }
        }
    }
    info.devices = found;
    QLibrary vk(QStringLiteral("vulkan"), 1);
    bool icd = false;
    for (const QString &dir : {QStringLiteral("/usr/share/vulkan/icd.d"), QStringLiteral("/etc/vulkan/icd.d"), QStringLiteral("/usr/local/share/vulkan/icd.d")})
        icd = icd || !QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty();
    info.vulkan = icd && vk.load();
#endif
    return info;
}

} // namespace Gpu
