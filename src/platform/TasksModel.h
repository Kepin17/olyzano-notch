#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLocale>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTime>
#include <algorithm>

// Tasks with a deadline, without timers or files so tests can drive it: reading "Buy milk tomorrow 17:00", the order
// of the list, what to say about the time left, and which reminder is due now. Tasks.cpp keeps the file and the timer.
namespace TasksModel {

struct Task {
    QString id, title;
    QString course;              // free-text course / subject ("" = none)
    QDateTime due;               // invalid: no deadline
    bool done = false;
    bool warned = false;         // the "soon" reminder was shown
    bool notified = false;       // the "now" reminder was shown
};

constexpr int SoonMinutes = 15;  // the warning comes this long before the deadline
constexpr int StaleHours = 1;    // a deadline missed by more than this while Notch was closed is not announced
constexpr int CriticalHours = 3; // a deadline this close is shown as critical (red) before the 15-minute heads-up

enum class DueState { None, Later, Critical, Soon, Overdue };

// "17:30", "tomorrow", "tomorrow 9:00", "today 18:00", "fri", "fri 17:00", "12 nov", "12/11 17:00", "2026-11-02 09:00", "in 30m", "in 2h", "in 3d".
// A date without a time means 09:00; a bare time that has already passed today means tomorrow. Invalid when unclear.
inline QDateTime parseDue(const QString &text, const QDateTime &now)
{
    const QString t = text.trimmed().toLower();
    static const QRegularExpression timeRe(QStringLiteral(R"(^(\d{1,2})[:.](\d{2})$)"));
    static const QRegularExpression inRe(QStringLiteral(R"(^in\s+(\d{1,3})\s*(m|min|mins|h|hr|hrs|d|day|days)$)"));
    static const QRegularExpression dayTimeRe(QStringLiteral(R"(^(today|tomorrow)(?:\s+(\d{1,2})[:.](\d{2}))?$)"));
    static const QRegularExpression isoRe(QStringLiteral(R"(^(\d{4}-\d{2}-\d{2})(?:[ t](\d{1,2})[:.](\d{2}))?$)"));
    const auto atTime = [](const QDate &d, int h, int m) {
        return h < 24 && m < 60 ? QDateTime(d, QTime(h, m)) : QDateTime();
    };
    if (auto m = timeRe.match(t); m.hasMatch()) {
        QDateTime d = atTime(now.date(), m.captured(1).toInt(), m.captured(2).toInt());
        if (d.isValid() && d <= now)
            d = d.addDays(1);
        return d;
    }
    if (auto m = inRe.match(t); m.hasMatch()) {
        const int n = m.captured(1).toInt();
        const QString u = m.captured(2);
        return u.startsWith(QLatin1Char('m')) ? now.addSecs(n * 60LL) : u.startsWith(QLatin1Char('h')) ? now.addSecs(n * 3600LL) : now.addDays(n);
    }
    if (auto m = dayTimeRe.match(t); m.hasMatch()) {
        const QDate day = m.captured(1) == QLatin1String("today") ? now.date() : now.date().addDays(1);
        return m.captured(2).isEmpty() ? atTime(day, 9, 0) : atTime(day, m.captured(2).toInt(), m.captured(3).toInt());
    }
    static const QRegularExpression weekdayRe(QStringLiteral(R"(^(mon|tue|wed|thu|fri|sat|sun)[a-z]*(?:\s+(\d{1,2})[:.](\d{2}))?$)"));
    if (auto m = weekdayRe.match(t); m.hasMatch()) { // the next such day (a week away when it is today)
        const int want = QStringList{"mon", "tue", "wed", "thu", "fri", "sat", "sun"}.indexOf(m.captured(1)) + 1;
        const QDate day = now.date().addDays((want - now.date().dayOfWeek() + 6) % 7 + 1);
        return m.captured(2).isEmpty() ? atTime(day, 9, 0) : atTime(day, m.captured(2).toInt(), m.captured(3).toInt());
    }
    static const QRegularExpression dmyRe(QStringLiteral(R"(^(\d{1,2})[/ -](\d{1,2}|[a-z]{3})(?:[/ -](\d{4}))?(?:\s+(\d{1,2})[:.](\d{2}))?$)"));
    if (auto m = dmyRe.match(t); m.hasMatch()) { // 12/11, 12/11/2026, 12 nov, 12 nov 17:00
        static const QStringList months{"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
        const int month = m.captured(2).at(0).isDigit() ? m.captured(2).toInt() : months.indexOf(m.captured(2)) + 1;
        const int dayNo = m.captured(1).toInt();
        int year = m.captured(3).isEmpty() ? now.date().year() : m.captured(3).toInt();
        QDate day(year, month, dayNo);
        if (day.isValid() && m.captured(3).isEmpty() && day < now.date())
            day = QDate(year + 1, month, dayNo);
        if (!day.isValid())
            return {};
        return m.captured(4).isEmpty() ? atTime(day, 9, 0) : atTime(day, m.captured(4).toInt(), m.captured(5).toInt());
    }
    if (auto m = isoRe.match(t); m.hasMatch()) {
        const QDate day = QDate::fromString(m.captured(1), Qt::ISODate);
        if (!day.isValid())
            return {};
        return m.captured(2).isEmpty() ? atTime(day, 9, 0) : atTime(day, m.captured(2).toInt(), m.captured(3).toInt());
    }
    return {};
}

struct Quick { QString title; QDateTime due; };

// One line from the add field: the last one to three words that read as a time become the deadline, the rest the title.
// "Buy milk @ tomorrow 17:00" works too (everything after the @).
inline Quick parseQuick(const QString &line, const QDateTime &now)
{
    const QString s = line.simplified();
    if (const int at = s.lastIndexOf(QLatin1Char('@')); at > 0) {
        const QDateTime d = parseDue(s.mid(at + 1), now);
        if (d.isValid())
            return {s.left(at).trimmed(), d};
    }
    const QStringList words = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int n = qMin(3, int(words.size()) - 1); n >= 1; --n) {
        const QDateTime d = parseDue(words.mid(words.size() - n).join(QLatin1Char(' ')), now);
        if (d.isValid())
            return {words.mid(0, words.size() - n).join(QLatin1Char(' ')), d};
    }
    return {s, {}};
}

inline DueState dueState(const Task &t, const QDateTime &now)
{
    if (t.done || !t.due.isValid())
        return DueState::None;
    if (t.due <= now)
        return DueState::Overdue;
    if (t.due <= now.addSecs(SoonMinutes * 60))
        return DueState::Soon;
    if (t.due <= now.addSecs(CriticalHours * 3600))
        return DueState::Critical;
    return DueState::Later;
}

// Open tasks first, nearest deadline first, those without one after them; done ones last.
inline QList<Task> sorted(QList<Task> list)
{
    std::stable_sort(list.begin(), list.end(), [](const Task &a, const Task &b) {
        if (a.done != b.done)
            return !a.done;
        if (a.due.isValid() != b.due.isValid())
            return a.due.isValid();
        return a.due.isValid() && a.due < b.due;
    });
    return list;
}

// The task the capsule should name: the open one with the earliest deadline (an overdue one counts).
inline int nextOpen(const QList<Task> &list)
{
    int best = -1;
    for (int i = 0; i < list.size(); ++i)
        if (!list[i].done && list[i].due.isValid() && (best < 0 || list[i].due < list[best].due))
            best = i;
    return best;
}

inline int overdueCount(const QList<Task> &list, const QDateTime &now)
{
    int n = 0;
    for (const Task &t : list)
        n += dueState(t, now) == DueState::Overdue;
    return n;
}

// "in 25 min", "in 3 h", "today 18:00", "tomorrow 09:00", "Mon 09:00", "12 Nov 09:00", "overdue 2 h".
inline QString remaining(const QDateTime &due, const QDateTime &now)
{
    if (!due.isValid())
        return {};
    const qint64 secs = now.secsTo(due);
    const qint64 mins = qAbs(secs) / 60;
    const auto span = [](qint64 m) {
        return m < 1 ? QStringLiteral("a moment") : m < 60 ? QStringLiteral("%1 min").arg(m)
             : m < 24 * 60 ? QStringLiteral("%1 h").arg(m / 60) : QStringLiteral("%1 d").arg(m / (24 * 60));
    };
    if (secs <= 0)
        return QStringLiteral("overdue %1").arg(span(mins));
    if (mins < 180)
        return QStringLiteral("in %1").arg(span(mins));
    const QString time = due.time().toString(QStringLiteral("HH:mm"));
    const qint64 days = now.date().daysTo(due.date());
    if (days == 0)
        return QStringLiteral("today %1").arg(time);
    if (days == 1)
        return QStringLiteral("tomorrow %1").arg(time);
    if (days < 7)
        return QStringLiteral("%1 %2").arg(QLocale::c().dayName(due.date().dayOfWeek(), QLocale::ShortFormat), time);
    return QStringLiteral("%1 %2").arg(QLocale::c().toString(due.date(), QStringLiteral("d MMM")), time);
}

struct Event { QString id, title; bool now = false; }; // now: the deadline itself, else the heads-up before it

// Flips the "shown" flags and says which reminders to show right now. A deadline long gone (Notch was closed) is
// marked without an announcement.
inline QList<Event> fire(QList<Task> &list, const QDateTime &now)
{
    QList<Event> out;
    for (Task &t : list) {
        if (t.done || !t.due.isValid())
            continue;
        if (!t.notified && t.due <= now) {
            t.notified = t.warned = true;
            if (t.due.secsTo(now) <= StaleHours * 3600)
                out << Event{t.id, t.title, true};
        } else if (!t.warned && t.due > now && t.due <= now.addSecs(SoonMinutes * 60)) {
            t.warned = true;
            out << Event{t.id, t.title, false};
        }
    }
    return out;
}

// When fire() next has something to say; invalid when there is nothing left to wait for.
inline QDateTime nextWake(const QList<Task> &list, const QDateTime &now)
{
    QDateTime best;
    const auto consider = [&](const QDateTime &d) {
        if (d > now && (!best.isValid() || d < best))
            best = d;
    };
    for (const Task &t : list) {
        if (t.done || !t.due.isValid())
            continue;
        if (!t.warned)
            consider(t.due.addSecs(-SoonMinutes * 60));
        if (!t.notified)
            consider(t.due);
    }
    return best;
}

inline QByteArray toJson(const QList<Task> &list)
{
    QJsonArray a;
    for (const Task &t : list)
        a.append(QJsonObject{{QStringLiteral("id"), t.id}, {QStringLiteral("title"), t.title},
                             {QStringLiteral("course"), t.course},
                             {QStringLiteral("due"), t.due.isValid() ? t.due.toString(Qt::ISODate) : QString()},
                             {QStringLiteral("done"), t.done}, {QStringLiteral("warned"), t.warned},
                             {QStringLiteral("notified"), t.notified}});
    return QJsonDocument(a).toJson(QJsonDocument::Compact);
}

inline QList<Task> fromJson(const QByteArray &json)
{
    QList<Task> list;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        Task t;
        t.id = o.value(QStringLiteral("id")).toString();
        t.title = o.value(QStringLiteral("title")).toString();
        t.course = o.value(QStringLiteral("course")).toString(); // older files: ""
        t.due = QDateTime::fromString(o.value(QStringLiteral("due")).toString(), Qt::ISODate);
        t.done = o.value(QStringLiteral("done")).toBool();
        t.warned = o.value(QStringLiteral("warned")).toBool();
        t.notified = o.value(QStringLiteral("notified")).toBool();
        if (!t.id.isEmpty() && !t.title.isEmpty())
            list << t;
    }
    return list;
}

}
