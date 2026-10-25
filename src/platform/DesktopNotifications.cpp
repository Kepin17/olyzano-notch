#include "platform/DesktopNotifications.h"

#include "models/FileItem.h"

#include <QDateTime>
#include <QStandardPaths>

namespace {
QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/notifications.conf");
}
}

#if defined(NOTCH_HAVE_DBUS) && defined(NOTCH_HAVE_LIBDBUS)

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QPointer>
#include <QThread>

#include <dbus/dbus.h>

#include <atomic>

namespace {
const char *Match = "interface='org.freedesktop.Notifications',member='Notify'";

QString nextString(DBusMessageIter *it)
{
    QString s;
    if (dbus_message_iter_get_arg_type(it) == DBUS_TYPE_STRING) {
        const char *v = nullptr;
        dbus_message_iter_get_basic(it, &v);
        s = QString::fromUtf8(v);
    }
    dbus_message_iter_next(it);
    return s;
}

// hints["desktop-entry"], when the app gave one: a better icon than app_icon, which is often a path or empty.
QString desktopEntry(DBusMessageIter *hints)
{
    if (dbus_message_iter_get_arg_type(hints) != DBUS_TYPE_ARRAY)
        return {};
    DBusMessageIter dict;
    dbus_message_iter_recurse(hints, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, value;
        dbus_message_iter_recurse(&dict, &entry);
        const QString key = nextString(&entry);
        if (key == QLatin1String("desktop-entry") && dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
            dbus_message_iter_recurse(&entry, &value);
            return nextString(&value);
        }
        dbus_message_iter_next(&dict);
    }
    return {};
}
}

// A private libdbus connection on a thread of its own: a monitor must never answer what it sees, and QtDBus
// would (it replies to every method call addressed to it), so this stays out of QtDBus entirely.
struct DesktopNotifications::Listener : QThread {
    QPointer<DesktopNotifications> owner;
    std::atomic<bool> quit{false};

    static DBusHandlerResult filter(DBusConnection *, DBusMessage *message, void *data)
    {
        auto *self = static_cast<Listener *>(data);
        if (dbus_message_is_method_call(message, "org.freedesktop.Notifications", "Notify")
            && dbus_message_has_signature(message, "susssasa{sv}i")) {
            DBusMessageIter it;
            dbus_message_iter_init(message, &it);
            const QString app = nextString(&it);
            dbus_message_iter_next(&it); // replaces_id
            const QString icon = nextString(&it);
            const QString summary = nextString(&it);
            dbus_message_iter_next(&it); // body: never read
            dbus_message_iter_next(&it); // actions
            const QString entry = desktopEntry(&it);
            const QPointer<DesktopNotifications> owner = self->owner;
            QMetaObject::invokeMethod(qApp, [owner, app, summary, icon = entry.isEmpty() ? icon : entry] {
                if (owner)
                    owner->received(app, summary, icon);
            }, Qt::QueuedConnection);
        }
        return DBUS_HANDLER_RESULT_HANDLED; // eat everything: monitors must not reply
    }

    void run() override
    {
        DBusError error;
        dbus_error_init(&error);
        DBusConnection *c = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
        if (!c) {
            qCWarning(lcNotch) << "Desktop notifications: no session bus:" << error.message;
            dbus_error_free(&error);
            return;
        }
        dbus_connection_set_exit_on_disconnect(c, FALSE);
        bool watching = false;
        DBusMessage *call = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_MONITORING, "BecomeMonitor");
        const char *matches[] = {Match};
        const char **list = matches;
        dbus_uint32_t flags = 0;
        if (call && dbus_message_append_args(call, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &list, 1, DBUS_TYPE_UINT32, &flags,
                                             DBUS_TYPE_INVALID)) {
            if (DBusMessage *reply = dbus_connection_send_with_reply_and_block(c, call, 3000, &error)) {
                dbus_message_unref(reply);
                watching = true;
            } else {
                qCWarning(lcNotch) << "Desktop notifications: BecomeMonitor refused:" << error.message;
                dbus_error_free(&error);
            }
        }
        if (call)
            dbus_message_unref(call);
        if (watching) {
            const QPointer<DesktopNotifications> o = owner;
            QMetaObject::invokeMethod(qApp, [o] { if (o) o->setAvailable(true); }, Qt::QueuedConnection);
            dbus_connection_add_filter(c, &Listener::filter, this, nullptr);
            while (!quit && dbus_connection_read_write_dispatch(c, 300)) {
            }
            dbus_connection_remove_filter(c, &Listener::filter, this);
        }
        dbus_connection_close(c);
        dbus_connection_unref(c);
    }
};

void DesktopNotifications::start()
{
    if (m_listener)
        return;
    m_listener = std::make_unique<Listener>();
    m_listener->owner = this;
    m_listener->start();
}

void DesktopNotifications::stop()
{
    if (!m_listener)
        return;
    m_listener->quit = true;
    m_listener->wait();
    m_listener.reset();
    setAvailable(false);
}

// Plasma's "Do not disturb" sets Inhibited on the notification server.
bool DesktopNotifications::doNotDisturb() const
{
    QDBusMessage get = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                      QStringLiteral("/org/freedesktop/Notifications"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    get.setArguments({QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Inhibited")});
    const QDBusReply<QDBusVariant> r = QDBusConnection::sessionBus().call(get, QDBus::Block, 300);
    return r.isValid() && r.value().variant().toBool();
}

#elif defined(Q_OS_WIN)
// DesktopNotifications_win.cpp
#else

struct DesktopNotifications::Listener {};
void DesktopNotifications::start() {}
void DesktopNotifications::stop() {}
bool DesktopNotifications::doNotDisturb() const { return false; }

#endif

DesktopNotifications::DesktopNotifications(QObject *parent)
    : QObject(parent)
    , m_settings(configPath(), QSettings::IniFormat)
{
    if (enabled())
        start();
}

DesktopNotifications::~DesktopNotifications()
{
    stop();
}

void DesktopNotifications::setEnabled(bool on)
{
    if (on == enabled())
        return;
    m_settings.setValue(QStringLiteral("enabled"), on);
    on ? start() : stop();
    emit settingsChanged();
}

void DesktopNotifications::ignore(const QString &app)
{
    QStringList list = ignored();
    if (app.isEmpty() || list.contains(app, Qt::CaseInsensitive))
        return;
    list << app;
    m_settings.setValue(QStringLiteral("ignored"), list);
    emit settingsChanged();
}

void DesktopNotifications::unignore(const QString &app)
{
    QStringList list = ignored();
    list.removeIf([&](const QString &a) { return a.compare(app, Qt::CaseInsensitive) == 0; });
    m_settings.setValue(QStringLiteral("ignored"), list);
    emit settingsChanged();
}

void DesktopNotifications::setAvailable(bool available)
{
    if (available == m_available)
        return;
    m_available = available;
    emit availableChanged();
}

void DesktopNotifications::received(const QString &app, const QString &title, const QString &icon)
{
    const QString name = app.trimmed();
    const QString lower = name.toLower();
    QStringList recent = recentApps();
    if (!name.isEmpty() && lower != QLatin1String("notch") && !lower.startsWith(QLatin1String("kdeconnect"))
        && !lower.startsWith(QLatin1String("kde connect")) && recent.value(0) != name) {
        recent.removeAll(name);
        recent.prepend(name);
        m_settings.setValue(QStringLiteral("recent"), recent.mid(0, 10));
        emit settingsChanged();
    }
    if (!enabled() || !m_filter.accept(app, title, ignored(), QDateTime::currentMSecsSinceEpoch()) || doNotDisturb())
        return;
    emit notification(app, title, icon);
}
