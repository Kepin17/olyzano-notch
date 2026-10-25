#pragma once

#include <QDate>
#include <QVariantList>
#include <QVariantMap>

// The month grid of the calendar card. Pure, for the tests.
namespace CalendarModel {

// Six weeks (42 days) starting on `firstDay` (Qt::DayOfWeek, 1 Monday ... 7 Sunday) around the month: each
// {year, month, day, inMonth, today, week (the ISO week number of the row's first day)}.
inline QVariantList grid(int year, int month, int firstDay, const QDate &today)
{
    QVariantList cells;
    const QDate first(year, month, 1);
    if (!first.isValid() || firstDay < 1 || firstDay > 7)
        return cells;
    const QDate start = first.addDays(-((first.dayOfWeek() - firstDay + 7) % 7));
    for (int i = 0; i < 42; ++i) {
        const QDate d = start.addDays(i);
        cells << QVariantMap{{QStringLiteral("year"), d.year()}, {QStringLiteral("month"), d.month()}, {QStringLiteral("day"), d.day()},
                             {QStringLiteral("inMonth"), d.month() == month}, {QStringLiteral("today"), d == today},
                             {QStringLiteral("week"), start.addDays(i - i % 7).weekNumber()}};
    }
    return cells;
}

}
