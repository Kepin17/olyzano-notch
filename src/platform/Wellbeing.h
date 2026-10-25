#pragma once

#include "platform/WellbeingModel.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTimer>

// The stretch reminder, as `wellbeing` in QML. It counts the time actually spent working, from the
// system's idle time (KDE: KIdleTime, else the ScreenSaver D-Bus API; Windows: GetLastInputInfo), and
// asks for a break when it reaches the limit. The person decides: start the break, later, or skip.
// QML sets `busy` while a film, a call or a Pomodoro is on, which holds the reminder back.
// Settings and today's totals live in notch/wellbeing.conf.
class Wellbeing : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)               // working | reminding | break
    Q_PROPERTY(qint64 workedMs READ workedMs NOTIFY tick)                  // since the last break
    Q_PROPERTY(qint64 breakEndsAt READ breakEndsAt NOTIFY stateChanged)    // epoch ms, while on a break
    Q_PROPERTY(bool userActive READ userActive NOTIFY tick)                // input in the last few seconds
    Q_PROPERTY(bool idleAvailable READ idleAvailable CONSTANT)             // the system tells us its idle time
    Q_PROPERTY(bool busy READ busy WRITE setBusy NOTIFY busyChanged)
    Q_PROPERTY(QStringList tips READ tips CONSTANT)                        // stretches suggested during a break
    Q_PROPERTY(qint64 todayWorkedMs READ todayWorkedMs NOTIFY tick)
    Q_PROPERTY(int todayBreaks READ todayBreaks NOTIFY tick)
    // Settings
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int workMinutes READ workMinutes WRITE setWorkMinutes NOTIFY settingsChanged)
    Q_PROPERTY(int breakMinutes READ breakMinutes WRITE setBreakMinutes NOTIFY settingsChanged)
    Q_PROPERTY(int idleMinutes READ idleMinutes WRITE setIdleMinutes NOTIFY settingsChanged)
    Q_PROPERTY(int snoozeMinutes READ snoozeMinutes WRITE setSnoozeMinutes NOTIFY settingsChanged)
    Q_PROPERTY(bool eyeReminder READ eyeReminder WRITE setEyeReminder NOTIFY settingsChanged)
    Q_PROPERTY(bool pauseForMedia READ pauseForMedia WRITE setPauseForMedia NOTIFY settingsChanged)
    Q_PROPERTY(bool pauseForCalls READ pauseForCalls WRITE setPauseForCalls NOTIFY settingsChanged)
    Q_PROPERTY(bool pauseForPomodoro READ pauseForPomodoro WRITE setPauseForPomodoro NOTIFY settingsChanged)

public:
    explicit Wellbeing(QObject *parent = nullptr);

    QString state() const;
    qint64 workedMs() const { return m_timer.workedMs; }
    qint64 breakEndsAt() const { return m_timer.state == StretchTimer::State::OnBreak ? m_breakEndsAtEpoch : 0; }
    bool userActive() const { return m_idleMs >= 0 && m_idleMs < 5000; }
    bool idleAvailable() const;
    bool busy() const { return m_busy; }
    void setBusy(bool busy);
    QStringList tips() const;
    qint64 todayWorkedMs() const { return m_todayWorked; }
    int todayBreaks() const { return m_todayBreaks; }

    bool enabled() const { return m_settings.value(QStringLiteral("enabled"), true).toBool(); }
    void setEnabled(bool on);
    int workMinutes() const { return m_settings.value(QStringLiteral("work"), 50).toInt(); }
    void setWorkMinutes(int minutes);
    int breakMinutes() const { return m_settings.value(QStringLiteral("break"), 5).toInt(); }
    void setBreakMinutes(int minutes);
    int idleMinutes() const { return m_settings.value(QStringLiteral("idle"), 5).toInt(); }
    void setIdleMinutes(int minutes);
    int snoozeMinutes() const { return m_settings.value(QStringLiteral("snooze"), 10).toInt(); }
    void setSnoozeMinutes(int minutes);
    bool eyeReminder() const { return m_settings.value(QStringLiteral("eyes"), false).toBool(); }
    void setEyeReminder(bool on);
    bool pauseForMedia() const { return m_settings.value(QStringLiteral("pauseMedia"), true).toBool(); }
    void setPauseForMedia(bool on);
    bool pauseForCalls() const { return m_settings.value(QStringLiteral("pauseCalls"), true).toBool(); }
    void setPauseForCalls(bool on);
    bool pauseForPomodoro() const { return m_settings.value(QStringLiteral("pausePomodoro"), true).toBool(); }
    void setPauseForPomodoro(bool on);

    // The answers to a reminder (and the break's own button).
    Q_INVOKABLE void startBreak();
    Q_INVOKABLE void later();
    Q_INVOKABLE void skip();
    Q_INVOKABLE void endBreak();

signals:
    void stateChanged();
    void tick();
    void busyChanged();
    void settingsChanged();
    void reminder(int workedMinutes);
    void breakOver();
    void eyeBreak();

private:
    void store(const QString &key, const QVariant &value);
    void applySettings();
    void onTick();
    qint64 systemIdleMs() const; // -1 when unknown
    void rollDay();
    void saveToday();

    QSettings m_settings;
    StretchTimer m_timer;
    QTimer m_poll;
    QTimer m_answerTimeout; // an unanswered reminder counts as "later"
    QElapsedTimer m_sinceTick;
    qint64 m_idleMs = -1;
    qint64 m_breakEndsAtEpoch = 0;
    bool m_busy = false;
    QString m_today;
    qint64 m_todayWorked = 0;
    int m_todayBreaks = 0;
};
