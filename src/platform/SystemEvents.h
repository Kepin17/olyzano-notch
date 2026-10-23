#pragma once

#include <QDBusMessage>
#include <QHash>
#include <QObject>

// Watches the system bus for moments worth a Dynamic-Island-style alert:
// charger plugged/unplugged (UPower DisplayDevice) and Bluetooth devices connecting or
// disconnecting (BlueZ Device1), and batteries running low: the laptop while discharging and
// Bluetooth devices that report Battery1. Nothing is polled; both services push PropertiesChanged.
class SystemEvents : public QObject
{
    Q_OBJECT

public:
    explicit SystemEvents(QObject *parent = nullptr);

signals:
    // kind: "charging" | "battery" | "bluetooth" | "lowbattery". icon: freedesktop icon name,
    // level: battery % or -1.
    void alert(const QString &kind, const QString &title, const QString &detail, const QString &icon, int level);

private slots:
    void onPowerChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onBluezChanged(const QDBusMessage &message);

private:
    int m_powerState = -1; // UPower Device.State: 1 charging, 2 discharging, 4 full, 5 pending charge
    double m_percentage = -1;
    QHash<QString, int> m_deviceBattery; // BlueZ device path -> last known %
};
