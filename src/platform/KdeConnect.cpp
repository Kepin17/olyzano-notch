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
