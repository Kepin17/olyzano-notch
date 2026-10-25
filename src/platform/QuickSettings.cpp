#include "platform/QuickSettings.h"

#include "models/FileItem.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#ifdef NOTCH_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>

using Interfaces = QMap<QString, QVariantMap>;
using Objects = QMap<QDBusObjectPath, Interfaces>;
Q_DECLARE_METATYPE(Interfaces)
Q_DECLARE_METATYPE(Objects)

namespace {
const QString Props = QStringLiteral("org.freedesktop.DBus.Properties");

bool has(const QDBusConnection &bus, const QString &service)
{
    return bus.interface() && bus.interface()->isServiceRegistered(service);
}
QDBusMessage ask(const QDBusConnection &bus, const QString &service, const QString &path, const QString &iface, const QString &method,
                 const QVariantList &args = {})
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, iface, method);
    m.setArguments(args);
    return bus.call(m, QDBus::Block, 500);
}
QVariant first(const QDBusMessage &r)
{
    return r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty() ? r.arguments().first() : QVariant();
}
QVariant prop(const QDBusConnection &bus, const QString &service, const QString &path, const QString &iface, const QString &name)
{
    const QVariant v = first(ask(bus, service, path, Props, QStringLiteral("Get"), {iface, name}));
    return v.canConvert<QDBusVariant>() ? v.value<QDBusVariant>().variant() : QVariant();
}
void setProp(const QDBusConnection &bus, const QString &service, const QString &path, const QString &iface, const QString &name, const QVariant &value)
{
    ask(bus, service, path, Props, QStringLiteral("Set"), {iface, name, QVariant::fromValue(QDBusVariant(value))});
}
void fire(const QDBusConnection &bus, const QString &service, const QString &path, const QString &iface, const QString &method, const QVariantList &args = {})
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, iface, method);
    m.setArguments(args);
    bus.asyncCall(m); // a connection can take seconds: don't wait
}

const QString NM = QStringLiteral("org.freedesktop.NetworkManager");
const QString NMPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString Bluez = QStringLiteral("org.bluez");
const QString PPD = QStringLiteral("net.hadess.PowerProfiles");
const QString Power = QStringLiteral("org.kde.Solid.PowerManagement");
const QString PowerPath = QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl");
const QString PowerIface = QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl");
}
#endif

QuickSettings::QuickSettings(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &QuickSettings::refresh);
    m_scan.setInterval(9000); // while the Wi-Fi list is open
    connect(&m_scan, &QTimer::timeout, this, &QuickSettings::scanWifi);
    m_poll.setInterval(20000); // Wi-Fi strength drifts without a signal worth waking for
    connect(&m_poll, &QTimer::timeout, this, &QuickSettings::refresh);
#ifdef NOTCH_HAVE_DBUS
    qDBusRegisterMetaType<QList<QVariantMap>>();
    qDBusRegisterMetaType<Interfaces>();
    qDBusRegisterMetaType<Objects>();
    QDBusConnection sys = QDBusConnection::systemBus();
    for (const QString &service : {NM, Bluez, PPD, QStringLiteral("org.freedesktop.UPower")})
        sys.connect(service, QString(), Props, QStringLiteral("PropertiesChanged"), this, SLOT(onProps(QString, QVariantMap, QStringList)));
    QDBusConnection::sessionBus().connect(Power, PowerPath, PowerIface, QStringLiteral("brightnessChanged"), this, SLOT(onBrightness(int)));
#endif
    m_audio = !QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty();
    refresh();
    listenAudio();
    m_poll.start();
}

QuickSettings::~QuickSettings()
{
    m_subscribe.disconnect();
    m_subscribe.kill();
    m_subscribe.waitForFinished(500);
}

void QuickSettings::soon()
{
    m_debounce.start();
}

void QuickSettings::refresh()
{
    readAudio();
    readSystem();
    emit changed();
}

// ---- sound ----
static QByteArray pactlOut(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("pactl"), args);
    if (!p.waitForFinished(1000) || p.exitCode() != 0) {
        if (p.state() != QProcess::NotRunning)
            p.kill();
        return {};
    }
    return p.readAllStandardOutput();
}

void QuickSettings::readAudio()
{
    if (!m_audio)
        return;
    const QString def = QString::fromUtf8(pactlOut({QStringLiteral("get-default-sink")})).trimmed();
    const QList<PanelModel::Sink> sinks = PanelModel::parseSinks(pactlOut({QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"), QStringLiteral("sinks")}), def);
    m_sinkList.clear();
    m_sinkName.clear();
    for (const PanelModel::Sink &s : sinks) {
        m_sinkList << QVariantMap{{QStringLiteral("name"), s.name}, {QStringLiteral("description"), s.description}, {QStringLiteral("current"), s.isDefault}};
        if (s.isDefault) {
            m_sinkName = s.description;
            m_volume = s.volume;
            m_muted = s.mute;
        }
    }
    if (sinks.isEmpty())
        m_audio = false;

    // the microphone
    const QString defIn = QString::fromUtf8(pactlOut({QStringLiteral("get-default-source")})).trimmed();
    const QList<PanelModel::Sink> sources = PanelModel::parseSources(pactlOut({QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"), QStringLiteral("sources")}), defIn);
    m_sourceList.clear();
    m_sourceName.clear();
    for (const PanelModel::Sink &s : sources) {
        m_sourceList << QVariantMap{{QStringLiteral("name"), s.name}, {QStringLiteral("description"), s.description}, {QStringLiteral("current"), s.isDefault}};
        if (s.isDefault) {
            m_sourceName = s.description;
            m_inVolume = s.volume;
            m_inMuted = s.mute;
        }
    }
}

void QuickSettings::listenAudio()
{
    if (!m_audio)
        return;
    connect(&m_subscribe, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray out = m_subscribe.readAllStandardOutput();
        if (out.contains("sink") || out.contains("source") || out.contains("server"))
            soon();
    });
    connect(&m_subscribe, &QProcess::finished, this, [this] { QTimer::singleShot(3000, this, [this] { m_subscribe.start(QStringLiteral("pactl"), {QStringLiteral("subscribe")}); }); });
    m_subscribe.start(QStringLiteral("pactl"), {QStringLiteral("subscribe")});
}

void QuickSettings::setVolume(int percent)
{
    percent = qBound(0, percent, 100);
    m_volume = percent;
    QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("%1%").arg(percent)});
    emit changed();
}

void QuickSettings::toggleMute()
{
    m_muted = !m_muted;
    QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("toggle")});
    emit changed();
}

void QuickSettings::setInputVolume(int percent)
{
    percent = qBound(0, percent, 100);
    m_inVolume = percent;
    QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-source-volume"), QStringLiteral("@DEFAULT_SOURCE@"), QStringLiteral("%1%").arg(percent)});
    emit changed();
}

void QuickSettings::toggleInputMute()
{
    m_inMuted = !m_inMuted;
    QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-source-mute"), QStringLiteral("@DEFAULT_SOURCE@"), QStringLiteral("toggle")});
    emit changed();
}

void QuickSettings::setSource(const QString &name)
{
    QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-default-source"), name});
    soon();
}

void QuickSettings::setSink(const QString &name)
{
    QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-default-sink"), name});
    soon();
}

// ---- the rest, over D-Bus ----
void QuickSettings::readSystem()
{
#ifdef NOTCH_HAVE_DBUS
    const QDBusConnection sys = QDBusConnection::systemBus(), session = QDBusConnection::sessionBus();

    // brightness
    m_brightMax = 0;
    if (has(session, Power)) {
        m_brightMax = first(ask(session, Power, PowerPath, PowerIface, QStringLiteral("brightnessMax"))).toInt();
        if (m_brightMax > 0)
            m_bright = first(ask(session, Power, PowerPath, PowerIface, QStringLiteral("brightness"))).toInt();
    }

    // power mode
    m_profiles.clear();
    m_profile.clear();
    if (has(sys, PPD)) {
        const QString path = QStringLiteral("/net/hadess/PowerProfiles");
        m_profile = prop(sys, PPD, path, PPD, QStringLiteral("ActiveProfile")).toString();
        const QVariant list = prop(sys, PPD, path, PPD, QStringLiteral("Profiles"));
        if (list.canConvert<QDBusArgument>()) {
            const QList<QVariantMap> all = qdbus_cast<QList<QVariantMap>>(list.value<QDBusArgument>());
            for (const QVariantMap &m : all)
                m_profiles << m.value(QStringLiteral("Profile")).toString();
        }
        if (m_profiles.size() < 2)
            m_profiles.clear();
    }

    // battery: how long
    m_batteryLeft.clear();
    {
        const QString up = QStringLiteral("org.freedesktop.UPower"), path = QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice"),
                      dev = QStringLiteral("org.freedesktop.UPower.Device");
        if (has(sys, up) && prop(sys, up, path, dev, QStringLiteral("IsPresent")).toBool()) {
            const int state = prop(sys, up, path, dev, QStringLiteral("State")).toInt(); // 1 charging, 2 discharging
            m_batteryLeft = PanelModel::timeLeft(prop(sys, up, path, dev, state == 1 ? QStringLiteral("TimeToFull") : QStringLiteral("TimeToEmpty")).toLongLong());
        }
    }

    // Bluetooth
    m_btAvailable = false;
    m_btOn = false;
    m_btDevices.clear();
    m_btConnected.clear();
    if (has(sys, Bluez)) {
        const QVariant r = first(ask(sys, Bluez, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("GetManagedObjects")));
        if (r.canConvert<QDBusArgument>()) {
            const Objects objs = qdbus_cast<Objects>(r.value<QDBusArgument>());
            QStringList connected;
            for (auto it = objs.cbegin(); it != objs.cend(); ++it) {
                if (it.value().contains(QStringLiteral("org.bluez.Adapter1")) && !m_btAvailable) {
                    m_btAvailable = true;
                    m_btAdapter = it.key().path();
                    m_btOn = it.value().value(QStringLiteral("org.bluez.Adapter1")).value(QStringLiteral("Powered")).toBool();
                }
                const QVariantMap d = it.value().value(QStringLiteral("org.bluez.Device1"));
                const bool paired = d.value(QStringLiteral("Paired")).toBool();
                if (d.isEmpty() || (!paired && !(m_btWatch && d.contains(QStringLiteral("RSSI"))))) // found nearby only while looking
                    continue;
                const bool on = d.value(QStringLiteral("Connected")).toBool();
                const QString name = d.value(QStringLiteral("Alias")).toString();
                m_btDevices << QVariantMap{{QStringLiteral("path"), it.key().path()}, {QStringLiteral("name"), name}, {QStringLiteral("connected"), on}, {QStringLiteral("paired"), paired},
                                           {QStringLiteral("icon"), d.value(QStringLiteral("Icon")).toString()}, {QStringLiteral("rssi"), d.value(QStringLiteral("RSSI")).toInt()}};
                if (on)
                    connected << name;
            }
            PanelModel::orderBluetooth(m_btDevices);
            m_btConnected = connected.join(QStringLiteral(", "));
        }
    }

    // Wi-Fi
    m_wifiAvailable = false;
    m_wifiOn = false;
    m_wifiName.clear();
    m_wifiStrength = 0;
    m_wifiNetworks.clear();
    m_wifiDevice.clear();
    m_wifiInfo.clear();
    m_ethInfo.clear();
    m_ethDevice.clear();
    m_ethState.clear();
    m_ethProfile.clear();
    if (has(sys, NM)) {
        const QVariant devs = first(ask(sys, NM, NMPath, NM, QStringLiteral("GetDevices")));
        if (devs.canConvert<QDBusArgument>()) {
            const QList<QDBusObjectPath> all = qdbus_cast<QList<QDBusObjectPath>>(devs.value<QDBusArgument>());
            for (const QDBusObjectPath &d : all)
            {
                const uint type = prop(sys, NM, d.path(), NM + QStringLiteral(".Device"), QStringLiteral("DeviceType")).toUInt();
                if (type == 2 && m_wifiDevice.isEmpty()) // 2: Wi-Fi
                    m_wifiDevice = d.path();
                else if (type == 1 && m_ethDevice.isEmpty()) // 1: Ethernet
                    m_ethDevice = d.path();
            }
        }
        // all the properties of an object (one call), and the addresses of an IPv4 configuration
        const auto getAll = [&](const QString &path, const QString &iface) {
            const QVariant r = first(ask(sys, NM, path, Props, QStringLiteral("GetAll"), {iface}));
            return r.canConvert<QDBusArgument>() ? qdbus_cast<QVariantMap>(r.value<QDBusArgument>()) : QVariantMap();
        };
        const auto ipOf = [&](const QString &device) {
            QVariantMap out;
            const QString cfg = prop(sys, NM, device, NM + QStringLiteral(".Device"), QStringLiteral("Ip4Config")).value<QDBusObjectPath>().path();
            if (cfg.size() <= 1)
                return out;
            const QVariantMap p = getAll(cfg, NM + QStringLiteral(".IP4Config"));
            const auto list = [](const QVariant &v) { return v.canConvert<QDBusArgument>() ? qdbus_cast<QList<QVariantMap>>(v.value<QDBusArgument>()) : QList<QVariantMap>(); };
            const auto addrs = list(p.value(QStringLiteral("AddressData")));
            if (!addrs.isEmpty())
                out[QStringLiteral("ip")] = addrs.first().value(QStringLiteral("address")).toString() + u'/' + addrs.first().value(QStringLiteral("prefix")).toString();
            out[QStringLiteral("gateway")] = p.value(QStringLiteral("Gateway")).toString();
            QStringList dns;
            for (const QVariantMap &n : list(p.value(QStringLiteral("NameserverData"))))
                dns << n.value(QStringLiteral("address")).toString();
            out[QStringLiteral("dns")] = dns.join(QStringLiteral(", "));
            return out;
        };
        // the saved connections: Wi-Fi ones by name, and the Ethernet profile
        QList<PanelModel::SavedWifi> saved;
        if (!m_wifiDevice.isEmpty() || !m_ethDevice.isEmpty()) {
            const QString settings = NMPath + QStringLiteral("/Settings"), conn = NM + QStringLiteral(".Settings.Connection");
            const QVariant list = first(ask(sys, NM, settings, NM + QStringLiteral(".Settings"), QStringLiteral("ListConnections")));
            if (list.canConvert<QDBusArgument>())
                for (const QDBusObjectPath &c : qdbus_cast<QList<QDBusObjectPath>>(list.value<QDBusArgument>())) {
                    const QVariant s = first(ask(sys, NM, c.path(), conn, QStringLiteral("GetSettings")));
                    if (!s.canConvert<QDBusArgument>())
                        continue;
                    const QVariantMap head = qdbus_cast<Interfaces>(s.value<QDBusArgument>()).value(QStringLiteral("connection"));
                    const QString type = head.value(QStringLiteral("type")).toString();
                    if (type == QLatin1String("802-3-ethernet") && m_ethProfile.isEmpty())
                        m_ethProfile = c.path();
                    if (type != QLatin1String("802-11-wireless"))
                        continue;
                    saved << PanelModel::SavedWifi{c.path(), head.value(QStringLiteral("id")).toString()};
                }
        }
        if (!m_ethDevice.isEmpty()) {
            const QVariantMap dev = getAll(m_ethDevice, NM + QStringLiteral(".Device"));
            const QVariantMap wired = getAll(m_ethDevice, NM + QStringLiteral(".Device.Wired"));
            const uint state = dev.value(QStringLiteral("State")).toUInt();
            m_ethState = state == 100 ? QStringLiteral("connected") : state >= 40 && state < 100 ? QStringLiteral("connecting")
                       : !wired.value(QStringLiteral("Carrier"), true).toBool() ? QStringLiteral("unplugged") : QStringLiteral("disconnected");
            m_ethInfo = state == 100 ? ipOf(m_ethDevice) : QVariantMap();
            m_ethInfo[QStringLiteral("device")] = dev.value(QStringLiteral("Interface")).toString();
            m_ethInfo[QStringLiteral("mac")] = wired.value(QStringLiteral("HwAddress")).toString();
            m_ethInfo[QStringLiteral("speed")] = wired.value(QStringLiteral("Speed")).toUInt(); // Mb/s
            const QString active = dev.value(QStringLiteral("ActiveConnection")).value<QDBusObjectPath>().path();
            m_ethInfo[QStringLiteral("name")] = active.size() > 1 ? prop(sys, NM, active, NM + QStringLiteral(".Connection.Active"), QStringLiteral("Id")).toString() : QString();
        }
        m_wifiAvailable = !m_wifiDevice.isEmpty();
        if (m_wifiAvailable) {
            m_wifiOn = prop(sys, NM, NMPath, NM, QStringLiteral("WirelessEnabled")).toBool();
            const QString ap = prop(sys, NM, m_wifiDevice, NM + QStringLiteral(".Device.Wireless"), QStringLiteral("ActiveAccessPoint")).value<QDBusObjectPath>().path();
            if (m_wifiOn && ap.size() > 1) {
                const QString apIface = NM + QStringLiteral(".AccessPoint");
                m_wifiName = QString::fromUtf8(prop(sys, NM, ap, apIface, QStringLiteral("Ssid")).toByteArray());
                m_wifiStrength = prop(sys, NM, ap, apIface, QStringLiteral("Strength")).toInt();
                const QVariantMap a = getAll(ap, apIface);
                const int mhz = a.value(QStringLiteral("Frequency")).toInt();
                const QVariantMap dev = getAll(m_wifiDevice, NM + QStringLiteral(".Device"));
                m_wifiInfo = ipOf(m_wifiDevice);
                m_wifiInfo[QStringLiteral("ssid")] = m_wifiName;
                m_wifiInfo[QStringLiteral("signal")] = m_wifiStrength;
                m_wifiInfo[QStringLiteral("bars")] = wifiBars();
                m_wifiInfo[QStringLiteral("security")] = PanelModel::securityText(a.value(QStringLiteral("Flags")).toUInt(), a.value(QStringLiteral("WpaFlags")).toUInt(), a.value(QStringLiteral("RsnFlags")).toUInt());
                m_wifiInfo[QStringLiteral("frequency")] = mhz;
                m_wifiInfo[QStringLiteral("band")] = PanelModel::bandText(mhz);
                m_wifiInfo[QStringLiteral("channel")] = PanelModel::channelOf(mhz);
                m_wifiInfo[QStringLiteral("bssid")] = a.value(QStringLiteral("HwAddress")).toString();
                m_wifiInfo[QStringLiteral("bitrate")] = a.value(QStringLiteral("MaxBitrate")).toUInt() / 1000; // Mb/s
                m_wifiInfo[QStringLiteral("mac")] = dev.value(QStringLiteral("HwAddress")).toString();
            }
            QList<PanelModel::AccessPoint> aps;
            const QVariant found = m_wifiOn ? first(ask(sys, NM, m_wifiDevice, NM + QStringLiteral(".Device.Wireless"), QStringLiteral("GetAllAccessPoints"))) : QVariant();
            if (found.canConvert<QDBusArgument>())
                for (const QDBusObjectPath &p : qdbus_cast<QList<QDBusObjectPath>>(found.value<QDBusArgument>())) {
                    const QVariant all = first(ask(sys, NM, p.path(), Props, QStringLiteral("GetAll"), {NM + QStringLiteral(".AccessPoint")}));
                    if (!all.canConvert<QDBusArgument>())
                        continue;
                    const QVariantMap a = qdbus_cast<QVariantMap>(all.value<QDBusArgument>());
                    const uint wpa = a.value(QStringLiteral("WpaFlags")).toUInt(), rsn = a.value(QStringLiteral("RsnFlags")).toUInt();
                    aps << PanelModel::AccessPoint{QString::fromUtf8(a.value(QStringLiteral("Ssid")).toByteArray()), p.path(), a.value(QStringLiteral("Strength")).toInt(),
                                                   (a.value(QStringLiteral("Flags")).toUInt() & 1) || wpa || rsn, ((wpa | rsn) & 0x200) != 0, // 0x200: 802.1x
                                                   PanelModel::securityText(a.value(QStringLiteral("Flags")).toUInt(), wpa, rsn), a.value(QStringLiteral("Frequency")).toInt()};
                }
            m_wifiNetworks = PanelModel::mergeWifi(aps, saved, m_wifiName);
        }
    }
#endif
}

void QuickSettings::setBrightness(int percent)
{
#ifdef NOTCH_HAVE_DBUS
    if (m_brightMax <= 0)
        return;
    m_bright = qBound(1, qRound(m_brightMax * qBound(1, percent, 100) / 100.0), m_brightMax);
    fire(QDBusConnection::sessionBus(), Power, PowerPath, PowerIface, QStringLiteral("setBrightness"), {m_bright});
    emit changed();
#else
    Q_UNUSED(percent)
#endif
}

void QuickSettings::setProfile(const QString &profile)
{
#ifdef NOTCH_HAVE_DBUS
    if (!m_profiles.contains(profile))
        return;
    m_profile = profile;
    setProp(QDBusConnection::systemBus(), PPD, QStringLiteral("/net/hadess/PowerProfiles"), PPD, QStringLiteral("ActiveProfile"), profile);
    emit changed();
#else
    Q_UNUSED(profile)
#endif
}

void QuickSettings::setBtEnabled(bool on)
{
#ifdef NOTCH_HAVE_DBUS
    if (!m_btAvailable)
        return;
    m_btOn = on;
    setProp(QDBusConnection::systemBus(), Bluez, m_btAdapter, QStringLiteral("org.bluez.Adapter1"), QStringLiteral("Powered"), on);
    emit changed();
    soon();
#else
    Q_UNUSED(on)
#endif
}

void QuickSettings::toggleBtDevice(const QString &path)
{
#ifdef NOTCH_HAVE_DBUS
    for (const QVariant &v : std::as_const(m_btDevices))
        if (v.toMap().value(QStringLiteral("path")).toString() == path) {
            fire(QDBusConnection::systemBus(), Bluez, path, QStringLiteral("org.bluez.Device1"),
                 v.toMap().value(QStringLiteral("connected")).toBool() ? QStringLiteral("Disconnect") : QStringLiteral("Connect"));
            QTimer::singleShot(1500, this, &QuickSettings::refresh);
            return;
        }
#else
    Q_UNUSED(path)
#endif
}

void QuickSettings::setWifiEnabled(bool on)
{
#ifdef NOTCH_HAVE_DBUS
    if (!m_wifiAvailable)
        return;
    m_wifiOn = on;
    setProp(QDBusConnection::systemBus(), NM, NMPath, NM, QStringLiteral("WirelessEnabled"), on);
    emit changed();
    soon();
#else
    Q_UNUSED(on)
#endif
}

void QuickSettings::connectWifi(const QString &connectionPath)
{
#ifdef NOTCH_HAVE_DBUS
    if (m_wifiDevice.isEmpty())
        return;
    fire(QDBusConnection::systemBus(), NM, NMPath, NM, QStringLiteral("ActivateConnection"),
         {QVariant::fromValue(QDBusObjectPath(connectionPath)), QVariant::fromValue(QDBusObjectPath(m_wifiDevice)), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))});
    QTimer::singleShot(2500, this, &QuickSettings::refresh);
#else
    Q_UNUSED(connectionPath)
#endif
}

void QuickSettings::joinWifi(const QString &apPath, const QString &ssid, const QString &password)
{
#ifdef NOTCH_HAVE_DBUS
    if (m_wifiDevice.isEmpty())
        return;
    Interfaces settings;
    settings[QStringLiteral("connection")] = {{QStringLiteral("type"), QStringLiteral("802-11-wireless")}, {QStringLiteral("id"), ssid}};
    settings[QStringLiteral("802-11-wireless")] = {{QStringLiteral("ssid"), ssid.toUtf8()}};
    if (!password.isEmpty())
        settings[QStringLiteral("802-11-wireless-security")] = {{QStringLiteral("key-mgmt"), QStringLiteral("wpa-psk")}, {QStringLiteral("psk"), password}};
    fire(QDBusConnection::systemBus(), NM, NMPath, NM, QStringLiteral("AddAndActivateConnection"),
         {QVariant::fromValue(settings), QVariant::fromValue(QDBusObjectPath(m_wifiDevice)), QVariant::fromValue(QDBusObjectPath(apPath))});
    QTimer::singleShot(3500, this, &QuickSettings::refresh);
#else
    Q_UNUSED(apPath) Q_UNUSED(ssid) Q_UNUSED(password)
#endif
}

void QuickSettings::disconnectWifi()
{
#ifdef NOTCH_HAVE_DBUS
    if (m_wifiDevice.isEmpty())
        return;
    fire(QDBusConnection::systemBus(), NM, m_wifiDevice, NM + QStringLiteral(".Device"), QStringLiteral("Disconnect"));
    QTimer::singleShot(1500, this, &QuickSettings::refresh);
#endif
}

void QuickSettings::requestWifiPassword(const QString &connectionPath)
{
#ifdef NOTCH_HAVE_DBUS
    m_secretFor = connectionPath;
    m_secret.clear();
    m_secretError.clear();
    m_secretBusy = true;
    emit secretChanged();
    QDBusMessage m = QDBusMessage::createMethodCall(NM, connectionPath, NM + QStringLiteral(".Settings.Connection"), QStringLiteral("GetSecrets"));
    m.setArguments({QStringLiteral("802-11-wireless-security")});
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(m, 60000), this); // a permission dialog may be shown meanwhile
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, connectionPath, w] {
        w->deleteLater();
        if (m_secretFor != connectionPath)
            return; // asked for another one since
        m_secretBusy = false;
        const QDBusMessage r = w->reply();
        if (r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty() && r.arguments().first().canConvert<QDBusArgument>()) {
            const QVariantMap sec = qdbus_cast<Interfaces>(r.arguments().first().value<QDBusArgument>()).value(QStringLiteral("802-11-wireless-security"));
            m_secret = sec.value(QStringLiteral("psk")).toString();
            if (m_secret.isEmpty())
                m_secretError = tr("This network has no password saved here.");
        } else {
            m_secretError = r.type() == QDBusMessage::ErrorMessage ? r.errorMessage() : tr("No answer.");
        }
        emit secretChanged();
    });
#else
    Q_UNUSED(connectionPath)
#endif
}

void QuickSettings::forgetWifiPassword()
{
    if (m_secretFor.isEmpty() && m_secret.isEmpty() && m_secretError.isEmpty())
        return;
    m_secretFor.clear();
    m_secret.clear();
    m_secretError.clear();
    m_secretBusy = false;
    emit secretChanged();
}

void QuickSettings::setEthernet(bool on)
{
#ifdef NOTCH_HAVE_DBUS
    if (m_ethDevice.isEmpty())
        return;
    if (on && !m_ethProfile.isEmpty())
        fire(QDBusConnection::systemBus(), NM, NMPath, NM, QStringLiteral("ActivateConnection"),
             {QVariant::fromValue(QDBusObjectPath(m_ethProfile)), QVariant::fromValue(QDBusObjectPath(m_ethDevice)), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))});
    else if (!on)
        fire(QDBusConnection::systemBus(), NM, m_ethDevice, NM + QStringLiteral(".Device"), QStringLiteral("Disconnect"));
    QTimer::singleShot(2000, this, &QuickSettings::refresh);
#else
    Q_UNUSED(on)
#endif
}

void QuickSettings::scanWifi()
{
#ifdef NOTCH_HAVE_DBUS
    if (m_wifiWatch && m_wifiDevice.size())
        fire(QDBusConnection::systemBus(), NM, m_wifiDevice, NM + QStringLiteral(".Device.Wireless"), QStringLiteral("RequestScan"), {QVariant::fromValue(QVariantMap())});
    soon();
#endif
}

void QuickSettings::watchWifi(bool on)
{
    m_wifiWatch = on;
    if (!on) {
        m_scan.stop();
        return;
    }
    m_scan.start();
    scanWifi();
    refresh();
}

void QuickSettings::watchBluetooth(bool on)
{
#ifdef NOTCH_HAVE_DBUS
    if (!m_btAvailable || m_btWatch == on)
        return;
    m_btWatch = on;
    fire(QDBusConnection::systemBus(), Bluez, m_btAdapter, QStringLiteral("org.bluez.Adapter1"), on ? QStringLiteral("StartDiscovery") : QStringLiteral("StopDiscovery"));
    QTimer::singleShot(on ? 1500 : 0, this, &QuickSettings::refresh);
#else
    Q_UNUSED(on)
#endif
}

void QuickSettings::pairBtDevice(const QString &path)
{
#ifdef NOTCH_HAVE_DBUS
    const QString dev = QStringLiteral("org.bluez.Device1");
    QDBusMessage m = QDBusMessage::createMethodCall(Bluez, path, dev, QStringLiteral("Pair"));
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(m, 60000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, path, dev, w] {
        if (!w->isError()) { // paired: trust it, and connect
            setProp(QDBusConnection::systemBus(), Bluez, path, dev, QStringLiteral("Trusted"), true);
            fire(QDBusConnection::systemBus(), Bluez, path, dev, QStringLiteral("Connect"));
        }
        w->deleteLater();
        QTimer::singleShot(1500, this, &QuickSettings::refresh);
    });
#else
    Q_UNUSED(path)
#endif
}

void QuickSettings::openSystemSettings()
{
    QProcess::startDetached(QStringLiteral("systemsettings"), {});
}

QByteArray QuickSettings::dump() const
{
    QJsonObject o;
    o[QStringLiteral("audio")] = QJsonObject{{QStringLiteral("available"), m_audio}, {QStringLiteral("volume"), m_volume}, {QStringLiteral("muted"), m_muted},
                                            {QStringLiteral("output"), m_sinkName}, {QStringLiteral("outputs"), QJsonArray::fromVariantList(m_sinkList)},
                                            {QStringLiteral("input"), m_sourceName}, {QStringLiteral("inputVolume"), m_inVolume}, {QStringLiteral("inputMuted"), m_inMuted},
                                            {QStringLiteral("inputs"), QJsonArray::fromVariantList(m_sourceList)}};
    o[QStringLiteral("brightness")] = QJsonObject{{QStringLiteral("available"), m_brightMax > 0}, {QStringLiteral("percent"), brightness()}};
    o[QStringLiteral("power")] = QJsonObject{{QStringLiteral("profile"), m_profile}, {QStringLiteral("profiles"), QJsonArray::fromStringList(m_profiles)}, {QStringLiteral("left"), m_batteryLeft}};
    o[QStringLiteral("bluetooth")] = QJsonObject{{QStringLiteral("available"), m_btAvailable}, {QStringLiteral("on"), m_btOn}, {QStringLiteral("connected"), m_btConnected},
                                               {QStringLiteral("devices"), QJsonArray::fromVariantList(m_btDevices)}};
    o[QStringLiteral("wifi")] = QJsonObject{{QStringLiteral("available"), m_wifiAvailable}, {QStringLiteral("on"), m_wifiOn}, {QStringLiteral("network"), m_wifiName},
                                          {QStringLiteral("bars"), wifiBars()}, {QStringLiteral("info"), QJsonObject::fromVariantMap(m_wifiInfo)}, {QStringLiteral("inRange"), QJsonArray::fromVariantList(m_wifiNetworks)}};
    o[QStringLiteral("ethernet")] = QJsonObject{{QStringLiteral("available"), !m_ethDevice.isEmpty()}, {QStringLiteral("state"), m_ethState}, {QStringLiteral("info"), QJsonObject::fromVariantMap(m_ethInfo)}};
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}
