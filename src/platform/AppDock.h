#pragma once

#include "platform/DockModel.h"

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QQuickImageProvider>
#include <QSettings>
#include <QTimer>
#include <QVariantList>

// The Mac-style dock at the bottom of the open panel: pinned apps plus whatever is running.
// Linux: apps come from .desktop files, "running" from /proc (a GUI process whose binary matches an app).
// Windows: apps come from the Start Menu, running ones from the visible top-level windows.
// Pins live in notch/dock.conf next to the other settings.
class AppDock : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged) // {id, name, iconSource, pinned, running, active}

public:
    explicit AppDock(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QVariantList items() const { return m_items; }

    // Only poll for running apps while the panel is open.
    Q_INVOKABLE void setWatching(bool watching);
    Q_INVOKABLE void launch(const QString &id);
    Q_INVOKABLE void pin(const QString &id);
    Q_INVOKABLE void unpin(const QString &id);
    Q_INVOKABLE void movePinned(int from, int to);
    // Installed apps for the picker, filtered by name; {id, name, iconSource, pinned}.
    Q_INVOKABLE QVariantList installedApps(const QString &filter);

    QIcon iconFor(const QString &id) const;

signals:
    void enabledChanged();
    void itemsChanged();

private:
    void loadCatalog();
    void refresh();
    QList<DockApp> runningApps(QString *activeId);
    static QString iconSource(const QString &id);

    QSettings m_settings;
    bool m_enabled = true;
    QStringList m_pinned;
    QHash<QString, DockApp> m_catalog;
    QHash<QString, DockApp> m_seenRunning; // running apps outside the catalog (Windows), kept for their icon
    QList<DockApp> m_running;
    QString m_active;
    QVariantList m_items;
    QTimer m_timer;
    bool m_catalogLoaded = false;
#ifdef Q_OS_WIN
    QHash<QString, quintptr> m_windows; // app id -> window handle, to bring it forward
#endif
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
