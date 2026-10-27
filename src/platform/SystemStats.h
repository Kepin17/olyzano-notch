#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

#include "platform/SystemStatsModel.h"

// How busy the computer is, for the System section of Quick Settings: processor, memory, graphics, disk and network. It reads
// only while `active` (the card is open), once a second, and the heavier "who is using it" list only while `topWanted`.
// Linux reads /proc and /sys; Windows gives the processor, memory and disk space (graphics, disk and network traffic are
// not read there yet: the matching `...Available` flags stay false and the section leaves those rows out).
class SystemStats : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool topWanted READ topWanted WRITE setTopWanted NOTIFY activeChanged)
    Q_PROPERTY(double cpu READ cpu NOTIFY changed)                 // percent
    Q_PROPERTY(int cpuTemp READ cpuTemp NOTIFY changed)            // degrees C, -1 when unknown
    Q_PROPERTY(int cores READ cores CONSTANT)
    Q_PROPERTY(double ramUsed READ ramUsed NOTIFY changed)         // bytes
    Q_PROPERTY(double ramTotal READ ramTotal NOTIFY changed)
    Q_PROPERTY(double swapUsed READ swapUsed NOTIFY changed)
    Q_PROPERTY(double swapTotal READ swapTotal NOTIFY changed)
    Q_PROPERTY(bool gpuAvailable READ gpuAvailable NOTIFY changed) // a graphics card that reports how busy it is
    Q_PROPERTY(QString gpuName READ gpuName NOTIFY changed)        // empty until found
    Q_PROPERTY(double gpu READ gpu NOTIFY changed)                 // percent
    Q_PROPERTY(double vramUsed READ vramUsed NOTIFY changed)       // bytes, 0 when unknown
    Q_PROPERTY(double vramTotal READ vramTotal NOTIFY changed)
    Q_PROPERTY(double diskUsed READ diskUsed NOTIFY changed)       // the system drive, bytes
    Q_PROPERTY(double diskTotal READ diskTotal NOTIFY changed)
    Q_PROPERTY(bool diskIoAvailable READ diskIoAvailable NOTIFY changed)
    Q_PROPERTY(double diskRead READ diskRead NOTIFY changed)       // bytes per second
    Q_PROPERTY(double diskWrite READ diskWrite NOTIFY changed)
    Q_PROPERTY(bool netAvailable READ netAvailable NOTIFY changed)
    Q_PROPERTY(double netDown READ netDown NOTIFY changed)         // bytes per second
    Q_PROPERTY(double netUp READ netUp NOTIFY changed)
    Q_PROPERTY(QVariantList topCpu READ topCpu NOTIFY changed)     // [{name, value (percent of one core)}], three of them
    Q_PROPERTY(QVariantList topRam READ topRam NOTIFY changed)     // [{name, value (bytes)}]

public:
    explicit SystemStats(QObject *parent = nullptr);

    bool active() const { return m_timer.isActive(); }
    void setActive(bool on);
    bool topWanted() const { return m_topWanted; }
    void setTopWanted(bool on);
    double cpu() const { return m_cpu; }
    int cpuTemp() const { return m_cpuTemp; }
    int cores() const { return m_cores; }
    double ramUsed() const { return double(m_mem.used()); }
    double ramTotal() const { return double(m_mem.total); }
    double swapUsed() const { return double(m_mem.swapUsed()); }
    double swapTotal() const { return double(m_mem.swapTotal); }
    bool gpuAvailable() const { return m_gpuAvailable; }
    QString gpuName() const { return m_gpuName; }
    double gpu() const { return m_gpu; }
    double vramUsed() const { return m_vramUsed; }
    double vramTotal() const { return m_vramTotal; }
    double diskUsed() const { return m_diskUsed; }
    double diskTotal() const { return m_diskTotal; }
    bool diskIoAvailable() const { return m_diskIo; }
    double diskRead() const { return m_diskRead; }
    double diskWrite() const { return m_diskWrite; }
    bool netAvailable() const { return m_netAvailable; }
    double netDown() const { return m_netDown; }
    double netUp() const { return m_netUp; }
    QVariantList topCpu() const { return m_topCpu; }
    QVariantList topRam() const { return m_topRam; }

signals:
    void changed();
    void activeChanged();

private:
    void poll();
    void findGpu();
    void readGpu();
    void readTop();
    void readTemp();

    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastMs = 0;
    bool m_topWanted = false;
    int m_cores = 1;
    int m_tick = 0;

    SystemStatsModel::CpuTimes m_cpuPrev;
    SystemStatsModel::Memory m_mem;
    SystemStatsModel::Traffic m_netPrev, m_diskPrev;
    bool m_haveNetPrev = false, m_haveDiskPrev = false;
    double m_cpu = 0, m_netDown = 0, m_netUp = 0, m_diskRead = 0, m_diskWrite = 0;
    double m_diskUsed = 0, m_diskTotal = 0;
    int m_cpuTemp = -1;
    bool m_netAvailable = false, m_diskIo = false;

    // graphics
    QString m_gpuDir;      // /sys/class/drm/cardN/device of an AMD card that reports its load
    QString m_nvidia;      // path of nvidia-smi, if there is one
    QProcess *m_smi = nullptr;
    bool m_gpuAvailable = false, m_gpuSearched = false;
    QString m_gpuName;
    double m_gpu = 0, m_vramUsed = 0, m_vramTotal = 0;

    // who is using the processor and memory
    QHash<qint64, quint64> m_ticksPrev;
    qint64 m_topPrevMs = 0;
    QVariantList m_topCpu, m_topRam;
};
