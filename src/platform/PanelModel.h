#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>

// What Quick Settings can work out without asking the system. Pure, for the tests.
namespace PanelModel {

struct Sink {
    QString name;        // what `pactl set-default-sink` takes
    QString description; // what a person reads
    int volume = 0;      // percent, the loudest channel
    bool mute = false;
    bool isDefault = false;
};

// `pactl -f json list sinks`: volume.<channel>.value_percent is "62%"; the monitor / dummy sinks are kept, the
// default one is marked.
inline QList<Sink> parseSinks(const QByteArray &json, const QString &defaultSink)
{
    QList<Sink> out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        Sink s;
        s.name = o.value(QLatin1String("name")).toString();
        if (s.name.isEmpty())
            continue;
        s.description = o.value(QLatin1String("description")).toString();
        s.mute = o.value(QLatin1String("mute")).toBool();
        const QJsonObject channels = o.value(QLatin1String("volume")).toObject();
        for (auto it = channels.begin(); it != channels.end(); ++it)
            s.volume = qMax(s.volume, it.value().toObject().value(QLatin1String("value_percent")).toString().remove(u'%').toInt());
        s.isDefault = s.name == defaultSink;
        out << s;
    }
    return out;
}

// `pactl -f json list sources`: the same shape as the sinks; what plays through the speakers (".monitor") is not a microphone.
inline QList<Sink> parseSources(const QByteArray &json, const QString &defaultSource)
{
    QList<Sink> all = parseSinks(json, defaultSource), out;
    for (const Sink &s : all)
        if (!s.name.endsWith(QLatin1String(".monitor")))
            out << s;
    return out;
}

// Wi-Fi signal strength (0-100) as 0-4 bars.
inline int wifiBars(int strength)
{
    return strength <= 0 ? 0 : strength < 25 ? 1 : strength < 50 ? 2 : strength < 75 ? 3 : 4;
}

struct AccessPoint {
    QString ssid, path;
    int strength = 0;
    bool secure = false;
    bool enterprise = false; // 802.1x: needs more than a password, left to the system settings
    QString security;        // "WPA2-PSK", "Open"...
    int frequency = 0;       // MHz
};
struct SavedWifi {
    QString id, name; // the connection's object path, its name
};

// The networks in range, one row per name (the strongest access point of it): the one in use first, then those already
// saved, then the rest by signal. Hidden networks (no name) are left out.
inline QVariantList mergeWifi(const QList<AccessPoint> &aps, const QList<SavedWifi> &saved, const QString &active)
{
    QList<AccessPoint> best;
    for (const AccessPoint &ap : aps) {
        if (ap.ssid.isEmpty())
            continue;
        auto it = std::find_if(best.begin(), best.end(), [&](const AccessPoint &b) { return b.ssid == ap.ssid; });
        if (it == best.end())
            best << ap;
        else if (ap.strength > it->strength)
            *it = ap;
    }
    auto savedId = [&](const QString &name) {
        for (const SavedWifi &s : saved)
            if (s.name == name)
                return s.id;
        return QString();
    };
    std::stable_sort(best.begin(), best.end(), [&](const AccessPoint &a, const AccessPoint &b) {
        const int ra = a.ssid == active ? 2 : !savedId(a.ssid).isEmpty() ? 1 : 0, rb = b.ssid == active ? 2 : !savedId(b.ssid).isEmpty() ? 1 : 0;
        return ra != rb ? ra > rb : a.strength > b.strength;
    });
    QVariantList out;
    for (const AccessPoint &ap : best)
        out << QVariantMap{{QStringLiteral("name"), ap.ssid}, {QStringLiteral("ap"), ap.path}, {QStringLiteral("id"), savedId(ap.ssid)},
                           {QStringLiteral("strength"), ap.strength}, {QStringLiteral("bars"), wifiBars(ap.strength)}, {QStringLiteral("secure"), ap.secure},
                           {QStringLiteral("enterprise"), ap.enterprise}, {QStringLiteral("security"), ap.security}, {QStringLiteral("frequency"), ap.frequency}, {QStringLiteral("saved"), !savedId(ap.ssid).isEmpty()}, {QStringLiteral("active"), ap.ssid == active}};
    return out;
}

// Bluetooth devices: connected first, then the paired, then those found nearby by signal (rssi is negative: closer is larger).
inline void orderBluetooth(QVariantList &devices)
{
    auto rank = [](const QVariantMap &d) { return d.value(QStringLiteral("connected")).toBool() ? 2 : d.value(QStringLiteral("paired")).toBool() ? 1 : 0; };
    std::stable_sort(devices.begin(), devices.end(), [&](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        return rank(x) != rank(y) ? rank(x) > rank(y) : x.value(QStringLiteral("rssi")).toInt() > y.value(QStringLiteral("rssi")).toInt();
    });
}

// Wi-Fi channel of a frequency in MHz (2.4, 5 and 6 GHz bands); 0 when it is none of them.
inline int channelOf(int mhz)
{
    if (mhz == 2484)
        return 14;
    if (mhz >= 2412 && mhz <= 2472)
        return (mhz - 2407) / 5;
    if (mhz >= 5160 && mhz <= 5885)
        return (mhz - 5000) / 5;
    if (mhz >= 5955 && mhz <= 7115)
        return (mhz - 5950) / 5;
    return 0;
}
inline QString bandText(int mhz)
{
    return mhz >= 5925 ? QStringLiteral("6 GHz") : mhz >= 4900 ? QStringLiteral("5 GHz") : mhz >= 2400 ? QStringLiteral("2.4 GHz") : QString();
}
// What NetworkManager's access point flags say about its security (Flags, WpaFlags, RsnFlags).
inline QString securityText(uint flags, uint wpa, uint rsn)
{
    const uint any = wpa | rsn;
    if (rsn & 0x400)
        return (rsn & 0x100) ? QStringLiteral("WPA2/WPA3") : QStringLiteral("WPA3-SAE");
    if (rsn & 0x800)
        return QStringLiteral("OWE");
    if (rsn & 0x200)
        return QStringLiteral("WPA2-Enterprise");
    if (rsn & 0x100)
        return QStringLiteral("WPA2-PSK");
    if (wpa & 0x200)
        return QStringLiteral("WPA-Enterprise");
    if (wpa & 0x100)
        return QStringLiteral("WPA-PSK");
    if (any)
        return QStringLiteral("WPA");
    return (flags & 1) ? QStringLiteral("WEP") : QStringLiteral("Open");
}
// The text a phone's camera reads to join a network: WIFI:T:WPA;S:name;P:password;;  (\ ; , : " are escaped).
inline QString wifiQrPayload(const QString &ssid, const QString &password, bool secure)
{
    auto esc = [](QString s) {
        QString out;
        for (const QChar c : std::as_const(s)) {
            if (c == u'\\' || c == u';' || c == u',' || c == u':' || c == u'"')
                out += u'\\';
            out += c;
        }
        return out;
    };
    return secure ? QStringLiteral("WIFI:T:WPA;S:%1;P:%2;;").arg(esc(ssid), esc(password)) : QStringLiteral("WIFI:T:nopass;S:%1;;").arg(esc(ssid));
}

// 0 muted or silent, 1 low, 2 medium, 3 high: which speaker to draw.
inline int volumeLevel(int volume, bool mute)
{
    return mute || volume <= 0 ? 0 : volume < 34 ? 1 : volume < 67 ? 2 : 3;
}

// "2 h 05 min", "35 min"; empty when unknown (0 or less).
inline QString timeLeft(qint64 seconds)
{
    if (seconds <= 0)
        return {};
    const qint64 minutes = (seconds + 30) / 60;
    if (minutes < 60)
        return QStringLiteral("%1 min").arg(qMax<qint64>(1, minutes));
    return QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

}
