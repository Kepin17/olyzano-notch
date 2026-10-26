#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QTimeZone>
#include <cmath>

// Prayer times, calculated here and offline (the usual astronomical method: the sun's position for the day, then the
// angles below the horizon that make Fajr and Isha, the shadow length for Asr). No timers, no network, so tests can
// drive it. Times are as good as the method: a minute or two from what a local authority prints.
namespace PrayerModel {

enum Time { Fajr = 0, Sunrise, Dhuhr, Asr, Maghrib, Isha, PrayerCount };

struct Method {
    const char *id, *name;
    double fajrAngle, ishaAngle;
    int ishaMinutes;   // > 0: Isha is this long after Maghrib (instead of an angle)
    int ihtiyat;       // minutes added for safety to every prayer (and taken from sunrise)
};

inline const Method *methods(int *count = nullptr)
{
    static const Method list[] = {
        {"kemenag", "Kemenag RI (Indonesia)", 20, 18, 0, 2},
        {"mwl", "Muslim World League", 18, 17, 0, 0},
        {"ummalqura", "Umm al-Qura (Makkah)", 18.5, 0, 90, 0},
        {"egypt", "Egyptian General Authority", 19.5, 17.5, 0, 0},
        {"karachi", "University of Islamic Sciences, Karachi", 18, 18, 0, 0},
        {"isna", "ISNA (North America)", 15, 15, 0, 0},
    };
    if (count)
        *count = int(sizeof(list) / sizeof(list[0]));
    return list;
}

inline const Method &method(const QString &id)
{
    int n = 0;
    const Method *m = methods(&n);
    for (int i = 0; i < n; ++i)
        if (id == QLatin1String(m[i].id))
            return m[i];
    return m[0];
}

struct Times {
    QDateTime at[PrayerCount]; // invalid when it cannot be told
    bool valid() const { return at[Fajr].isValid() && at[Isha].isValid(); }
};

namespace detail {
constexpr double Pi = 3.14159265358979323846;
inline double rad(double d) { return d * Pi / 180; }
inline double deg(double r) { return r * 180 / Pi; }
inline double fixAngle(double a) { a -= 360 * std::floor(a / 360); return a; }
inline double fixHour(double h) { h -= 24 * std::floor(h / 24); return h; }

inline double julian(int y, int m, int d)
{
    if (m <= 2) { y -= 1; m += 12; }
    const int a = y / 100, b = 2 - a + a / 4;
    return std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + d + b - 1524.5;
}

struct Sun { double declination, equation; }; // degrees, hours
inline Sun sunPosition(double jd)
{
    const double d = jd - 2451545.0;
    const double g = fixAngle(357.529 + 0.98560028 * d);
    const double q = fixAngle(280.459 + 0.98564736 * d);
    const double l = fixAngle(q + 1.915 * std::sin(rad(g)) + 0.020 * std::sin(rad(2 * g)));
    const double e = 23.439 - 0.00000036 * d;
    const double ra = fixHour(deg(std::atan2(std::cos(rad(e)) * std::sin(rad(l)), std::cos(rad(l)))) / 15);
    return {deg(std::asin(std::sin(rad(e)) * std::sin(rad(l)))), q / 15 - ra};
}

struct Calc {
    double lat, jd;
    double midDay(double t) const { return fixHour(12 - sunPosition(jd + t).equation); }
    // the time (hours) the sun is `angle` degrees below the horizon; NaN when it never gets there
    double sunAngleTime(double angle, double t, bool ccw) const
    {
        const double decl = sunPosition(jd + t).declination;
        const double c = (-std::sin(rad(angle)) - std::sin(rad(decl)) * std::sin(rad(lat))) / (std::cos(rad(decl)) * std::cos(rad(lat)));
        if (c > 1 || c < -1)
            return std::nan("");
        const double h = deg(std::acos(c)) / 15;
        return midDay(t) + (ccw ? -h : h);
    }
    double asrTime(int factor, double t) const
    {
        const double decl = sunPosition(jd + t).declination;
        const double angle = -deg(std::atan(1.0 / (factor + std::tan(rad(std::fabs(lat - decl))))));
        return sunAngleTime(angle, t, false);
    }
};

// when a twilight angle is never reached (high latitudes), or reached too far from the sun: no more than angle/60 of the night
inline double nightLimited(double t, double base, double angle, double night, bool ccw)
{
    const double portion = angle / 60 * night;
    const double diff = ccw ? base - t : t - base;
    return std::isnan(t) || diff > portion ? base + (ccw ? -portion : portion) : t;
}
}

// The six times of `date` at (lat, lon); `tz` is the zone's offset from UTC in hours on that day (+7 for Jakarta).
// asrFactor: 1 Shafi'i (and most), 2 Hanafi.
inline Times times(const QDate &date, double lat, double lon, double tz, const Method &m, int asrFactor = 1)
{
    using namespace detail;
    Times out;
    const Calc c{lat, julian(date.year(), date.month(), date.day()) - lon / (15 * 24)};
    double fajr = c.sunAngleTime(m.fajrAngle, 5.0 / 24, true);
    double sunrise = c.sunAngleTime(0.833, 6.0 / 24, true);
    double dhuhr = c.midDay(12.0 / 24);
    double asr = c.asrTime(asrFactor, 13.0 / 24);
    double sunset = c.sunAngleTime(0.833, 18.0 / 24, false);
    double maghrib = sunset;
    double isha = m.ishaMinutes > 0 ? maghrib + m.ishaMinutes / 60.0 : c.sunAngleTime(m.ishaAngle, 18.0 / 24, false);
    if (std::isnan(sunrise) || std::isnan(sunset))
        return out; // a polar day or night: nothing sensible to say
    const double night = fixHour(sunrise - sunset);
    fajr = nightLimited(fajr, sunrise, m.fajrAngle, night, true);
    if (m.ishaMinutes == 0)
        isha = nightLimited(isha, sunset, m.ishaAngle, night, false);
    const double shift = tz - lon / 15;
    const double hours[PrayerCount] = {fajr, sunrise, dhuhr, asr, maghrib, isha};
    const QTimeZone zone = QTimeZone::fromSecondsAheadOfUtc(int(std::lround(tz * 3600)));
    const QDateTime midnight(date, QTime(0, 0), zone);
    for (int i = 0; i < PrayerCount; ++i) {
        const double h = hours[i] + shift + (i == Sunrise ? -m.ihtiyat : m.ihtiyat) / 60.0;
        out.at[i] = midnight.addSecs(qint64(std::floor(fixHour(h) * 60 + 0.5)) * 60);
    }
    return out;
}

// The first of Fajr, Dhuhr, Asr, Maghrib, Isha after `now`, or -1 when today's are over. (Sunrise is a time, not a prayer.)
inline int next(const Times &t, const QDateTime &now)
{
    for (int p : {Fajr, Dhuhr, Asr, Maghrib, Isha})
        if (t.at[p].isValid() && t.at[p] > now)
            return p;
    return -1;
}

inline QString name(int prayer, const QDate &date)
{
    switch (prayer) {
    case Fajr: return QStringLiteral("Fajr · Subuh");
    case Sunrise: return QStringLiteral("Sunrise · Terbit");
    case Dhuhr: return date.dayOfWeek() == 5 ? QStringLiteral("Dhuhr · Jumat") : QStringLiteral("Dhuhr · Zuhur");
    case Asr: return QStringLiteral("Asr · Asar");
    case Maghrib: return QStringLiteral("Maghrib");
    case Isha: return QStringLiteral("Isha · Isya");
    }
    return {};
}
inline QString shortName(int prayer)
{
    static const char *n[] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
    return prayer >= 0 && prayer < PrayerCount ? QLatin1String(n[prayer]) : QString();
}

}
