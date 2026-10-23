#include "ColorPicker.h"

#include "models/FileItem.h"

#include <QColor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QRandomGenerator>

namespace {
const QString Portal = QStringLiteral("org.freedesktop.portal.Desktop");
const QString RequestIface = QStringLiteral("org.freedesktop.portal.Request");
}

void ColorPicker::pick()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    disconnectRequest();
    // The portal answers on a Request object whose path we can predict; subscribe before calling.
    const QString token = QStringLiteral("notch%1").arg(QRandomGenerator::global()->generate());
    const QString sender = bus.baseService().mid(1).replace(u'.', u'_');
    m_requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect(Portal, m_requestPath, RequestIface, QStringLiteral("Response"), this,
                SLOT(onResponse(uint, QVariantMap)));

    QDBusMessage msg = QDBusMessage::createMethodCall(Portal, QStringLiteral("/org/freedesktop/portal/desktop"),
                                                      QStringLiteral("org.freedesktop.portal.Screenshot"),
                                                      QStringLiteral("PickColor"));
    msg << QString() << QVariantMap{{QStringLiteral("handle_token"), token}};
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            disconnectRequest();
            emit failed(reply.error().message());
        }
    });
}

void ColorPicker::onResponse(uint response, const QVariantMap &results)
{
    disconnectRequest();
    if (response != 0) // 1 = cancelled (Esc), 2 = other
        return;
    const QDBusArgument arg = results.value(QStringLiteral("color")).value<QDBusArgument>();
    double r = 0, g = 0, b = 0;
    arg.beginStructure();
    arg >> r >> g >> b;
    arg.endStructure();
    emit picked(QColor::fromRgbF(float(r), float(g), float(b)).name().toUpper());
}

void ColorPicker::disconnectRequest()
{
    if (m_requestPath.isEmpty())
        return;
    QDBusConnection::sessionBus().disconnect(Portal, m_requestPath, RequestIface, QStringLiteral("Response"), this,
                                             SLOT(onResponse(uint, QVariantMap)));
    m_requestPath.clear();
}
