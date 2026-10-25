#pragma once

#include "platform/GamingModel.h"

#include <QObject>
#include <QSettings>
#include <QTimer>

// Gaming mode, as `gaming` in QML. Switched on, it keeps Notch out of a full-screen game's way: while a full-screen
// app is in front the notch and the dock are hidden and ignore the pointer (nothing at the screen edges can be hit
// by accident), and sounds and reminders stay quiet. Minimise the game or run it in a window and Notch is back at
// once. The switch is remembered (notch/gaming.conf).
class GamingMode : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed) // enabled, and a full-screen app is in front

public:
    explicit GamingMode(QObject *parent = nullptr);

    bool enabled() const { return m_state.enabled; }
    bool active() const { return m_state.active(); }
    void setEnabled(bool on);

    // Linux: from the dock's window watcher. (Windows asks the system itself.)
    void setFullScreen(bool on);

signals:
    void changed();

private:
    void apply(const GamingModel &next);
    void poll();

    QSettings m_settings;
    GamingModel m_state;
    QTimer m_poll;
};
