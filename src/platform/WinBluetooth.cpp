// C++/WinRT first: its headers must not see Qt's keyword macros.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>

#include "platform/WinBluetooth.h"

#include "models/FileItem.h"
#include "platform/BatteryThreshold.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QPointer>

using namespace winrt::Windows::Devices::Enumeration;
using winrt::Windows::Devices::Bluetooth::BluetoothDevice;
using winrt::Windows::Devices::Bluetooth::BluetoothLEDevice;
using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Foundation::IAsyncOperation;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::IReference;

namespace {

const wchar_t *IsConnected = L"System.Devices.Aep.IsConnected";
const wchar_t *CodMajor = L"System.Devices.Aep.Bluetooth.Cod.Major";
const wchar_t *ContainerId = L"System.Devices.Aep.ContainerId";
const wchar_t *Battery = L"{104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2"; // DEVPKEY_Bluetooth_Battery

QString qstr(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), int(s.size()));
}

int unsignedValue(const IInspectable &v)
{
    if (!v)
        return -1;
    if (auto b = v.try_as<IReference<uint8_t>>())
        return b.Value();
    if (auto s = v.try_as<IReference<uint16_t>>())
        return s.Value();
    if (auto i = v.try_as<IReference<uint32_t>>())
        return int(i.Value());
    return -1;
}

template<class Properties>
WinBluetooth::Reading read(const winrt::hstring &id, const Properties &props)
{
    WinBluetooth::Reading r;
    r.id = qstr(id);
    if (const IInspectable v = props.TryLookup(IsConnected)) {
        if (auto b = v.try_as<IReference<bool>>()) {
            r.hasConnected = true;
            r.connected = b.Value();
        }
    }
    r.major = unsignedValue(props.TryLookup(CodMajor));
    if (const IInspectable v = props.TryLookup(ContainerId))
        if (auto g = v.try_as<IReference<winrt::guid>>())
            r.container = qstr(winrt::to_hstring(g.Value()));
    return r;
}

// Runs `f` on the GUI thread if `guard` still exists then. WinRT calls back on its thread pool.
template<class F>
void post(const QPointer<WinBluetooth> &guard, F f)
{
    QMetaObject::invokeMethod(qApp, [guard, f] {
        if (guard)
            f(guard.data());
    }, Qt::QueuedConnection);
}

}

struct WinBluetooth::Watchers {
    struct One {
        DeviceWatcher watcher{nullptr};
        DeviceWatcher::Added_revoker added;
        DeviceWatcher::Updated_revoker updated;
        DeviceWatcher::EnumerationCompleted_revoker completed;
    };
    One classic, le;
};

WinBluetooth::WinBluetooth(QObject *parent)
    : QObject(parent)
    , m_watchers(std::make_unique<Watchers>())
{
    m_batteryPoll.setInterval(60000);
    connect(&m_batteryPoll, &QTimer::timeout, this, &WinBluetooth::pollBatteries);

    const QPointer<WinBluetooth> guard(this);
    auto start = [&](Watchers::One &one, const winrt::hstring &selector) {
        try {
            auto properties = winrt::single_threaded_vector<winrt::hstring>({IsConnected, CodMajor, ContainerId});
            one.watcher = DeviceInformation::CreateWatcher(selector, properties, DeviceInformationKind::AssociationEndpoint);
            one.added = one.watcher.Added(winrt::auto_revoke, [guard](const DeviceWatcher &, const DeviceInformation &info) {
                Reading r = read(info.Id(), info.Properties());
                r.name = qstr(info.Name());
                post(guard, [r](WinBluetooth *self) { self->added(r); });
            });
            one.updated = one.watcher.Updated(winrt::auto_revoke, [guard](const DeviceWatcher &, const DeviceInformationUpdate &update) {
                const Reading r = read(update.Id(), update.Properties());
                post(guard, [r](WinBluetooth *self) { self->updated(r); });
            });
            one.completed = one.watcher.EnumerationCompleted(winrt::auto_revoke, [guard](const DeviceWatcher &, const IInspectable &) {
                post(guard, [](WinBluetooth *self) { self->enumerated(); });
            });
            one.watcher.Start();
        } catch (const winrt::hresult_error &e) {
            qCWarning(lcNotch) << "Bluetooth watcher unavailable:" << qstr(e.message());
            ++m_enumerated; // don't wait for it
        }
    };
    start(m_watchers->classic, BluetoothDevice::GetDeviceSelectorFromPairingState(true));
    start(m_watchers->le, BluetoothLEDevice::GetDeviceSelectorFromPairingState(true));
}

WinBluetooth::~WinBluetooth()
{
    for (Watchers::One *one : {&m_watchers->classic, &m_watchers->le}) {
        one->added.revoke();
        one->updated.revoke();
        one->completed.revoke();
        try {
            if (one->watcher && (one->watcher.Status() == DeviceWatcherStatus::Started
                                 || one->watcher.Status() == DeviceWatcherStatus::EnumerationCompleted))
                one->watcher.Stop();
        } catch (const winrt::hresult_error &) {
        }
    }
}

void WinBluetooth::added(const Reading &r)
{
    Device &d = m_devices[r.id];
    d.name = r.name;
    d.icon = r.major == 4 ? QStringLiteral("headphones") : QStringLiteral("bluetooth"); // 4: audio / video
    d.container = r.container;
    d.connected = r.hasConnected && r.connected;
    if (m_enumerated >= 2 && d.connected) // paired just now
        connectedChanged(r.id);
    else if (d.connected)
        requestBattery(r.id, false); // a known level, so a later drop can warn
}

void WinBluetooth::updated(const Reading &r)
{
    auto it = m_devices.find(r.id);
    if (it == m_devices.end() || !r.hasConnected || it->connected == r.connected)
        return;
    it->connected = r.connected;
    connectedChanged(r.id);
}

void WinBluetooth::enumerated()
{
    ++m_enumerated;
}

void WinBluetooth::connectedChanged(const QString &id)
{
    Device &d = m_devices[id];
    if (!d.connected) {
        d.battery = -1;
        announce(d);
        return;
    }
    requestBattery(id, true); // the alert waits for the level, like on Linux
    if (!m_batteryPoll.isActive())
        m_batteryPoll.start();
}

void WinBluetooth::requestBattery(const QString &id, bool announceAfter)
{
    const Device d = m_devices.value(id);
    if (d.container.isEmpty()) {
        if (announceAfter)
            announce(d);
        return;
    }
    const QPointer<WinBluetooth> guard(this);
    try {
        const std::wstring aqs = L"System.Devices.ContainerId:=\"" + d.container.toStdWString() + L"\"";
        auto op = DeviceInformation::FindAllAsync(winrt::hstring(aqs), winrt::single_threaded_vector<winrt::hstring>({Battery}),
                                                  DeviceInformationKind::Device);
        op.Completed([guard, id, announceAfter](const IAsyncOperation<DeviceInformationCollection> &done, AsyncStatus status) {
            int percent = -1;
            if (status == AsyncStatus::Completed) {
                try {
                    for (const DeviceInformation &info : done.GetResults()) {
                        const int v = unsignedValue(info.Properties().TryLookup(Battery));
                        if (v >= 0 && v <= 100) {
                            percent = v;
                            break;
                        }
                    }
                } catch (const winrt::hresult_error &) {
                }
            }
            post(guard, [id, percent, announceAfter](WinBluetooth *self) { self->batteryRead(id, percent, announceAfter); });
        });
    } catch (const winrt::hresult_error &) {
        if (announceAfter)
            announce(d);
    }
}

void WinBluetooth::batteryRead(const QString &id, int percent, bool announceAfter)
{
    auto it = m_devices.find(id);
    if (it == m_devices.end() || !it->connected)
        return;
    const int before = it->battery;
    it->battery = percent;
    if (announceAfter)
        announce(*it);
    else if (const int level = before >= 0 && percent >= 0 ? crossedLowBattery(before, percent) : 0)
        emit alert(QStringLiteral("lowbattery"), it->name.isEmpty() ? tr("Bluetooth device") : it->name,
                   tr("Battery %1%").arg(percent), it->icon, percent);
}

void WinBluetooth::announce(const Device &d)
{
    const QString name = d.name.isEmpty() ? tr("Bluetooth device") : d.name;
    const QString key = name + (d.connected ? u'+' : u'-');
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastAlert.value(key, 0) < 5000)
        return;
    m_lastAlert.insert(key, now);
    emit alert(QStringLiteral("bluetooth"), name, d.connected ? tr("Connected") : tr("Disconnected"), d.icon,
               d.connected ? d.battery : -1);
}

void WinBluetooth::pollBatteries()
{
    bool any = false;
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it)
        if (it->connected) {
            any = true;
            requestBattery(it.key(), false);
        }
    if (!any)
        m_batteryPoll.stop();
}
