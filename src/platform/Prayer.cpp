#include "platform/Prayer.h"

#include "platform/Cities.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QTimeZone>

using namespace PrayerModel;

namespace {
bool isPrayer(int p) { return p == Fajr || p == Dhuhr || p == Asr || p == Maghrib || p == Isha; } // sunrise is a time, not a prayer

QString span(qint64 minutes)
{
    if (minutes < 1)
        return QStringLiteral("now");
    if (minutes < 60)
        return QStringLiteral("in %1 min").arg(minutes);
    return minutes % 60 == 0 ? QStringLiteral("in %1 h").arg(minutes / 60) : QStringLiteral("in %1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}
}

Prayer::Prayer(QObject *parent)
    : QObject(parent)
    , m_settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/prayer.conf"), QSettings::IniFormat)
{
    const QByteArray fake = qgetenv("NOTCH_FAKE_NOW"); // for trying the reminders without waiting for noon
    if (!fake.isEmpty()) {
        m_fakeBase = QDateTime::fromString(QString::fromLatin1(fake), Qt::ISODate);
        m_fakeClock.start();
    }
    // the first time: a place from the time zone when it says where we are
    if (!m_settings.contains(QStringLiteral("city")) && !m_settings.contains(QStringLiteral("lat"))) {
        const QString guess = Cities::guess(QString::fromLatin1(QTimeZone::systemTimeZoneId()));
        if (!guess.isEmpty())
            m_settings.setValue(QStringLiteral("city"), guess);
    }
    m_tick.setInterval(20000);
    connect(&m_tick, &QTimer::timeout, this, &Prayer::tick);
    m_tick.start();
    refresh();
    QTimer::singleShot(4000, this, [this] { // said once, when the notch has had time to appear
        if (!enabled() || m_settings.value(QStringLiteral("hinted")).toBool())
            return;
        m_settings.setValue(QStringLiteral("hinted"), true);
        if (m_ready)
            emit placeNote(QStringLiteral("Prayer times for %1 · change the city in Settings").arg(m_place));
        else
            emit placeNote(QStringLiteral("Choose your city for prayer times in Settings"));
    });
    QTimer::singleShot(5000, this, &Prayer::tick);
}

QDateTime Prayer::now() const
{
    return m_fakeBase.isValid() ? m_fakeBase.addMSecs(m_fakeClock.elapsed()) : QDateTime::currentDateTime();
}

QVariantList Prayer::cities() const
{
    QVariantList list;
    int n = 0;
    const Cities::City *c = Cities::all(&n);
    for (int i = 0; i < n; ++i)
        list << QVariantMap{{QStringLiteral("name"), QString::fromLatin1(c[i].name)}, {QStringLiteral("country"), QString::fromLatin1(c[i].country)}};
    return list;
}

QVariantList Prayer::methods() const
{
    QVariantList list;
    int n = 0;
    const Method *m = PrayerModel::methods(&n);
    for (int i = 0; i < n; ++i)
        list << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(m[i].id)}, {QStringLiteral("name"), QString::fromLatin1(m[i].name)}};
    return list;
}

bool Prayer::place(double *lat, double *lon)
{
    const QString c = city();
    if (!c.isEmpty()) {
        int n = 0;
        const Cities::City *list = Cities::all(&n);
        const int i = Cities::indexOf(c);
        if (i >= 0) {
            *lat = list[i].lat;
            *lon = list[i].lon;
            m_place = c;
            return true;
        }
    }
    if (m_settings.contains(QStringLiteral("lat")) && m_settings.contains(QStringLiteral("lon"))) {
        *lat = customLat();
        *lon = customLon();
        m_place = QStringLiteral("%1°, %2°").arg(*lat, 0, 'f', 3).arg(*lon, 0, 'f', 3);
        return true;
    }
    m_place.clear();
    return false;
}

PrayerModel::Times Prayer::timesFor(const QDate &date)
{
    double lat = 0, lon = 0;
    if (!place(&lat, &lon))
        return {};
    const double tz = QDateTime(date, QTime(12, 0)).offsetFromUtc() / 3600.0; // the system zone on that day (summer time too)
    return PrayerModel::times(date, lat, lon, tz, PrayerModel::method(method()), asr());
}

void Prayer::refresh()
{
    const QDateTime t = now();
    m_date = t.date();
    m_times = timesFor(m_date);
    m_tomorrow = timesFor(m_date.addDays(1));
    m_ready = m_times.valid();
    m_next = -1;
    m_nextTomorrow = false;
    m_today.clear();
    if (m_ready) {
        m_next = PrayerModel::next(m_times, t);
        if (m_next < 0 && m_tomorrow.valid()) {
            m_next = Fajr;
            m_nextTomorrow = true;
        }
        for (int i = 0; i < PrayerCount; ++i)
            m_today << QVariantMap{{QStringLiteral("name"), name(i, m_date)}, {QStringLiteral("time"), m_times.at[i].toString(QStringLiteral("HH:mm"))},
                                   {QStringLiteral("next"), i == m_next && !m_nextTomorrow}, {QStringLiteral("prayer"), isPrayer(i)}};
    }
    emit changed();
}

QString Prayer::nextName() const
{
    return m_next >= 0 ? name(m_next, m_nextTomorrow ? m_date.addDays(1) : m_date) : QString();
}

QString Prayer::nextTime() const
{
    if (m_next < 0)
        return {};
    return (m_nextTomorrow ? m_tomorrow : m_times).at[m_next].toString(QStringLiteral("HH:mm"));
}

QString Prayer::nextRemaining() const
{
    if (m_next < 0)
        return {};
    return span((now().secsTo((m_nextTomorrow ? m_tomorrow : m_times).at[m_next]) + 59) / 60);
}

bool Prayer::soon() const
{
    if (m_next < 0 || beforeMinutes() == 0)
        return false;
    const qint64 secs = now().secsTo((m_nextTomorrow ? m_tomorrow : m_times).at[m_next]);
    return secs > 0 && secs <= beforeMinutes() * 60;
}

bool Prayer::fired(const QDate &date, int prayer, const char *kind) const
{
    return m_settings.value(QStringLiteral("fired")).toStringList().contains(QStringLiteral("%1:%2:%3").arg(date.toString(Qt::ISODate)).arg(prayer).arg(QLatin1String(kind)));
}

void Prayer::mark(const QDate &date, int prayer, const char *kind)
{
    QStringList list = m_settings.value(QStringLiteral("fired")).toStringList();
    const QString today = date.toString(Qt::ISODate);
    list.erase(std::remove_if(list.begin(), list.end(), [&](const QString &s) { return s.left(10) < today; }), list.end()); // yesterday's are done with
    list << QStringLiteral("%1:%2:%3").arg(today).arg(prayer).arg(QLatin1String(kind));
    m_settings.setValue(QStringLiteral("fired"), list);
}

void Prayer::tick()
{
    const QDateTime t = now();
    if (t.date() != m_date)
        refresh();
    if (enabled() && m_ready) {
        for (int p : {Fajr, Dhuhr, Asr, Maghrib, Isha}) {
            const QDateTime at = m_times.at[p];
            if (!at.isValid())
                continue;
            if (t >= at) {
                if (!fired(m_date, p, "now")) {
                    mark(m_date, p, "now");
                    mark(m_date, p, "soon");
                    if (at.secsTo(t) <= 5 * 60) // a time that passed while Notch was closed is not announced
                        emit prayerTime(name(p, m_date), at.toString(QStringLiteral("HH:mm")));
                }
            } else if (beforeMinutes() > 0 && t >= at.addSecs(-beforeMinutes() * 60) && !fired(m_date, p, "soon")) {
                mark(m_date, p, "soon");
                emit prayerSoon(name(p, m_date), int((t.secsTo(at) + 59) / 60));
            }
        }
    }
    refresh(); // keeps "in 25 min" and the next one current
}

void Prayer::setEnabled(bool on)
{
    m_settings.setValue(QStringLiteral("enabled"), on);
    emit settingsChanged();
}

void Prayer::setCity(const QString &city)
{
    m_settings.setValue(QStringLiteral("city"), city);
    if (!city.isEmpty())
        m_settings.remove(QStringLiteral("lat"));
    emit settingsChanged();
    refresh();
}

void Prayer::setCustomPlace(double lat, double lon)
{
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180)
        return;
    m_settings.setValue(QStringLiteral("city"), QString());
    m_settings.setValue(QStringLiteral("lat"), lat);
    m_settings.setValue(QStringLiteral("lon"), lon);
    emit settingsChanged();
    refresh();
}

void Prayer::setMethod(const QString &id)
{
    m_settings.setValue(QStringLiteral("method"), id);
    emit settingsChanged();
    refresh();
}

void Prayer::setAsr(int factor)
{
    m_settings.setValue(QStringLiteral("asr"), factor == 2 ? 2 : 1);
    emit settingsChanged();
    refresh();
}

void Prayer::setBeforeMinutes(int minutes)
{
    m_settings.setValue(QStringLiteral("before"), qBound(0, minutes, 60));
    emit settingsChanged();
    refresh();
}

void Prayer::setInCapsule(bool on)
{
    m_settings.setValue(QStringLiteral("inCapsule"), on);
    emit settingsChanged();
}
