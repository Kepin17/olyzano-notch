#include "platform/GamingMode.h"

#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/gaming.conf");
}
}

GamingMode::GamingMode(QObject *parent)
    : QObject(parent)
    , m_settings(configPath(), QSettings::IniFormat)
{
    m_state.enabled = m_settings.value(QStringLiteral("enabled"), false).toBool();
#ifdef Q_OS_WIN
    m_poll.setInterval(2000); // Windows has no event for "a game went full screen"
    connect(&m_poll, &QTimer::timeout, this, &GamingMode::poll);
    m_poll.start();
#endif
}

void GamingMode::apply(const GamingModel &next)
{
    const bool same = next.enabled == m_state.enabled && next.fullScreen == m_state.fullScreen;
    m_state = next;
    if (!same)
        emit changed();
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

void GamingMode::setFullScreen(bool on)
{
    GamingModel next = m_state;
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
