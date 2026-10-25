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
    Q_PROPERTY(bool blur READ blur WRITE setBlur NOTIFY appearanceChanged) // blur what's behind the bar (KDE)
    Q_PROPERTY(QString animation READ animation WRITE setAnimation NOTIFY appearanceChanged) // none | smooth | bouncy
    Q_PROPERTY(bool showRunning READ showRunning WRITE setShowRunning NOTIFY appearanceChanged) // running apps that aren't pinned
    Q_PROPERTY(QVariantList pinnedApps READ pinnedApps NOTIFY itemsChanged) // {id, name, iconSource}, for Settings
    // {id, name, iconSource, pinned, running, active, canLaunch, kind ("app" | "folder"),
    //  badge (unread count, 0 = none), progress (0-1, -1 = none), urgent, windows [{id, title}]}
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
    double magnification() const { return m_settings.value(QStringLiteral("magnification"), 1.12).toDouble(); }
    void setMagnification(double scale);
    double backgroundOpacity() const { return m_settings.value(QStringLiteral("opacity"), 0.85).toDouble(); }
    void setBackgroundOpacity(double opacity);
    bool blur() const { return m_settings.value(QStringLiteral("blur"), true).toBool(); }
    void setBlur(bool blur);
    QString animation() const { return m_settings.value(QStringLiteral("animation"), QStringLiteral("smooth")).toString(); }
    void setAnimation(const QString &animation);
    bool showRunning() const { return m_settings.value(QStringLiteral("showRunning"), true).toBool(); }
    void setShowRunning(bool show);
    QVariantList pinnedApps() const;

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
    // Drag-and-drop: pin (if needed) and put the app at `index` among the pinned ones.
    Q_INVOKABLE void pinAt(const QString &id, int index);
    // Installed apps for the picker, filtered by name; {id, name, iconSource, pinned}.
    Q_INVOKABLE QVariantList installedApps(const QString &filter);
    // Bring one particular window forward (from the hover list of an app's windows).
    Q_INVOKABLE void activateWindow(const QString &windowId);
    // Files dropped on an app's icon: open them with it (file:// URLs or paths).
    Q_INVOKABLE void openWith(const QString &id, const QStringList &urls);
    // Folders kept in the dock ("folder:<path>" items) open as a stack of their newest entries.
    Q_INVOKABLE void pinFolder(const QString &pathOrUrl);
    Q_INVOKABLE QString downloadsFolder() const;
    // {name, url, iconName, isDir}, newest first.
    Q_INVOKABLE QVariantList folderEntries(const QString &id, int limit = 24) const;

    QIcon iconFor(const QString &id) const;

public slots:
    // Called by the KWin script with the current windows as JSON. Scriptable = exported on D-Bus.
    Q_SCRIPTABLE void kwinWindows(const QString &json);
    // com.canonical.Unity.LauncherEntry.Update from any app: unread counts, progress, urgency.
    void launcherEntry(const QString &appUri, const QVariantMap &properties);

signals:
    void enabledChanged();
    void autoHideChanged();
    void appearanceChanged();
    void positionChanged(); // also signalled through appearanceChanged; the window has to move
    void itemsChanged();

private:
    struct KWinWindow {
        QString id, desktopFile, windowClass, caption;
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
    static bool isFolder(const QString &id) { return id.startsWith(QLatin1String("folder:")); }
    static QString folderPath(const QString &id) { return id.mid(7); }
    bool openIfFolder(const QString &id) const;

    QSettings m_settings;
    bool m_enabled = true;
    bool m_autoHide = true;
    QStringList m_pinned;
    QHash<QString, DockApp> m_catalog;
    QHash<QString, DockApp> m_seenRunning; // running apps outside the catalog, kept for name and icon
    QHash<QString, QStringList> m_appWindows; // app id -> its windows, front to back (KWin ids / HWNDs)
    QHash<QString, QString> m_windowTitles;   // window id -> title
    QHash<QString, QVariantMap> m_badges;     // desktop file id -> launcher entry properties
    QVariantList m_items;
    QTimer m_timer;
    bool m_catalogLoaded = false;
    bool m_kwin = false;
    int m_actions = 0;             // numbers each click's KWin script
    QStringList m_pendingActions;  // their plugin names until unloaded
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
