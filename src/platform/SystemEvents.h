#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#ifdef NOTCH_HAVE_DBUS
#include <QDBusMessage>
#endif

#ifndef NOTCH_HAVE_DBUS
// Without D-Bus: on Windows the laptop battery is polled (charger plugged / unplugged, running low) and
// Bluetooth devices are watched (WinBluetooth). Elsewhere this is a stub that never fires. Same `alert`
// signal as the Linux class.
class SystemEvents : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY batteryChanged)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY batteryChanged)

public:
    explicit SystemEvents(QObject *parent = nullptr);
    int batteryLevel() const { return m_percent; }
    bool batteryCharging() const { return m_charging == 1; }

signals:
    void alert(const QString &kind, const QString &title, const QString &detail, const QString &icon, int level);
    void batteryChanged();

private:
    void poll();

    QTimer m_timer;
    int m_charging = -1; // -1 unknown, 0 on battery, 1 on mains
    int m_percent = -1;
};
#else
// Watches the system bus for moments worth a Dynamic-Island-style alert:
// charger plugged/unplugged (UPower DisplayDevice) and Bluetooth devices connecting or
// disconnecting (BlueZ Device1), and batteries running low: the laptop while discharging and
// Bluetooth devices that report Battery1. Nothing is polled; both services push PropertiesChanged.
class SystemEvents : public QObject
{
    Q_OBJECT
    // The laptop battery for the idle notch: % (-1 without a battery) and whether it's on the charger.
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY batteryChanged)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY batteryChanged)

public:
    explicit SystemEvents(QObject *parent = nullptr);
    int batteryLevel() const { return m_hasBattery ? qRound(m_percentage) : -1; }
    bool batteryCharging() const;

signals:
    void batteryChanged();
    // kind: "charging" | "battery" | "bluetooth" | "lowbattery". icon: freedesktop icon name,
    // level: battery % or -1.
    void alert(const QString &kind, const QString &title, const QString &detail, const QString &icon, int level);

private slots:
    void onPowerChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onBluezChanged(const QDBusMessage &message);

private:
    int m_powerState = -1; // UPower Device.State: 1 charging, 2 discharging, 4 full, 5 pending charge
    bool m_hasBattery = false;
    double m_percentage = -1;
    QHash<QString, int> m_deviceBattery; // BlueZ device path -> last known %
};
#endif
