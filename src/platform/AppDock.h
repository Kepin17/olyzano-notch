#pragma once

#include "platform/DockModel.h"

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QQuickImageProvider>
#include <QSettings>
#include <QTimer>
#include <QVariantList>

// The app dock along the bottom of the screen: pinned apps plus whatever is running.
// KDE Plasma: KWin reports the open windows to us (a small KWin script calls back over D-Bus) and raises,
// minimizes or closes them on request, so clicking a running app brings its window back.
// Other Linux desktops: apps come from .desktop files, "running" from /proc; a click can only start the app.
// Windows: apps come from the Start Menu, running ones from the visible top-level windows.
// Pins live in notch/dock.conf next to the other settings.
class AppDock : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.olyzano.Notch.Dock")
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool autoHide READ autoHide WRITE setAutoHide NOTIFY autoHideChanged)
    // Look and place, from Settings.
    Q_PROPERTY(QString position READ position WRITE setPosition NOTIFY appearanceChanged)         // bottom | left | right
    Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY appearanceChanged)              // px, 32-80
    Q_PROPERTY(double magnification READ magnification WRITE setMagnification NOTIFY appearanceChanged) // hovered icon's scale, 1 = off
    Q_PROPERTY(double backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY appearanceChanged) // 0.2-1
    // {id, name, iconSource, pinned, running, active, canLaunch}
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)

public:
    explicit AppDock(QObject *parent = nullptr);
    ~AppDock() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool autoHide() const { return m_autoHide; }
    void setAutoHide(bool autoHide);
    QVariantList items() const { return m_items; }
    QString position() const { return m_settings.value(QStringLiteral("position"), QStringLiteral("bottom")).toString(); }
    void setPosition(const QString &position);
    int iconSize() const { return m_settings.value(QStringLiteral("iconSize"), 46).toInt(); }
    void setIconSize(int size);
    double magnification() const { return m_settings.value(QStringLiteral("magnification"), 1.3).toDouble(); }
    void setMagnification(double scale);
    double backgroundOpacity() const { return m_settings.value(QStringLiteral("opacity"), 0.85).toDouble(); }
    void setBackgroundOpacity(double opacity);

    // Polling (where the window list isn't pushed to us) only runs while a dock is showing.
    Q_INVOKABLE void setWatching(bool watching);
    // Click: raise the app's window (minimize it if it's already in front, cycle if it has several),
    // or start the app when nothing of it is open.
    Q_INVOKABLE void activate(const QString &id);
    Q_INVOKABLE void launchNew(const QString &id);
    Q_INVOKABLE void close(const QString &id); // close all of its windows
    Q_INVOKABLE void pin(const QString &id);
    Q_INVOKABLE void unpin(const QString &id);
    Q_INVOKABLE void movePinned(int from, int to);
    // Installed apps for the picker, filtered by name; {id, name, iconSource, pinned}.
    Q_INVOKABLE QVariantList installedApps(const QString &filter);

    QIcon iconFor(const QString &id) const;

public slots:
    // Called by the KWin script with the current windows as JSON. Scriptable = exported on D-Bus.
    Q_SCRIPTABLE void kwinWindows(const QString &json);

signals:
    void enabledChanged();
    void autoHideChanged();
    void appearanceChanged();
    void positionChanged(); // also signalled through appearanceChanged; the window has to move
    void itemsChanged();

private:
    struct KWinWindow {
        QString id, desktopFile, windowClass;
        qint64 pid = 0;
        bool active = false;
    };

    void storeAppearance(const QString &key, const QVariant &value);
    void loadCatalog();
    void refresh();
    QList<DockApp> runningApps(QString *activeId);
    void startKWin();
    void runKWinAction(const QString &mode, const QStringList &windows);
    static QString iconSource(const QString &id);

    QSettings m_settings;
    bool m_enabled = true;
    bool m_autoHide = true;
    QStringList m_pinned;
    QHash<QString, DockApp> m_catalog;
    QHash<QString, DockApp> m_seenRunning; // running apps outside the catalog, kept for name and icon
    QHash<QString, QStringList> m_appWindows; // app id -> its windows, front to back (KWin ids / HWNDs)
    QVariantList m_items;
    QTimer m_timer;
    bool m_catalogLoaded = false;
    bool m_kwin = false;
    QList<KWinWindow> m_kwinWindows;
};

// image://appicon/<base64url app id> -> the app's icon.
class DockIconProvider : public QQuickImageProvider
{
public:
    explicit DockIconProvider(AppDock *dock) : QQuickImageProvider(Pixmap), m_dock(dock) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requested) override;

private:
    AppDock *m_dock;
};
