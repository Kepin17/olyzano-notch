#include "SystemEvents.h"

#ifndef NOTCH_HAVE_DBUS

#include "platform/BatteryThreshold.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

SystemEvents::SystemEvents(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    m_timer.setInterval(15000);
    connect(&m_timer, &QTimer::timeout, this, &SystemEvents::poll);
    m_timer.start();
    poll(); // learn the starting state without raising an alert
#endif
}

// Windows: GetSystemPowerStatus. A desktop PC reports no battery (flag 128), so nothing happens there.
void SystemEvents::poll()
{
#ifdef Q_OS_WIN
    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s) || (s.BatteryFlag & 128) || s.BatteryLifePercent > 100) {
        if (m_percent != -1) {
            m_percent = -1;
            emit batteryChanged();
        }
        return;
    }
    const bool mains = s.ACLineStatus == 1;
    const int percent = s.BatteryLifePercent;
    if (m_charging >= 0 && mains != bool(m_charging)) {
        emit alert(QStringLiteral("charging"), mains ? tr("Charging") : tr("On battery"),
                   tr("%1%").arg(percent), QStringLiteral("battery"), percent);
    } else if (!mains && m_percent >= 0) {
        if (const int level = crossedLowBattery(m_percent, percent))
            emit alert(QStringLiteral("lowbattery"), tr("Battery low"), tr("%1% left").arg(level),
                       QStringLiteral("battery-low"), percent);
    }
    const bool changed = m_charging != int(mains) || m_percent != percent;
    m_charging = mains;
    m_percent = percent;
    if (changed)
        emit batteryChanged();
#endif
}

#endif
