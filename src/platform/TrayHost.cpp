#include "platform/TrayHost.h"

#include "platform/AppMenu.h"

#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>

#ifdef NOTCH_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFileInfo>
#include <unistd.h>

namespace {
struct IconPixmap {
    int width = 0, height = 0;
    QByteArray data; // ARGB32, network byte order
};
using IconPixmaps = QList<IconPixmap>;

const QString Watcher = QStringLiteral("org.kde.StatusNotifierWatcher");
const QString WatcherPath = QStringLiteral("/StatusNotifierWatcher");
const QString ItemIface = QStringLiteral("org.kde.StatusNotifierItem");
const QString Props = QStringLiteral("org.freedesktop.DBus.Properties");

QImage largest(const IconPixmaps &all)
{
    const IconPixmap *best = nullptr;
    for (const IconPixmap &p : all)
        if (p.width > 0 && p.height > 0 && p.data.size() >= p.width * p.height * 4 && (!best || p.width > best->width))
            best = &p;
    if (!best)
        return {};
    QImage img(best->width, best->height, QImage::Format_ARGB32);
    const uchar *src = reinterpret_cast<const uchar *>(best->data.constData());
    for (int y = 0; y < best->height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < best->width; ++x, src += 4)
            line[x] = qRgba(src[1], src[2], src[3], src[0]);
    }
    return img;
}
}

QDBusArgument &operator<<(QDBusArgument &a, const IconPixmap &p)
{
    a.beginStructure();
    a << p.width << p.height << p.data;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, IconPixmap &p)
{
    a.beginStructure();
    a >> p.width >> p.height >> p.data;
    a.endStructure();
    return a;
}
Q_DECLARE_METATYPE(IconPixmap)
Q_DECLARE_METATYPE(IconPixmaps)
#endif

TrayHost::TrayHost(QObject *parent)
    : QObject(parent)
{
#ifdef NOTCH_HAVE_DBUS
    qDBusRegisterMetaType<IconPixmap>();
    qDBusRegisterMetaType<IconPixmaps>();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface() || !bus.interface()->isServiceRegistered(Watcher))
        return;
    const QString host = QStringLiteral("org.kde.StatusNotifierHost-notch-%1").arg(getpid());
    bus.registerService(host);
    QDBusMessage reg = QDBusMessage::createMethodCall(Watcher, WatcherPath, Watcher, QStringLiteral("RegisterStatusNotifierHost"));
    reg.setArguments({host});
    bus.asyncCall(reg);
    bus.connect(Watcher, WatcherPath, Watcher, QStringLiteral("StatusNotifierItemRegistered"), this, SLOT(onRegistered(QString)));
    bus.connect(Watcher, WatcherPath, Watcher, QStringLiteral("StatusNotifierItemUnregistered"), this, SLOT(onUnregistered(QString)));
    for (const char *sig : {"NewIcon", "NewAttentionIcon", "NewStatus", "NewTitle"})
        bus.connect(QString(), QString(), ItemIface, QLatin1String(sig), this, SLOT(onItemSignal()));

    QDBusMessage list = QDBusMessage::createMethodCall(Watcher, WatcherPath, Props, QStringLiteral("Get"));
    list.setArguments({Watcher, QStringLiteral("RegisteredStatusNotifierItems")});
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(list), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        const QDBusPendingReply<QDBusVariant> r = *w;
        if (!r.isError())
            for (const QString &e : r.value().variant().toStringList())
                add(e);
        w->deleteLater();
    });
#endif
}

const TrayHost::Item *TrayHost::find(const QString &key) const
{
    const auto it = m_items.constFind(key);
    return it == m_items.constEnd() ? nullptr : &it.value();
}

void TrayHost::add(const QString &entry)
{
#ifdef NOTCH_HAVE_DBUS
    for (const Item &i : std::as_const(m_items))
        if (i.entry == entry)
            return;
    Item it;
    it.entry = entry;
    const int slash = entry.indexOf(u'/');
    it.service = slash < 0 ? entry : entry.left(slash);
    it.path = slash < 0 ? QStringLiteral("/StatusNotifierItem") : entry.mid(slash);
    it.owner = it.service.startsWith(u':') ? it.service : QDBusConnection::sessionBus().interface()->serviceOwner(it.service).value();
    const QString key = QString::number(m_next++);
    m_items.insert(key, it);
    m_order << key;
    read(key);
#else
    Q_UNUSED(entry)
#endif
}

void TrayHost::onUnregistered(const QString &entry)
{
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
        if (it->entry == entry) {
            m_order.removeAll(it.key());
            m_items.erase(it);
            emit changed();
            return;
        }
}

void TrayHost::onItemSignal()
{
#ifdef NOTCH_HAVE_DBUS
    const QString from = message().service();
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
        if (it->owner == from) {
            read(it.key());
            return;
        }
#endif
}

void TrayHost::read(const QString &key)
{
#ifdef NOTCH_HAVE_DBUS
    const Item *i = find(key);
    if (!i)
        return;
    QDBusMessage m = QDBusMessage::createMethodCall(i->service, i->path, Props, QStringLiteral("GetAll"));
    m.setArguments({ItemIface});
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, key, w] {
        w->deleteLater();
        const QDBusPendingReply<QVariantMap> r = *w;
        auto it = m_items.find(key);
        if (r.isError() || it == m_items.end())
            return;
        const QVariantMap p = r.value();
        it->id = p.value(QStringLiteral("Id")).toString();
        it->title = p.value(QStringLiteral("Title")).toString();
        it->status = p.value(QStringLiteral("Status")).toString();
        it->themePath = p.value(QStringLiteral("IconThemePath")).toString();
        it->isMenu = p.value(QStringLiteral("ItemIsMenu")).toBool();
        it->menuPath = p.value(QStringLiteral("Menu")).value<QDBusObjectPath>().path();
        const bool attention = it->status == QLatin1String("NeedsAttention");
        it->iconName = p.value(attention && !p.value(QStringLiteral("AttentionIconName")).toString().isEmpty() ? QStringLiteral("AttentionIconName") : QStringLiteral("IconName")).toString();
        it->image = {};
        const QVariant px = p.value(QStringLiteral("IconPixmap"));
        if (it->iconName.isEmpty() && px.canConvert<QDBusArgument>())
            it->image = largest(qdbus_cast<IconPixmaps>(px.value<QDBusArgument>()));
        ++it->rev;
        emit changed();
    });
#else
    Q_UNUSED(key)
#endif
}

QVariantList TrayHost::items() const
{
    QVariantList out;
    for (const QString &key : m_order) {
        const Item *i = find(key);
        if (!i || i->id.isEmpty() || i->status == QLatin1String("Passive"))
            continue;
        out << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("id"), i->id}, {QStringLiteral("title"), i->title.isEmpty() ? i->id : i->title},
                           {QStringLiteral("status"), i->status}, {QStringLiteral("isMenu"), i->isMenu}, {QStringLiteral("hasMenu"), !i->menuPath.isEmpty()},
                           {QStringLiteral("icon"), QStringLiteral("image://tray/%1/%2").arg(key).arg(i->rev)}};
    }
    return out;
}

QPixmap TrayHost::pixmap(const QString &key, const QSize &size) const
{
    const Item *i = find(key);
    const QSize want = size.isValid() ? size : QSize(32, 32);
    if (!i)
        return {};
    if (!i->image.isNull())
        return QPixmap::fromImage(i->image.scaled(want, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    QIcon icon;
    if (QFileInfo(i->iconName).isAbsolute())
        icon = QIcon(i->iconName);
    else {
        icon = QIcon::fromTheme(i->iconName);
#ifdef NOTCH_HAVE_DBUS
        if (icon.isNull() && !i->themePath.isEmpty()) { // the app's own icons: <path>/<name>.png or <path>/hicolor/...
            for (const QString &ext : {QStringLiteral(".png"), QStringLiteral(".svg")})
                if (QFileInfo::exists(i->themePath + u'/' + i->iconName + ext))
                    icon = QIcon(i->themePath + u'/' + i->iconName + ext);
        }
#endif
    }
    return icon.pixmap(want);
}

#ifdef NOTCH_HAVE_DBUS
static void callItem(const TrayHost *, const QString &service, const QString &path, const QString &method, const QVariantList &args)
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, ItemIface, method);
    m.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(m);
}
#endif

void TrayHost::activate(const QString &key, int x, int y)
{
#ifdef NOTCH_HAVE_DBUS
    const Item *i = find(key);
    if (!i)
        return;
    QDBusMessage m = QDBusMessage::createMethodCall(i->service, i->path, ItemIface, QStringLiteral("Activate"));
    m.setArguments({x, y});
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, key, x, y, w] { // an app that has no Activate gets its context menu
        if (w->isError())
            if (const Item *i = find(key))
                callItem(this, i->service, i->path, QStringLiteral("ContextMenu"), {x, y});
        w->deleteLater();
    });
#else
    Q_UNUSED(key) Q_UNUSED(x) Q_UNUSED(y)
#endif
}

void TrayHost::secondary(const QString &key, int x, int y)
{
#ifdef NOTCH_HAVE_DBUS
    if (const Item *i = find(key))
        callItem(this, i->service, i->path, QStringLiteral("SecondaryActivate"), {x, y});
#else
    Q_UNUSED(key) Q_UNUSED(x) Q_UNUSED(y)
#endif
}

QVariantList TrayHost::menu(const QString &key)
{
    const Item *i = find(key);
    m_menuKey = key;
    if (!i || i->menuPath.isEmpty() || i->menuPath == QLatin1String("/"))
        return {};
    AppMenu::aboutToShowAt(i->service, i->menuPath, 0);
    return AppMenu::layoutAt(i->service, i->menuPath);
}

void TrayHost::trigger(int id)
{
    if (const Item *i = find(m_menuKey))
        AppMenu::clickAt(i->service, i->menuPath, id);
}

void TrayHost::aboutToShow(int id)
{
    if (const Item *i = find(m_menuKey))
        AppMenu::aboutToShowAt(i->service, i->menuPath, id);
}

QByteArray TrayHost::dump() const
{
    QJsonArray list;
    for (const QString &key : m_order)
        if (const Item *i = find(key))
            list << QJsonObject{{QStringLiteral("entry"), i->entry}, {QStringLiteral("id"), i->id}, {QStringLiteral("title"), i->title}, {QStringLiteral("status"), i->status},
                                {QStringLiteral("icon"), i->iconName}, {QStringLiteral("pixmap"), QStringLiteral("%1x%2").arg(i->image.width()).arg(i->image.height())},
                                {QStringLiteral("menu"), i->menuPath}, {QStringLiteral("isMenu"), i->isMenu}};
    return QJsonDocument(list).toJson(QJsonDocument::Indented);
}
