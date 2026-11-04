#include "platform/NotchPaths.h"
#include "platform/GamingMode.h"

#include "models/FileItem.h"

#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
QString configPath()
{
    return NotchPaths::conf(QStringLiteral("gaming"));
}
}

GamingMode::GamingMode(QObject *parent)
    : QObject(parent)
    , m_settings(configPath(), QSettings::IniFormat)
{
    m_state.enabled = m_settings.value(QStringLiteral("enabled"), false).toBool();
    m_state.hideInFullScreen = m_settings.value(QStringLiteral("hideInFullScreen"), false).toBool();
#ifdef Q_OS_WIN
    m_poll.setInterval(2000); // Windows has no event for "a game went full screen"
    connect(&m_poll, &QTimer::timeout, this, &GamingMode::poll);
    m_poll.start();
#endif
}

void GamingMode::apply(const GamingModel &next)
{
    const bool same = next.enabled == m_state.enabled && next.fullScreen == m_state.fullScreen && next.hideInFullScreen == m_state.hideInFullScreen
                      && next.revealed == m_state.revealed;
    m_state = next;
    if (!same) {
        qCInfo(lcNotch) << "Gaming mode: switch" << (m_state.enabled ? "on" : "off") << "- hide in full screen:" << (m_state.hideInFullScreen ? "on" : "off")
                        << "- full-screen app in front:" << (m_state.fullScreen ? "yes" : "no") << "- Notch kept away:" << (m_state.active() ? "yes" : "no");
        emit changed();
    }
}

void GamingMode::setEnabled(bool on)
{
    if (on == m_state.enabled)
        return;
    m_settings.setValue(QStringLiteral("enabled"), on);
    GamingModel next = m_state;
    next.enabled = on;
    apply(next);
}

void GamingMode::setHideInFullScreen(bool on)
{
    if (on == m_state.hideInFullScreen)
        return;
    m_settings.setValue(QStringLiteral("hideInFullScreen"), on);
    GamingModel next = m_state;
    next.hideInFullScreen = on;
    next.revealed = false;
    apply(next);
}

void GamingMode::reveal(bool on)
{
    GamingModel next = m_state;
    next.revealed = on;
    apply(next);
}

void GamingMode::setFullScreen(bool on)
{
    GamingModel next = m_state;
    if (on != next.fullScreen)
        next.revealed = false; // another full-screen app, or none: hidden again, as the preference says
    next.fullScreen = on;
    apply(next);
}

void GamingMode::poll()
{
#ifdef Q_OS_WIN
    QUERY_USER_NOTIFICATION_STATE state;
    if (SUCCEEDED(SHQueryUserNotificationState(&state)))
        setFullScreen(state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE);
#endif
}
