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
    Q_PROPERTY(bool searchEnabled READ searchEnabled WRITE setSearchEnabled NOTIFY searchEnabledChanged) // the search in the notch (Shift + Space)
    Q_PROPERTY(QString searchKey READ searchKey WRITE setSearchKey NOTIFY searchEnabledChanged)       // "Shift+Space": Qt's portable text, Meta is the Windows key
    Q_PROPERTY(QString searchKeyLabel READ searchKeyLabel NOTIFY searchEnabledChanged)                // "Shift + Space", for the screen
    Q_PROPERTY(bool searchKeyIsDefault READ searchKeyIsDefault NOTIFY searchEnabledChanged)
    Q_PROPERTY(QString searchNote READ searchNote NOTIFY searchEnabledChanged) // "" or why the shortcut does not work (Windows: another program has Shift + Space)
    Q_PROPERTY(QStringList pinned READ pinned NOTIFY changed)
    Q_PROPERTY(QStringList categories READ categories NOTIFY changed) // the groups that have apps, in a fixed order
    Q_PROPERTY(bool superKeyAvailable READ superKeyAvailable CONSTANT)  // KDE's settings tools are there
    Q_PROPERTY(QString superKeyStatus READ superKeyStatus NOTIFY superKeyChanged) // "notch" | "plasma" | "other" | "unavailable"
    Q_PROPERTY(bool superKey READ superKey WRITE setSuperKey NOTIFY superKeyChanged) // the Windows (Super) key opens this launcher

public:
    explicit Launcher(AppDock *dock, QObject *parent = nullptr);

    QStringList pinned() const { return m_pinned; }
    QStringList categories();

    // Apps matching `query` in `category` ("" = all), best first: [{id, name, iconSource, category, pinned}].
    bool searchEnabled() const { return m_settings.value(QStringLiteral("searchEnabled"), true).toBool(); }
    void setSearchEnabled(bool on);
    QString searchKey() const;
    void setSearchKey(const QString &key);
    QString searchKeyLabel() const;
    bool searchKeyIsDefault() const { return searchKey() == QLatin1String("Shift+Space"); }
    Q_INVOKABLE QString describeKey(int key, int modifiers) const; // a key press as text for setSearchKey; "" when it cannot be a shortcut (no modifier, or only a modifier)
    Q_INVOKABLE void resetSearchKey() { setSearchKey(QStringLiteral("Shift+Space")); }
    QString searchNote() const { return m_searchNote; }
    void setSearchNote(const QString &note) { if (note != m_searchNote) { m_searchNote = note; emit searchEnabledChanged(); } }
    bool superKeyAvailable() const;
    bool superKey() const;
    QString superKeyStatus() const; // who the Windows key goes to now, as KWin has it
    QByteArray superKeyReport() const; // `notch --super-key status`
    void setSuperKey(bool on);

    Q_INVOKABLE QVariantList search(const QString &query, const QString &category);
    Q_INVOKABLE QVariantList pinnedApps();
    Q_INVOKABLE QVariantList recentApps(int count);
    int uses(const QString &id) const { return m_uses.value(id); } // how often it was opened through the launcher or by voice
    Q_INVOKABLE void launch(const QString &id); // opens it (or raises a running one) and counts the use
    Q_INVOKABLE void setPinned(const QString &id, bool pinned);

public slots:
    // org.olyzano.Notch /Launcher toggle: what KWin calls on the Super key, and what a script can call too.
    Q_SCRIPTABLE void toggle() { emit toggleRequested(); }
    // org.olyzano.Notch /Launcher openSearch: the shortcut's call; the search drops down from the notch.
    Q_SCRIPTABLE void openSearch() { if (searchEnabled()) emit searchRequested(); }

signals:
    void changed();
    void superKeyChanged();
    void toggleRequested();
    void searchRequested();
    void searchEnabledChanged();

private:
    QVariantList all();

    AppDock *m_dock;
    QSettings m_settings;
    QStringList m_pinned;
    QHash<QString, int> m_uses;
    QStringList m_recent; // newest first
    QString m_searchNote;
};
