#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QStringList>
#include <QVariantList>

#ifdef NOTCH_HAVE_DBUS
#include <QDBusContext>
#else
class QDBusContext {};
#endif

// The tray, as `trayHost` in QML: the apps that live in the background (Steam, Discord, a screen share...) publish a
// StatusNotifierItem on D-Bus; Notch registers as one more host next to Plasma's and lists them on the status bar.
// Left click activates the app, right click opens its menu (`menu` / `trigger`, which AppMenuPopup uses like `appMenu`).
class TrayHost : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed) // [{key, id, title, status, icon (image://tray/...), isMenu}]

public:
    explicit TrayHost(QObject *parent = nullptr);

    QVariantList items() const;
    QPixmap pixmap(const QString &key, const QSize &size) const;

    Q_INVOKABLE void activate(const QString &key, int x, int y);
    Q_INVOKABLE void secondary(const QString &key, int x, int y);
    Q_INVOKABLE QVariantList menu(const QString &key); // reads the app's menu; `trigger` / `aboutToShow` then act on it
    Q_INVOKABLE void trigger(int id);
    Q_INVOKABLE void aboutToShow(int id);
    QByteArray dump() const; // `notch --dump-tray`

signals:
    void changed();

private slots:
    void onRegistered(const QString &entry) { add(entry); }
    void onUnregistered(const QString &entry);
    void onItemSignal();

private:
    struct Item {
        QString entry, service, path, owner, id, title, status, iconName, themePath, menuPath;
        QImage image;
        bool isMenu = false;
        int rev = 0;
    };
    void add(const QString &entry);
    void read(const QString &key);
    const Item *find(const QString &key) const;

    QHash<QString, Item> m_items; // by key (a counter)
    QStringList m_order;
    int m_next = 1;
    QString m_menuKey;
};
