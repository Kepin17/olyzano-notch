#include "platform/AppMenu.h"

#include "platform/DbusMenuModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>

#ifdef NOTCH_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusVariant>
#include <QXmlStreamReader>

namespace {
const QString MenuIface = QStringLiteral("com.canonical.dbusmenu");

QVariantMap readNode(const QDBusArgument &arg)
{
    QVariantMap node;
    int id = 0;
    QVariantMap props;
    arg.beginStructure();
    arg >> id >> props;
    QVariantList kids;
    arg.beginArray();
    while (!arg.atEnd()) {
        QDBusVariant v;
        arg >> v;
        kids << readNode(v.variant().value<QDBusArgument>());
    }
    arg.endArray();
    arg.endStructure();
    const QVariant shortcut = props.value(QStringLiteral("shortcut"));
    if (shortcut.canConvert<QDBusArgument>()) {
        QVariantList ways;
        for (const QStringList &w : qdbus_cast<QList<QStringList>>(shortcut.value<QDBusArgument>()))
            ways << QVariant(w);
        props[QStringLiteral("shortcut")] = ways;
    }
    node[QStringLiteral("id")] = id;
    node[QStringLiteral("props")] = props;
    node[QStringLiteral("children")] = kids;
    return node;
}

// The objects under `path` that speak dbusmenu (a bounded walk: apps have few objects, and some answer slowly).
QString walk(const QDBusConnection &bus, const QString &service, const QString &path, int depth, int &budget)
{
    if (depth > 5 || --budget < 0)
        return {};
    // some apps (Chrome) publish the menu object without introspection data, so ask for its Version instead
    QDBusMessage probe = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    probe.setArguments({MenuIface, QStringLiteral("Version")});
    if (bus.call(probe, QDBus::Block, 400).type() == QDBusMessage::ReplyMessage)
        return path;
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
    const QDBusMessage r = bus.call(m, QDBus::Block, 400);
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty())
        return {};
    QXmlStreamReader xml(r.arguments().first().toString());
    QStringList kids;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("node") && xml.attributes().hasAttribute(QLatin1String("name")))
            kids << xml.attributes().value(QLatin1String("name")).toString();
    }
    for (const QString &k : std::as_const(kids))
        if (const QString found = walk(bus, service, (path == QLatin1String("/") ? QString() : path) + u'/' + k, depth + 1, budget); !found.isEmpty())
            return found;
    return {};
}
}
#endif

AppMenu::AppMenu(QObject *parent)
    : QObject(parent)
{
#ifdef NOTCH_HAVE_DBUS
    qDBusRegisterMetaType<QList<QStringList>>();
#endif
    m_reload.setSingleShot(true);
    m_reload.setInterval(250);
    connect(&m_reload, &QTimer::timeout, this, [this] { if (m_pid) start(m_pid); });
    connect(&m_watch, &QFutureWatcher<Result>::finished, this, &AppMenu::done);
}

AppMenu::Found AppMenu::discover(qint64 pid)
{
#ifdef NOTCH_HAVE_DBUS
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface() || pid <= 0)
        return {};
    for (const QString &name : bus.interface()->registeredServiceNames().value()) {
        if (!name.startsWith(u':'))
            continue;
        const QDBusReply<uint> owner = bus.interface()->servicePid(name);
        if (!owner.isValid() || qint64(owner.value()) != pid)
            continue;
        int budget = 60;
        if (const QString path = walk(bus, name, QStringLiteral("/"), 0, budget); !path.isEmpty())
            return {name, path};
    }
#else
    Q_UNUSED(pid)
#endif
    return {};
}

QVariantList AppMenu::readMenus(const Found &where)
{
#ifdef NOTCH_HAVE_DBUS
    if (!where.ok())
        return {};
    QDBusMessage m = QDBusMessage::createMethodCall(where.service, where.path, MenuIface, QStringLiteral("GetLayout"));
    m.setArguments({0, -1, QStringList()});
    const QDBusMessage r = QDBusConnection::sessionBus().call(m, QDBus::Block, 800);
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().size() < 2 || !r.arguments().at(1).canConvert<QDBusArgument>())
        return {};
    return DbusMenuModel::menuBar(readNode(r.arguments().at(1).value<QDBusArgument>()));
#else
    Q_UNUSED(where)
    return {};
#endif
}

AppMenu::Result AppMenu::load(qint64 pid, const Found &known)
{
    Result r;
    r.pid = pid;
    r.found = known.ok() ? known : discover(pid);
    r.menus = readMenus(r.found);
    return r;
}

void AppMenu::setPid(qint64 pid)
{
    if (pid == m_pid)
        return;
    m_pid = pid;
    if (pid <= 0 || (m_known.contains(pid) && !m_known.value(pid).ok())) {
        if (!m_menus.isEmpty()) {
            m_menus.clear();
            emit changed();
        }
        m_found = {};
        return;
    }
    start(pid);
}

void AppMenu::start(qint64 pid)
{
    if (m_watch.isRunning()) { // one at a time; the newest wish is picked up when this ends
        m_asked = pid;
        return;
    }
    m_asked = 0;
    m_watch.setFuture(QtConcurrent::run([pid, known = m_known.value(pid)] { return load(pid, known); }));
}

void AppMenu::done()
{
    const Result r = m_watch.result();
    m_known.insert(r.pid, r.found);
    if (r.pid == m_pid) {
        const bool moved = !(r.found.service == m_found.service && r.found.path == m_found.path);
        m_found = r.found;
        if (m_menus != r.menus) {
            m_menus = r.menus;
            emit changed();
        }
        if (moved)
            listen();
    }
    if (m_asked && m_asked != r.pid)
        start(m_asked);
}

void AppMenu::listen()
{
#ifdef NOTCH_HAVE_DBUS
    if (m_found.ok())
        QDBusConnection::sessionBus().connect(m_found.service, m_found.path, MenuIface, QStringLiteral("LayoutUpdated"), this, SLOT(onLayoutUpdated(uint, int)));
#endif
}

void AppMenu::clickAt(const QString &service, const QString &path, int id)
{
#ifdef NOTCH_HAVE_DBUS
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, MenuIface, QStringLiteral("Event"));
    m.setArguments({id, QStringLiteral("clicked"), QVariant::fromValue(QDBusVariant(0)), uint(QDateTime::currentSecsSinceEpoch())});
    QDBusConnection::sessionBus().asyncCall(m);
#else
    Q_UNUSED(service) Q_UNUSED(path) Q_UNUSED(id)
#endif
}

void AppMenu::aboutToShowAt(const QString &service, const QString &path, int id)
{
#ifdef NOTCH_HAVE_DBUS
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, MenuIface, QStringLiteral("AboutToShow"));
    m.setArguments({id});
    QDBusConnection::sessionBus().asyncCall(m);
#else
    Q_UNUSED(service) Q_UNUSED(path) Q_UNUSED(id)
#endif
}

void AppMenu::trigger(int id)
{
    if (m_found.ok())
        clickAt(m_found.service, m_found.path, id);
}

void AppMenu::aboutToShow(int id)
{
    if (!m_found.ok())
        return;
    aboutToShowAt(m_found.service, m_found.path, id);
    m_reload.start(); // some apps fill a menu in when it is asked to show
}

QByteArray AppMenu::dump(qint64 pid)
{
    const Result r = load(pid, {});
    QJsonObject o{{QStringLiteral("pid"), pid}, {QStringLiteral("service"), r.found.service}, {QStringLiteral("path"), r.found.path}};
    o[QStringLiteral("menus")] = QJsonArray::fromVariantList(r.menus);
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}
