#include "KdeConnect.h"

#include "models/FileItem.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>

namespace {
const QString Service = QStringLiteral("org.kde.kdeconnect");
const QString DevicePath = QStringLiteral("/modules/kdeconnect/devices/");
const QString DeviceIface = QStringLiteral("org.kde.kdeconnect.device");
const QString ShareIface = QStringLiteral("org.kde.kdeconnect.device.share");
const QString TelephonyIface = QStringLiteral("org.kde.kdeconnect.device.telephony");
const QString NotificationsIface = QStringLiteral("org.kde.kdeconnect.device.notifications");
const QString NotificationIface = QStringLiteral("org.kde.kdeconnect.device.notifications.notification");
const QString BatteryIface = QStringLiteral("org.kde.kdeconnect.device.battery");

// /modules/kdeconnect/devices/<id>/<plugin…> -> /modules/kdeconnect/devices/<id>
QString devicePathOf(const QString &path)
{
    return path.section(u'/', 0, 4);
}

QString deviceProperty(const QString &path, const QString &name)
{
    QDBusInterface props(Service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::sessionBus());
    const QDBusReply<QDBusVariant> reply = props.call(QStringLiteral("Get"), DeviceIface, name);
    return reply.isValid() ? reply.value().variant().toString() : QString();
}
}

KdeConnect::KdeConnect(QObject *parent)
    : QObject(parent)
{
    // Empty path: the share plugin of every device. Works even if KDE Connect starts later.
    const bool ok = QDBusConnection::sessionBus().connect(Service, QString(), ShareIface, QStringLiteral("shareReceived"),
                                                         this, SLOT(onShareReceived(QDBusMessage)));
    qCInfo(lcNotch) << "KDE Connect:" << (ok ? "listening for shared files" : "unavailable");
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(Service, QString(), TelephonyIface, QStringLiteral("callReceived"), this, SLOT(onCall(QDBusMessage)));
    bus.connect(Service, QString(), NotificationsIface, QStringLiteral("notificationPosted"), this,
                SLOT(onNotificationPosted(QDBusMessage)));
    bus.connect(Service, QString(), BatteryIface, QStringLiteral("refreshed"), this, SLOT(onBattery(QDBusMessage)));
}

void KdeConnect::onCall(const QDBusMessage &message)
{
    const QVariantList args = message.arguments(); // event, number, contact name
    if (args.size() < 3)
        return;
    const QString contact = args.at(2).toString();
    emit phoneCall(args.at(0).toString(), contact.isEmpty() ? args.at(1).toString() : contact,
                   deviceProperty(devicePathOf(message.path()), QStringLiteral("name")));
}

void KdeConnect::onNotificationPosted(const QDBusMessage &message)
{
    if (message.arguments().isEmpty())
        return;
    // The notification is its own object next to the plugin: read it without blocking.
    const QString path = message.path() + u'/' + message.arguments().constFirst().toString();
    QDBusMessage get = QDBusMessage::createMethodCall(Service, path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("GetAll"));
    get.setArguments({NotificationIface});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(get), this);
    const QString device = devicePathOf(message.path());
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, device] {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError())
            return;
        const QVariantMap n = reply.value();
        if (n.value(QStringLiteral("silent")).toBool())
            return;
        emit phoneNotification(n.value(QStringLiteral("appName")).toString(), n.value(QStringLiteral("title")).toString(),
                               n.value(QStringLiteral("text")).toString(), deviceProperty(device, QStringLiteral("name")));
    });
}

void KdeConnect::onBattery(const QDBusMessage &message)
{
    const QVariantList args = message.arguments(); // charging, charge
    if (args.size() < 2)
        return;
    emit phoneBattery(args.at(1).toInt(), args.at(0).toBool(), deviceProperty(devicePathOf(message.path()), QStringLiteral("name")));
}

QVariantList KdeConnect::devices() const
{
    QDBusInterface daemon(Service, QStringLiteral("/modules/kdeconnect"), QStringLiteral("org.kde.kdeconnect.daemon"),
                          QDBusConnection::sessionBus());
    const QDBusReply<QStringList> ids = daemon.call(QStringLiteral("devices"), true, true); // reachable, paired
    QVariantList out;
    if (!ids.isValid())
        return out;
    for (const QString &id : ids.value()) {
        const QString path = DevicePath + id;
        out << QVariantMap{{QStringLiteral("id"), id},
                           {QStringLiteral("name"), deviceProperty(path, QStringLiteral("name"))},
                           {QStringLiteral("type"), deviceProperty(path, QStringLiteral("type"))}};
    }
    return out;
}

void KdeConnect::send(const QString &deviceId, const QString &deviceName, const QStringList &urls)
{
    if (urls.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(Service, DevicePath + deviceId + QStringLiteral("/share"), ShareIface,
                                                      QStringLiteral("shareUrls"));
    msg.setArguments({urls});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, deviceName, count = urls.size()] {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        emit sent(count, deviceName, reply.isError() ? reply.error().message() : QString());
    });
}

void KdeConnect::onShareReceived(const QDBusMessage &message)
{
    if (message.arguments().isEmpty())
        return;
    QString path = message.path(); // /modules/kdeconnect/devices/<id>/share
    path.chop(QStringLiteral("/share").size());
    emit received(message.arguments().constFirst().toString(), deviceProperty(path, QStringLiteral("name")));
}
