#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// The menu of the app in front, as `appMenu` in QML: the menu bar an app publishes on D-Bus (`com.canonical.dbusmenu`,
// what Chrome and Qt / KDE apps with a global menu do). The app is found by its process id: the bus names that process
// owns are searched for an object speaking dbusmenu. An app that publishes none simply has no menu here.
class AppMenu : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList menus READ menus NOTIFY changed) // the menu bar: [{id, label, enabled, children: [...]}]
    Q_PROPERTY(bool available READ available NOTIFY changed)

public:
    explicit AppMenu(QObject *parent = nullptr);

    QVariantList menus() const { return m_menus; }
    bool available() const { return !m_menus.isEmpty(); }

    void setPid(qint64 pid);                  // the app in front changed
    Q_INVOKABLE void trigger(int id);         // an item was clicked
    Q_INVOKABLE void aboutToShow(int id);     // a menu is about to open: apps fill some lazily
    // the same, for a menu at a known place (a tray item's)
    static QVariantList layoutAt(const QString &service, const QString &path) { return readMenus({service, path}); }
    static void clickAt(const QString &service, const QString &path, int id);
    static void aboutToShowAt(const QString &service, const QString &path, int id);
    QByteArray dump(qint64 pid);              // `notch --dump-menu <pid>`: what this finds, as JSON (blocks)

signals:
    void changed();

private slots:
    void onLayoutUpdated(uint, int) { m_reload.start(); }

private:
    struct Found {
        QString service, path;
        bool ok() const { return !service.isEmpty(); }
    };
    struct Result {
        qint64 pid = 0;
        Found found;
        QVariantList menus;
    };
    static Found discover(qint64 pid);
    static QVariantList readMenus(const Found &where);
    static Result load(qint64 pid, const Found &known);
    void start(qint64 pid);
    void done();
    void listen();

    qint64 m_pid = 0, m_asked = 0;
    Found m_found;
    QVariantList m_menus;
    QHash<qint64, Found> m_known; // per process, "none" included
    QFutureWatcher<Result> m_watch;
    QTimer m_reload;
};
