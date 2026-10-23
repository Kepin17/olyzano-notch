#include "SystemEvents.h"

#include "models/FileItem.h"
#include "platform/BatteryThreshold.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace {
const QString UPower = QStringLiteral("org.freedesktop.UPower");
const QString DisplayDevice = QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString BlueZ = QStringLiteral("org.bluez");

bool onMains(int state)
{
    return state == 1 || state == 4 || state == 5; // charging, fully charged, pending charge
}

QVariant busProperty(const QString &service, const QString &path, const QString &interface, const QString &name)
{
    QDBusInterface props(service, path, Properties, QDBusConnection::systemBus());
    const QDBusReply<QVariant> reply = props.call(QStringLiteral("Get"), interface, name);
    return reply.isValid() ? reply.value() : QVariant();
}
}

SystemEvents::SystemEvents(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        qCWarning(lcNotch) << "No system bus: charger and Bluetooth alerts disabled";
        return;
    }
    const QString device = QStringLiteral("org.freedesktop.UPower.Device");
    m_powerState = busProperty(UPower, DisplayDevice, device, QStringLiteral("State")).toInt();
    m_percentage = busProperty(UPower, DisplayDevice, device, QStringLiteral("Percentage")).toDouble();
    const bool power = bus.connect(UPower, DisplayDevice, Properties, QStringLiteral("PropertiesChanged"), this,
                                   SLOT(onPowerChanged(QString, QVariantMap, QStringList)));
    // Empty path: every BlueZ object, filtered to Device1 in the slot.
    const bool bluetooth = bus.connect(BlueZ, QString(), Properties, QStringLiteral("PropertiesChanged"), this,
                                       SLOT(onBluezChanged(QDBusMessage)));
    qCInfo(lcNotch) << "System alerts: power" << (power ? "on" : "unavailable") << "state" << m_powerState
                    << m_percentage << "% | bluetooth" << (bluetooth ? "on" : "unavailable");
}

void SystemEvents::onPowerChanged(const QString &, const QVariantMap &changed, const QStringList &)
{
    if (changed.contains(QStringLiteral("Percentage"))) {
        const double now = changed.value(QStringLiteral("Percentage")).toDouble();
        const int level = crossedLowBattery(qRound(m_percentage), qRound(now));
        m_percentage = now;
        if (level && !onMains(m_powerState))
            emit alert(QStringLiteral("lowbattery"), tr("Low battery"), tr("%1% left").arg(qRound(now)),
                       QStringLiteral("battery-low"), qRound(now));
    }
    if (!changed.contains(QStringLiteral("State")))
        return;
    const int state = changed.value(QStringLiteral("State")).toInt();
    const bool was = onMains(m_powerState), now = onMains(state);
    m_powerState = state;
    if (was == now)
        return; // e.g. charging -> fully charged: still plugged in
    const int level = qRound(m_percentage);
    if (now)
        emit alert(QStringLiteral("charging"), tr("Charging"), QStringLiteral("%1%").arg(level),
                   QStringLiteral("battery-charging"), level);
    else
        emit alert(QStringLiteral("battery"), tr("On battery"), QStringLiteral("%1%").arg(level),
                   QStringLiteral("battery"), level);
}

void SystemEvents::onBluezChanged(const QDBusMessage &message)
{
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2)
        return;
    const QString interface = args.at(0).toString();
    const QVariantMap changed = qdbus_cast<QVariantMap>(args.at(1));
    const QString device = QStringLiteral("org.bluez.Device1");

    if (interface == QLatin1String("org.bluez.Battery1") && changed.contains(QStringLiteral("Percentage"))) {
        const int now = changed.value(QStringLiteral("Percentage")).toInt();
        const int before = m_deviceBattery.value(message.path(), -1);
        m_deviceBattery.insert(message.path(), now);
        if (crossedLowBattery(before, now)) {
            const QString name = busProperty(BlueZ, message.path(), device, QStringLiteral("Alias")).toString();
            QString icon = busProperty(BlueZ, message.path(), device, QStringLiteral("Icon")).toString();
            emit alert(QStringLiteral("lowbattery"), name.isEmpty() ? tr("Bluetooth device") : name,
                       tr("Battery %1%").arg(now), icon.isEmpty() ? QStringLiteral("preferences-system-bluetooth") : icon, now);
        }
        return;
    }
    if (interface != device || !changed.contains(QStringLiteral("Connected")))
        return;
    const bool connected = changed.value(QStringLiteral("Connected")).toBool();
    const QString path = message.path();
    const QString name = busProperty(BlueZ, path, device, QStringLiteral("Alias")).toString();
    QString icon = busProperty(BlueZ, path, device, QStringLiteral("Icon")).toString();
    if (icon.isEmpty())
        icon = QStringLiteral("preferences-system-bluetooth");
    const QVariant battery = busProperty(BlueZ, path, QStringLiteral("org.bluez.Battery1"), QStringLiteral("Percentage"));
    const int level = connected && battery.isValid() ? battery.toInt() : -1;
    if (connected && level >= 0)
        m_deviceBattery.insert(path, level); // a warning needs a drop from a known level
    else
        m_deviceBattery.remove(path);
    emit alert(QStringLiteral("bluetooth"), name.isEmpty() ? tr("Bluetooth device") : name,
               connected ? tr("Connected") : tr("Disconnected"), icon, level);
}
