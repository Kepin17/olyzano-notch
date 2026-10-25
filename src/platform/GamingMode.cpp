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
    m_state.autoDetect = m_settings.value(QStringLiteral("auto"), false).toBool();
#ifdef Q_OS_WIN
    m_poll.setInterval(2000); // Windows has no event for "a game went full screen"
    connect(&m_poll, &QTimer::timeout, this, &GamingMode::poll);
    if (m_state.autoDetect)
        m_poll.start();
#endif
}

void GamingMode::apply(const GamingModel &next)
{
    const bool was = m_state.active();
    const bool same = next.manual == m_state.manual && next.autoDetect == m_state.autoDetect;
    m_state = next;
    if (was != m_state.active() || !same)
        emit changed();
}

void GamingMode::setManual(bool on)
{
    GamingModel next = m_state;
    next.manual = on;
    apply(next);
}

void GamingMode::setAutoDetect(bool on)
{
    if (on == m_state.autoDetect)
        return;
    m_settings.setValue(QStringLiteral("auto"), on);
    GamingModel next = m_state;
    next.autoDetect = on;
    if (!on)
        next.suspended = false;
    apply(next);
#ifdef Q_OS_WIN
    on ? m_poll.start() : m_poll.stop();
    if (on)
        poll();
#endif
}

void GamingMode::setFullScreen(bool on)
{
    if (on == m_state.fullScreen)
        return;
    GamingModel next = m_state;
    next.setFullScreen(on);
    apply(next);
}

void GamingMode::wake()
{
    GamingModel next = m_state;
    next.wake();
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
