#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>

#include "platform/NotificationFilter.h"

#include <memory>

// Notifications other apps show on this computer, repeated in the notch as "Notification from <app> · <title>"
// (never the message itself), as `desktopNotifications` in QML. Linux watches the session bus for
// org.freedesktop.Notifications.Notify calls (as a D-Bus monitor, like KDE Connect does); Windows asks
// UserNotificationListener, which Windows may refuse to an app without package identity: then `available`
// is false. Settings in notch/notifications.conf.
class DesktopNotifications : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QStringList ignored READ ignored NOTIFY settingsChanged)
    // Apps that showed notifications lately, newest first (ignored ones too), for Settings to switch off and on.
    Q_PROPERTY(QStringList recentApps READ recentApps NOTIFY settingsChanged)

public:
    explicit DesktopNotifications(QObject *parent = nullptr);
    ~DesktopNotifications() override;

    bool available() const { return m_available; }
    bool enabled() const { return m_settings.value(QStringLiteral("enabled"), true).toBool(); }
    void setEnabled(bool on);
    QStringList ignored() const { return m_settings.value(QStringLiteral("ignored")).toStringList(); }
    QStringList recentApps() const { return m_settings.value(QStringLiteral("recent")).toStringList(); }
    Q_INVOKABLE void ignore(const QString &app);
    Q_INVOKABLE void unignore(const QString &app);

    // From the platform listener, on this object's thread.
    void received(const QString &app, const QString &title, const QString &icon);
    void setAvailable(bool available);

signals:
    void availableChanged();
    void settingsChanged();
    void notification(const QString &app, const QString &title, const QString &icon); // filtered, to show and sound

private:
    bool doNotDisturb() const;
    void start();
    void stop();

    QSettings m_settings;
    NotificationFilter m_filter;
    bool m_available = false;
    struct Listener;
    std::unique_ptr<Listener> m_listener;
};
