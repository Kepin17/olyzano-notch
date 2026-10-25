#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>

#include <memory>

// Windows: paired Bluetooth devices connecting and disconnecting, and their batteries running low.
// Two WinRT DeviceWatchers (classic and LE association endpoints) report System.Devices.Aep.IsConnected;
// the battery comes from the device node in the same container (DEVPKEY_Bluetooth_Battery), which
// headsets that report one publish. Same `alert` signal as SystemEvents.
class WinBluetooth : public QObject
{
    Q_OBJECT

public:
    explicit WinBluetooth(QObject *parent = nullptr);
    ~WinBluetooth() override;

    // What a watcher saw, copied out of WinRT on its thread and handled on this object's.
    struct Reading {
        QString id;
        QString name;      // only from Added
        QString container; // "{guid}" or empty
        int major = -1;    // Bluetooth class of device, major class (4: audio)
        bool hasConnected = false;
        bool connected = false;
    };

signals:
    void alert(const QString &kind, const QString &title, const QString &detail, const QString &icon, int level);

private:
    struct Device {
        QString name, icon, container;
        bool connected = false;
        int battery = -1;
    };

    void added(const Reading &r);
    void updated(const Reading &r);
    void enumerated();
    void connectedChanged(const QString &id);
    void requestBattery(const QString &id, bool announce);
    void batteryRead(const QString &id, int percent, bool announce);
    void announce(const Device &d);
    void pollBatteries();

    struct Watchers;
    std::unique_ptr<Watchers> m_watchers;
    QHash<QString, Device> m_devices; // by endpoint id
    QHash<QString, qint64> m_lastAlert; // name + state -> ms: one headset is both a classic and an LE endpoint
    int m_enumerated = 0;             // watchers done with the devices already there (no alerts for those)
    QTimer m_batteryPoll;
};
