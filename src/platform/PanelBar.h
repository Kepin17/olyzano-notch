#pragma once

#include <QObject>
#include <QSettings>

// The status bar's settings, as `panel` in QML: a glass strip along the top edge with the app in front, the clock and
// the status icons, that can auto-hide or stay put. Settings live in notch/panel.conf.
class PanelBar : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString behavior READ behavior WRITE setBehavior NOTIFY changed) // "always" | "auto"
    Q_PROPERTY(bool reserve READ reserve WRITE setReserve NOTIFY changed)       // keep windows out from under it (always only)
    Q_PROPERTY(bool blur READ blur WRITE setBlur NOTIFY changed)
    Q_PROPERTY(double opacity READ opacity WRITE setOpacity NOTIFY changed)     // 0.2 - 1
    Q_PROPERTY(QString openOn READ openOn WRITE setOpenOn NOTIFY changed)       // Quick Settings: "click" | "hover"
    Q_PROPERTY(bool showApp READ showApp WRITE setShowApp NOTIFY changed)
    Q_PROPERTY(bool showMenu READ showMenu WRITE setShowMenu NOTIFY changed)   // the app's own menu (File, Edit, ...) beside its name
    Q_PROPERTY(bool showDate READ showDate WRITE setShowDate NOTIFY changed)
    Q_PROPERTY(bool showSeconds READ showSeconds WRITE setShowSeconds NOTIFY changed)
    Q_PROPERTY(bool showBattery READ showBattery WRITE setShowBattery NOTIFY changed)
    Q_PROPERTY(int height READ height CONSTANT) // the strip, in pixels

public:
    explicit PanelBar(QObject *parent = nullptr);

    bool enabled() const { return flag("enabled", false); }
    void setEnabled(bool on) { store("enabled", on); }
    QString behavior() const;
    void setBehavior(const QString &b);
    bool reserve() const { return flag("reserve", false); }
    void setReserve(bool on) { store("reserve", on); }
    bool blur() const { return flag("blur", true); }
    void setBlur(bool on) { store("blur", on); }
    double opacity() const;
    void setOpacity(double v);
    QString openOn() const;
    void setOpenOn(const QString &m);
    bool showApp() const { return flag("showApp", true); }
    void setShowApp(bool on) { store("showApp", on); }
    bool showMenu() const { return flag("showMenu", true); }
    void setShowMenu(bool on) { store("showMenu", on); }
    bool showDate() const { return flag("showDate", true); }
    void setShowDate(bool on) { store("showDate", on); }
    bool showSeconds() const { return flag("showSeconds", false); }
    void setShowSeconds(bool on) { store("showSeconds", on); }
    bool showBattery() const { return flag("showBattery", true); }
    void setShowBattery(bool on) { store("showBattery", on); }
    int height() const { return 36; }

signals:
    void changed();

private:
    bool flag(const char *key, bool def) const { return m_settings.value(QLatin1String(key), def).toBool(); }
    void store(const char *key, const QVariant &v);

    QSettings m_settings;
};
