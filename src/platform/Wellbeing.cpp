#include "platform/Wellbeing.h"

#include <QDate>
#include <limits>
#include <QDateTime>
#include <QStandardPaths>

#ifdef NOTCH_HAVE_KIDLETIME
#include <KIdleTime>
#endif
#ifdef NOTCH_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

static QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/wellbeing.conf");
}

Wellbeing::Wellbeing(QObject *parent)
    : QObject(parent), m_settings(configPath(), QSettings::IniFormat)
{
    applySettings();
    m_poll.setInterval(5000);
    connect(&m_poll, &QTimer::timeout, this, &Wellbeing::onTick);
    m_answerTimeout.setSingleShot(true);
    m_answerTimeout.setInterval(60000);
    connect(&m_answerTimeout, &QTimer::timeout, this, &Wellbeing::later);
    m_today = m_settings.value(QStringLiteral("today/date")).toString();
    rollDay();
    m_todayWorked = m_settings.value(QStringLiteral("today/worked"), 0).toLongLong();
    m_todayBreaks = m_settings.value(QStringLiteral("today/breaks"), 0).toInt();
    m_sinceTick.start();
    if (enabled())
        m_poll.start();
}

QString Wellbeing::state() const
{
    switch (m_timer.state) {
    case StretchTimer::State::Reminding: return QStringLiteral("reminding");
    case StretchTimer::State::OnBreak: return QStringLiteral("break");
    default: return QStringLiteral("working");
    }
}

QStringList Wellbeing::tips() const
{
    return {QStringLiteral("Roll your shoulders back, slowly, ten times"),
            QStringLiteral("Look at something far away for 20 seconds"),
            QStringLiteral("Stand up and reach for the ceiling"),
            QStringLiteral("Tilt your head gently to each side"),
            QStringLiteral("Stretch your wrists and open your hands wide"),
            QStringLiteral("Walk a few steps and drink some water"),
            QStringLiteral("Twist gently left and right from your waist"),
            QStringLiteral("Close your eyes and take three deep breaths")};
}

void Wellbeing::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

// ---- Settings ----

void Wellbeing::store(const QString &key, const QVariant &value)
{
    if (m_settings.value(key) == value)
        return;
    m_settings.setValue(key, value);
    applySettings();
    emit settingsChanged();
}

void Wellbeing::applySettings()
{
    m_timer.workLimitMs = qint64(workMinutes()) * 60000;
    m_timer.breakMs = qint64(breakMinutes()) * 60000;
    m_timer.idleResetMs = qint64(idleMinutes()) * 60000;
    m_timer.eyeIntervalMs = eyeReminder() ? 20 * 60000 : 0;
}

void Wellbeing::setEnabled(bool on)
{
    store(QStringLiteral("enabled"), on);
    if (on) {
        m_sinceTick.restart();
        m_poll.start();
        return;
    }
    m_poll.stop();
    m_answerTimeout.stop();
    m_timer.endBreak(); // nothing pending once it's off
    emit stateChanged();
}

void Wellbeing::setWorkMinutes(int minutes) { store(QStringLiteral("work"), qBound(10, minutes, 240)); }
void Wellbeing::setBreakMinutes(int minutes) { store(QStringLiteral("break"), qBound(1, minutes, 30)); }
void Wellbeing::setIdleMinutes(int minutes) { store(QStringLiteral("idle"), qBound(1, minutes, 30)); }
void Wellbeing::setSnoozeMinutes(int minutes) { store(QStringLiteral("snooze"), qBound(1, minutes, 60)); }
void Wellbeing::setEyeReminder(bool on) { store(QStringLiteral("eyes"), on); }
void Wellbeing::setPauseForMedia(bool on) { store(QStringLiteral("pauseMedia"), on); }
void Wellbeing::setPauseForCalls(bool on) { store(QStringLiteral("pauseCalls"), on); }
void Wellbeing::setPauseForPomodoro(bool on) { store(QStringLiteral("pausePomodoro"), on); }

// ---- Answers ----

void Wellbeing::startBreak()
{
    m_answerTimeout.stop();
    m_timer.startBreak(QDateTime::currentMSecsSinceEpoch());
    m_breakEndsAtEpoch = m_timer.breakEndsAt;
    emit stateChanged();
}

void Wellbeing::later()
{
    if (m_timer.state != StretchTimer::State::Reminding)
        return;
    m_answerTimeout.stop();
    m_timer.snooze(QDateTime::currentMSecsSinceEpoch(), qint64(snoozeMinutes()) * 60000);
    emit stateChanged();
}

void Wellbeing::skip()
{
    m_answerTimeout.stop();
    m_timer.skip();
    emit stateChanged();
}

void Wellbeing::endBreak()
{
    if (m_timer.state != StretchTimer::State::OnBreak)
        return;
    m_timer.endBreak();
    ++m_todayBreaks;
    saveToday();
    emit stateChanged();
    emit tick();
}

// ---- Ticking ----

void Wellbeing::onTick()
{
    rollDay();
    const qint64 dt = m_sinceTick.restart();
    m_idleMs = systemIdleMs();
    const qint64 before = m_timer.workedMs;
    const StretchTimer::State was = m_timer.state;
    // Without an idle time from the system, every tick counts as work (reminders by the clock).
    const StretchTimer::Event event = m_timer.tick(QDateTime::currentMSecsSinceEpoch(), dt, qMax<qint64>(0, m_idleMs), m_busy);
    if (m_timer.workedMs > before)
        m_todayWorked += m_timer.workedMs - before;

    switch (event) {
    case StretchTimer::Event::Remind:
        m_answerTimeout.start();
        emit reminder(int(m_timer.workedMs / 60000));
        break;
    case StretchTimer::Event::BreakOver:
        ++m_todayBreaks;
        emit breakOver();
        break;
    case StretchTimer::Event::EyeBreak:
        emit eyeBreak();
        break;
    case StretchTimer::Event::None:
        break;
    }
    if (m_timer.state != was) {
        if (was == StretchTimer::State::Reminding)
            m_answerTimeout.stop(); // answered by walking away
        emit stateChanged();
    }
    saveToday();
    emit tick();
}

qint64 Wellbeing::systemIdleMs() const
{
#ifdef NOTCH_HAVE_KIDLETIME
    return KIdleTime::instance()->idleTime(); // Wayland (ext-idle-notify / KWin) and X11
#elif defined(NOTCH_HAVE_DBUS)
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto call = [&](const char *method) {
        const QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
                                                                QStringLiteral("org.freedesktop.ScreenSaver"), QString::fromLatin1(method));
        const QDBusMessage reply = bus.call(msg, QDBus::Block, 1000);
        return reply.type() == QDBusMessage::ReplyMessage ? reply.arguments().value(0) : QVariant();
    };
    if (call("GetActive").toBool())
        return std::numeric_limits<qint32>::max(); // locked screen: away
    const QVariant seconds = call("GetSessionIdleTime");
    return seconds.isValid() ? seconds.toLongLong() * 1000 : -1;
#elif defined(Q_OS_WIN)
    LASTINPUTINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info))
        return -1;
    return qint64(GetTickCount() - info.dwTime);
#else
    return -1;
#endif
}

bool Wellbeing::idleAvailable() const
{
    return systemIdleMs() >= 0;
}

void Wellbeing::rollDay()
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (today == m_today)
        return;
    m_today = today;
    m_todayWorked = 0;
    m_todayBreaks = 0;
    saveToday();
}

void Wellbeing::saveToday()
{
    m_settings.setValue(QStringLiteral("today/date"), m_today);
    m_settings.setValue(QStringLiteral("today/worked"), m_todayWorked);
    m_settings.setValue(QStringLiteral("today/breaks"), m_todayBreaks);
}
