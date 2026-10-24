#pragma once

#include <QObject>
#include <QVariantList>
#ifdef NOTCH_HAVE_DBUS
#include <QDBusMessage>
#endif

#ifndef NOTCH_HAVE_DBUS
// KDE Connect is a Linux desktop thing: here there are never devices, so "Send to device" does not show.
class KdeConnect : public QObject
{
    Q_OBJECT

public:
    explicit KdeConnect(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE QVariantList devices() const { return {}; }
    Q_INVOKABLE void send(const QString &, const QString &, const QStringList &) {}

signals:
    void sent(int count, const QString &deviceName, const QString &error);
    void received(const QString &url, const QString &deviceName);
};
#else
// KDE Connect over the session bus (org.kde.kdeconnect): send files to paired, reachable devices
// through their share plugin, and hear about files they send to this computer.
class KdeConnect : public QObject
{
    Q_OBJECT

public:
    explicit KdeConnect(QObject *parent = nullptr);

    // Paired devices reachable right now: [{id, name, type}] (type: phone, tablet, desktop, …).
    Q_INVOKABLE QVariantList devices() const;
    // Shares the given file:// URLs with a device. Result arrives as sent().
    Q_INVOKABLE void send(const QString &deviceId, const QString &deviceName, const QStringList &urls);

signals:
    void sent(int count, const QString &deviceName, const QString &error); // error empty on success
    void received(const QString &url, const QString &deviceName);

private slots:
    void onShareReceived(const QDBusMessage &message);
};
#endif
