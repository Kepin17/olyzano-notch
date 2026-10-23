#include "MediaPlayers.h"

#include "models/FileItem.h"
#include "platform/PlayerSelection.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>

namespace {
const QString Prefix = QStringLiteral("org.mpris.MediaPlayer2.");
const QString Path = QStringLiteral("/org/mpris/MediaPlayer2");
const QString Root = QStringLiteral("org.mpris.MediaPlayer2");
const QString PlayerIface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");

QVariantMap getAll(const QString &service, const QString &interface)
{
    QDBusInterface props(service, Path, Properties, QDBusConnection::sessionBus());
    const QDBusReply<QVariantMap> reply = props.call(QStringLiteral("GetAll"), interface);
    return reply.isValid() ? reply.value() : QVariantMap();
}

// Nested a{sv} values arrive as QDBusArgument; plain values as themselves.
QVariantMap asMap(const QVariant &v)
{
    return v.canConvert<QDBusArgument>() ? qdbus_cast<QVariantMap>(v.value<QDBusArgument>()) : v.toMap();
}
}

MediaPlayers::MediaPlayers(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(lcNotch) << "No session bus: music player disabled";
        return;
    }
    bus.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"), this,
                SLOT(onNameOwnerChanged(QString, QString, QString)));
    const QStringList names = bus.interface()->registeredServiceNames().value();
    for (const QString &name : names)
        if (name.startsWith(Prefix))
            addPlayer(name, bus.interface()->serviceOwner(name).value());
    select();
    m_ready = true;
    qCInfo(lcNotch) << "Music players found:" << m_players.size();
}

void MediaPlayers::onNameOwnerChanged(const QString &name, const QString &, const QString &newOwner)
{
    if (!name.startsWith(Prefix))
        return;
    m_players.remove(name);
    if (!newOwner.isEmpty())
        addPlayer(name, newOwner);
    select();
}

void MediaPlayers::addPlayer(const QString &service, const QString &owner)
{
    Player p;
    p.service = service;
    p.owner = owner;
    p.identity = getAll(service, Root).value(QStringLiteral("Identity")).toString();
    refresh(p);
    m_players.insert(service, p);
    QDBusConnection::sessionBus().connect(service, Path, Properties, QStringLiteral("PropertiesChanged"), this,
                                          SLOT(onPropertiesChanged(QDBusMessage)));
}

void MediaPlayers::refresh(Player &p)
{
    const QVariantMap props = getAll(p.service, PlayerIface);
    const QString status = props.value(QStringLiteral("PlaybackStatus")).toString();
    if (status != p.status && (status == QLatin1String("Playing") || status == QLatin1String("Paused")))
        p.lastActive = m_clock.elapsed() + 1;
    if (status == QLatin1String("Playing") && p.status != status && p.service != m_manual)
        m_manual.clear(); // a player that starts playing takes over from a manual pick
    p.status = status;
    p.metadata = asMap(props.value(QStringLiteral("Metadata")));
    p.canNext = props.value(QStringLiteral("CanGoNext")).toBool();
    p.canPrevious = props.value(QStringLiteral("CanGoPrevious")).toBool();
    p.canSeek = props.value(QStringLiteral("CanSeek")).toBool();
}

void MediaPlayers::onPropertiesChanged(const QDBusMessage &message)
{
    for (Player &p : m_players) {
        if (p.owner == message.service()) {
            refresh(p);
            select();
            return;
        }
    }
}

void MediaPlayers::select()
{
    QList<PlayerState> states;
    for (const Player &p : std::as_const(m_players))
        states << PlayerState{p.service, p.status, p.lastActive};
    if (!m_manual.isEmpty() && !m_players.contains(m_manual))
        m_manual.clear();
    m_current = pickPlayer(states, m_manual);

    const QString track = title() + u'\x1f' + artist();
    if (m_ready && playing() && !title().isEmpty() && track != m_lastTrack)
        emit trackChanged(title(), artist(), artUrl());
    if (active())
        m_lastTrack = track;
    emit changed();
}

const MediaPlayers::Player *MediaPlayers::current() const
{
    const auto it = m_players.constFind(m_current);
    return it == m_players.cend() ? nullptr : &it.value();
}

bool MediaPlayers::playing() const
{
    const Player *p = current();
    return p && p->status == QLatin1String("Playing");
}

QString MediaPlayers::title() const
{
    const Player *p = current();
    return p ? p->metadata.value(QStringLiteral("xesam:title")).toString() : QString();
}

QString MediaPlayers::artist() const
{
    const Player *p = current();
    return p ? p->metadata.value(QStringLiteral("xesam:artist")).toStringList().join(QStringLiteral(", ")) : QString();
}

QString MediaPlayers::artUrl() const
{
    const Player *p = current();
    return p ? p->metadata.value(QStringLiteral("mpris:artUrl")).toString() : QString();
}

qint64 MediaPlayers::lengthMs() const
{
    const Player *p = current();
    if (!p)
        return 0;
    // Browsers report "not known yet" (e.g. right after Next) as INT64_MAX; anything that long or
    // non-positive means unknown, which hides the progress bar until the real length arrives.
    constexpr qint64 Week = 7LL * 24 * 3600 * 1000;
    const qint64 ms = p->metadata.value(QStringLiteral("mpris:length")).toLongLong() / 1000;
    return ms > 0 && ms < Week ? ms : 0;
}

bool MediaPlayers::canSeek() const { return current() && current()->canSeek; }
bool MediaPlayers::canNext() const { return current() && current()->canNext; }
bool MediaPlayers::canPrevious() const { return current() && current()->canPrevious; }
QString MediaPlayers::identity() const { return current() ? current()->identity : QString(); }

int MediaPlayers::playerCount() const
{
    QList<PlayerState> states;
    for (const Player &p : std::as_const(m_players))
        states << PlayerState{p.service, p.status, p.lastActive};
    return visiblePlayers(states).size();
}

qint64 MediaPlayers::positionMs() const
{
    const Player *p = current();
    if (!p)
        return 0;
    QDBusInterface props(p->service, Path, Properties, QDBusConnection::sessionBus());
    const QDBusReply<QDBusVariant> reply = props.call(QStringLiteral("Get"), PlayerIface, QStringLiteral("Position"));
    return reply.isValid() ? reply.value().variant().toLongLong() / 1000 : 0;
}

void MediaPlayers::call(const QString &method, const QList<QVariant> &args) const
{
    const Player *p = current();
    if (!p)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(p->service, Path,
                                                      method == QLatin1String("Raise") ? Root : PlayerIface, method);
    msg.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(msg);
}

void MediaPlayers::playPause() { call(QStringLiteral("PlayPause")); }
void MediaPlayers::next() { call(QStringLiteral("Next")); }
void MediaPlayers::previous() { call(QStringLiteral("Previous")); }
void MediaPlayers::raise() { call(QStringLiteral("Raise")); }

void MediaPlayers::seek(qint64 ms)
{
    const Player *p = current();
    if (!p || !p->canSeek)
        return;
    const QDBusObjectPath track = p->metadata.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>();
    call(QStringLiteral("SetPosition"), {QVariant::fromValue(track), QVariant::fromValue(qlonglong(ms * 1000))});
}

void MediaPlayers::cyclePlayer()
{
    QList<PlayerState> states;
    for (const Player &p : std::as_const(m_players))
        states << PlayerState{p.service, p.status, p.lastActive};
    const QList<PlayerState> players = visiblePlayers(states);
    if (players.size() < 2)
        return;
    int at = 0;
    for (int i = 0; i < players.size(); ++i)
        if (players[i].service == m_current)
            at = i;
    m_manual = players[(at + 1) % players.size()].service;
    select();
}
