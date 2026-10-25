#pragma once

#include "platform/LauncherModel.h"

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantList>

class AppDock;

// The app launcher, as `launcher` in QML: the installed apps (from the dock's catalog) searched, grouped, pinned and
// ranked by use. Pins and counts live in notch/launcher.conf.
class Launcher : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.olyzano.Notch")
    Q_PROPERTY(QStringList pinned READ pinned NOTIFY changed)
    Q_PROPERTY(QStringList categories READ categories NOTIFY changed) // the groups that have apps, in a fixed order
    Q_PROPERTY(bool superKeyAvailable READ superKeyAvailable CONSTANT)  // KDE's settings tools are there
    Q_PROPERTY(bool superKey READ superKey WRITE setSuperKey NOTIFY superKeyChanged) // the Windows (Super) key opens this launcher

public:
    explicit Launcher(AppDock *dock, QObject *parent = nullptr);

    QStringList pinned() const { return m_pinned; }
    QStringList categories();

    // Apps matching `query` in `category` ("" = all), best first: [{id, name, iconSource, category, pinned}].
    bool superKeyAvailable() const;
    bool superKey() const;
    void setSuperKey(bool on);

    Q_INVOKABLE QVariantList search(const QString &query, const QString &category);
    Q_INVOKABLE QVariantList pinnedApps();
    Q_INVOKABLE QVariantList recentApps(int count);
    Q_INVOKABLE void launch(const QString &id); // opens it (or raises a running one) and counts the use
    Q_INVOKABLE void setPinned(const QString &id, bool pinned);

public slots:
    // org.olyzano.Notch /Launcher toggle: what KWin calls on the Super key, and what a script can call too.
    Q_SCRIPTABLE void toggle() { emit toggleRequested(); }

signals:
    void changed();
    void superKeyChanged();
    void toggleRequested();

private:
    QVariantList all();

    AppDock *m_dock;
    QSettings m_settings;
    QStringList m_pinned;
    QHash<QString, int> m_uses;
    QStringList m_recent; // newest first
};
