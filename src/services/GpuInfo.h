#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Which graphics processors this computer has and how much memory each can offer, so the speech and language models can be given to
// the one that suits them (or to the processor when none does). The parsing is pure and tested; `detect()` reads the system.
namespace Gpu {

struct Device {
    QString vendor;          // nvidia | amd | intel | other
    QString name;            // as the system names it ("NVIDIA GeForce RTX 4060 Laptop GPU"), or vendor and id when it does not say
    qint64 vramTotal = 0;    // bytes the models can use: its own memory, or for an integrated one the memory it shares with the system
    qint64 vramFree = 0;
    bool integrated = false; // shares the system's memory (no memory of its own to speak of)
    QString key() const { return vendor + u'|' + name + u'|' + QString::number(vramTotal); } // what a stored measurement belongs to
};

struct Info {
    QList<Device> devices;
    bool vulkan = false; // a Vulkan loader and at least one driver are present
};

inline QString vendorOf(const QString &pciVendor)
{
    const QString v = pciVendor.trimmed().toLower();
    if (v == QLatin1String("0x10de")) return QStringLiteral("nvidia");
    if (v == QLatin1String("0x1002") || v == QLatin1String("0x1022")) return QStringLiteral("amd");
    if (v == QLatin1String("0x8086")) return QStringLiteral("intel");
    return QStringLiteral("other");
}

// `nvidia-smi --query-gpu=name,memory.total,memory.used --format=csv,noheader,nounits`: "NVIDIA GeForce RTX 4060 Laptop GPU, 8188, 512" per line (MiB)
inline QList<Device> parseNvidiaSmi(const QByteArray &csv)
{
    QList<Device> out;
    for (const QByteArray &raw : csv.split('\n')) {
        const QList<QByteArray> p = raw.trimmed().split(',');
        if (p.size() < 3)
            continue;
        bool ok1 = false, ok2 = false;
        const qint64 total = p[p.size() - 2].trimmed().toLongLong(&ok1) << 20, used = p[p.size() - 1].trimmed().toLongLong(&ok2) << 20;
        if (!ok1 || !ok2 || total <= 0)
            continue;
        Device d;
        d.vendor = QStringLiteral("nvidia");
        d.name = QString::fromUtf8(p.mid(0, p.size() - 2).join(',')).trimmed();
        d.vramTotal = total;
        d.vramFree = qMax<qint64>(0, total - used);
        out.append(d);
    }
    return out;
}

// What /sys/class/drm/cardN/device says
struct Sysfs {
    QString vendor, device; // "0x1002", "0x1900"
    qint64 vramTotal = -1, vramUsed = -1, gttTotal = -1, gttUsed = -1; // bytes, -1: not told
};

// An integrated processor has a small slice of memory of its own (a carve-out) and uses the system's for the rest (GTT): then it is
// the system's memory that counts. Intel does not tell: its memory is the system's.
inline Device fromSysfs(const Sysfs &s)
{
    Device d;
    d.vendor = vendorOf(s.vendor);
    d.name = QStringLiteral("%1 GPU (%2:%3)").arg(d.vendor == QLatin1String("nvidia") ? QStringLiteral("NVIDIA") : d.vendor == QLatin1String("amd") ? QStringLiteral("AMD") : d.vendor == QLatin1String("intel") ? QStringLiteral("Intel") : QStringLiteral("Unknown"),
                                                   s.vendor.mid(2), s.device.mid(2));
    const qint64 ownMemoryMin = Q_INT64_C(2) << 30;
    if (s.vramTotal > 0 && s.vramTotal >= ownMemoryMin) {
        d.vramTotal = s.vramTotal;
        d.vramFree = s.vramUsed >= 0 ? qMax<qint64>(0, s.vramTotal - s.vramUsed) : s.vramTotal;
    } else {
        d.integrated = true;
        d.vramTotal = s.gttTotal > 0 ? s.gttTotal : 0;
        d.vramFree = s.gttTotal > 0 ? qMax<qint64>(0, s.gttTotal - qMax<qint64>(0, s.gttUsed)) : 0;
    }
    return d;
}

Info detect(); // reads the system (the first call takes a moment: run it on a worker thread)

} // namespace Gpu
