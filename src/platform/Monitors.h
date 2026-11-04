#pragma once

#include <QObject>
#include <QSet>
#include <QSettings>
#include <QVariantList>

class QScreen;

// The connected monitors, and which of them show Notch (Settings → General → Monitors). A monitor is known by its name (eDP-1,
// HDMI-A-1; \\.\DISPLAY1 on Windows) and shows Notch unless it was switched off; a new one shows it. Follows plugging and unplugging.
class Monitors : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList screens READ screens NOTIFY changed) // [{name, label, x, y, w, h, primary, shown}]
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    explicit Monitors(QObject *parent = nullptr);

    QVariantList screens() const;
    int count() const;
    bool shown(const QScreen *screen) const;
    Q_INVOKABLE bool setShown(const QString &name, bool on); // false: refused, it is the last monitor that shows Notch

    // What this monitor does differently from the general settings: {scale, mode, dock, bar}, each 0 = as general (MonitorModel).
    Q_INVOKABLE QVariantMap options(const QString &name) const;
    Q_INVOKABLE void setOption(const QString &name, const QString &key, int value);
    int option(const QString &name, const QString &key) const;

signals:
    void changed();
    void rejected(const QString &name);

private:
    void watch(QScreen *screen);
    QStringList connectedNames() const;

    QSettings m_settings;
    QSet<QString> m_disabled;
};
