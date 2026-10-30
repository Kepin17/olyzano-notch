#include "platform/NotchPaths.h"
#include "platform/PanelBar.h"

#include "platform/CalendarModel.h"

#include <QStandardPaths>
#include <QtGlobal>

PanelBar::PanelBar(QObject *parent)
    : QObject(parent)
    , m_settings(NotchPaths::conf(QStringLiteral("panel")), QSettings::IniFormat)
{
}

void PanelBar::store(const char *key, const QVariant &v)
{
    if (m_settings.value(QLatin1String(key)) == v)
        return;
    m_settings.setValue(QLatin1String(key), v);
    emit changed();
}

QString PanelBar::behavior() const
{
    return m_settings.value(QStringLiteral("behavior"), QStringLiteral("always")).toString() == QLatin1String("auto") ? QStringLiteral("auto") : QStringLiteral("always");
}

void PanelBar::setBehavior(const QString &b)
{
    store("behavior", b == QLatin1String("auto") ? QStringLiteral("auto") : QStringLiteral("always"));
}

double PanelBar::opacity() const
{
    return qBound(0.2, m_settings.value(QStringLiteral("opacity"), 0.55).toDouble(), 1.0);
}

void PanelBar::setOpacity(double v)
{
    store("opacity", qBound(0.2, v, 1.0));
}

QString PanelBar::openOn() const
{
    return m_settings.value(QStringLiteral("openOn"), QStringLiteral("click")).toString() == QLatin1String("hover") ? QStringLiteral("hover") : QStringLiteral("click");
}

void PanelBar::setOpenOn(const QString &m)
{
    store("openOn", m == QLatin1String("hover") ? QStringLiteral("hover") : QStringLiteral("click"));
}

QString PanelBar::weekStart() const
{
    const QString w = m_settings.value(QStringLiteral("weekStart"), QStringLiteral("system")).toString();
    return w == QLatin1String("monday") || w == QLatin1String("sunday") ? w : QStringLiteral("system");
}

void PanelBar::setWeekStart(const QString &w)
{
    store("weekStart", w == QLatin1String("monday") || w == QLatin1String("sunday") ? w : QStringLiteral("system"));
}

int PanelBar::firstDay() const
{
    const QString w = weekStart();
    return w == QLatin1String("monday") ? Qt::Monday : w == QLatin1String("sunday") ? Qt::Sunday : int(QLocale().firstDayOfWeek());
}

QVariantList PanelBar::monthGrid(int year, int month) const
{
    return CalendarModel::grid(year, month, firstDay(), QDate::currentDate());
}
