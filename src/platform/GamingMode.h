#pragma once

#include "platform/GamingModel.h"

#include <QObject>
#include <QSettings>
#include <QTimer>

// Gaming mode, as `gaming` in QML: while it is on the notch and the dock are hidden and click-through (nothing at
// the screen edges can be hit by accident), and the sounds and reminders stay quiet. The tray icon stays: it
// wakes Notch up. On by hand (not remembered across starts, so a forgotten switch can't make Notch look dead) or,
// if `autoDetect` is on (a setting, notch/gaming.conf), while a full-screen app or game is in front.
class GamingMode : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool manual READ manual WRITE setManual NOTIFY changed)
    Q_PROPERTY(bool autoDetect READ autoDetect WRITE setAutoDetect NOTIFY changed)
    Q_PROPERTY(bool fullScreen READ fullScreen NOTIFY changed) // a full-screen app is in front

public:
    explicit GamingMode(QObject *parent = nullptr);

    bool active() const { return m_state.active(); }
    bool manual() const { return m_state.manual; }
    bool autoDetect() const { return m_state.autoDetect; }
    bool fullScreen() const { return m_state.fullScreen; }
    void setManual(bool on);
    void setAutoDetect(bool on);

    // Linux: from the dock's window watcher. (Windows asks the system itself.)
    void setFullScreen(bool on);
    // The tray icon: leave gaming mode. True when Notch may open now; false while a full-screen app is in front
    // (the panel can't be opened over it, so the tray does nothing).
    Q_INVOKABLE bool wake();

signals:
    void changed();

private:
    void apply(const GamingModel &next);
    void poll();

    QSettings m_settings;
    GamingModel m_state;
    QTimer m_poll;
};
