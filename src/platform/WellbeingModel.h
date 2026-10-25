#pragma once

#include <QtGlobal>

// The stretch reminder's bookkeeping, without timers or OS calls so tests can drive it: time spent actually
// working (input within the idle threshold) adds up; walking away for long enough counts as a break and
// starts over. When the work time reaches the limit the person is asked, and they choose: break, later, skip.
struct StretchTimer {
    enum class State { Working, Reminding, OnBreak };
    enum class Event { None, Remind, BreakOver, EyeBreak };

    qint64 workLimitMs = 50 * 60000;
    qint64 idleResetMs = 5 * 60000;  // idle this long = a break was taken
    qint64 breakMs = 5 * 60000;
    qint64 eyeIntervalMs = 0;        // 20-20-20 reminder every this much work; 0 = off

    State state = State::Working;
    qint64 workedMs = 0;    // since the last break
    qint64 eyeWorkedMs = 0; // since the last eye reminder
    qint64 snoozeUntil = 0;
    qint64 breakEndsAt = 0;

    // Called every few seconds. dt: ms since the previous tick; idleMs: time since the last input;
    // busy: a film, a call or a Pomodoro is on, so don't interrupt (the reminder waits until it's over).
    Event tick(qint64 now, qint64 dt, qint64 idleMs, bool busy)
    {
        dt = qBound(qint64(0), dt, qint64(15000)); // a suspended laptop isn't a long stretch of work
        if (state == State::OnBreak) {
            if (now < breakEndsAt)
                return Event::None;
            endBreak();
            return Event::BreakOver;
        }
        if (idleMs >= idleResetMs) {
            // Away from the computer long enough: that was the break (also answers a pending reminder).
            workedMs = 0;
            eyeWorkedMs = 0;
            snoozeUntil = 0;
            state = State::Working;
            return Event::None;
        }
        if (state == State::Reminding)
            return Event::None; // waiting for an answer
        workedMs += dt;
        eyeWorkedMs += dt;
        if (busy)
            return Event::None;
        if (workedMs >= workLimitMs && now >= snoozeUntil) {
            state = State::Reminding;
            return Event::Remind;
        }
        if (eyeIntervalMs > 0 && eyeWorkedMs >= eyeIntervalMs) {
            eyeWorkedMs = 0;
            return Event::EyeBreak;
        }
        return Event::None;
    }

    void startBreak(qint64 now)
    {
        state = State::OnBreak;
        breakEndsAt = now + breakMs;
    }
    void endBreak()
    {
        state = State::Working;
        workedMs = 0;
        eyeWorkedMs = 0;
        snoozeUntil = 0;
    }
    // "Later": ask again after `ms` more (of any time), keeping the work time so far.
    void snooze(qint64 now, qint64 ms)
    {
        state = State::Working;
        snoozeUntil = now + ms;
    }
    // "Skip": no break this time; the next reminder comes after a full work period.
    void skip()
    {
        state = State::Working;
        workedMs = 0;
        snoozeUntil = 0;
    }
};
