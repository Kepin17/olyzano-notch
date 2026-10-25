#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#if defined(NOTCH_HAVE_DBUS) || defined(NOTCH_HAVE_KDECONNECT)
#include <QDBusMessage>
#endif

#if !defined(NOTCH_HAVE_DBUS) && !defined(NOTCH_HAVE_KDECONNECT)
// No KDE Connect here: there are never devices, so "Send to device" does not show.
class KdeConnect : public QObject
{
    Q_OBJECT

public:
    explicit KdeConnect(QObject *parent = nullptr) : QObject(parent) {}
    Q_PROPERTY(QVariantList deviceList READ deviceList NOTIFY devicesChanged)
    Q_PROPERTY(bool hasApp READ hasApp CONSTANT)
    QVariantList deviceList() const { return {}; }
    bool hasApp() const { return false; }
    Q_INVOKABLE QVariantList devices() const { return {}; }
    Q_INVOKABLE void send(const QString &, const QString &, const QStringList &) {}
    Q_INVOKABLE void ping(const QString &) {}
    Q_INVOKABLE void findPhone(const QString &) {}
    Q_INVOKABLE void openApp() {}

signals:
    void devicesChanged();
    void sent(int count, const QString &deviceName, const QString &error);
    void received(const QString &url, const QString &deviceName);
    void phoneCall(const QString &event, const QString &who, const QString &deviceName);
    void phoneNotification(const QString &app, const QString &title, const QString &text, const QString &deviceName);
    void phoneBattery(int charge, bool charging, const QString &deviceName);
};
#else
// KDE Connect over the session bus (org.kde.kdeconnect): send files to paired, reachable devices
// through their share plugin, and hear about files they send to this computer, calls coming in,
// notifications posted on the phone and its battery. On Windows that bus is the one KDE Connect for
// Windows runs itself; without KDE Connect installed there are no devices.
class KdeConnect : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList deviceList READ deviceList NOTIFY devicesChanged) // reachable paired devices: [{id, name, type, charge, charging}]
    Q_PROPERTY(bool hasApp READ hasApp CONSTANT) // the KDE Connect app is installed

public:
    explicit KdeConnect(QObject *parent = nullptr);

    QVariantList deviceList() const { return m_list; }
    bool hasApp() const;
    Q_INVOKABLE void ping(const QString &deviceId);
    Q_INVOKABLE void findPhone(const QString &deviceId); // makes it ring
    Q_INVOKABLE void openApp();                          // kdeconnect-app (or its settings)

    // Paired devices reachable right now: [{id, name, type}] (type: phone, tablet, desktop, …).
    Q_INVOKABLE QVariantList devices() const;
    // Shares the given file:// URLs with a device. Result arrives as sent().
    Q_INVOKABLE void send(const QString &deviceId, const QString &deviceName, const QStringList &urls);

signals:
    void devicesChanged();
    void sent(int count, const QString &deviceName, const QString &error); // error empty on success
    void received(const QString &url, const QString &deviceName);
    // event: "ringing", "talking", "missedCall" or "sms"; who: the contact's name, or the number.
    void phoneCall(const QString &event, const QString &who, const QString &deviceName);
    void phoneNotification(const QString &app, const QString &title, const QString &text, const QString &deviceName);
    void phoneBattery(int charge, bool charging, const QString &deviceName);

private slots:
    void onShareReceived(const QDBusMessage &message);
    void onCall(const QDBusMessage &message);
    void onNotificationPosted(const QDBusMessage &message);
    void onBattery(const QDBusMessage &message);
    void refreshList();

private:
    QVariantList m_list;
    QTimer m_poll;
};
#endif
