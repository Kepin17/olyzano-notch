#pragma once

#include "platform/PrayerModel.h"

#include <QDate>
#include <QElapsedTimer>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

// Prayer times as `prayer` in QML: calculated here from the place and method chosen in Settings (nothing is sent
// anywhere), a toast in the notch when a time comes and a heads-up a few minutes before, each once per day even across
// restarts, and the next one for the idle capsule. On by default; it stays quiet until a place is known. Settings live
// in notch/prayer.conf. `NOTCH_FAKE_NOW=2026-11-02T11:35:00` moves the clock for trying it.
class Prayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString city READ city WRITE setCity NOTIFY settingsChanged)            // a bundled city, or "" for custom
    Q_PROPERTY(double customLat READ customLat NOTIFY settingsChanged)
    Q_PROPERTY(double customLon READ customLon NOTIFY settingsChanged)
    Q_PROPERTY(QString method READ method WRITE setMethod NOTIFY settingsChanged)      // kemenag | mwl | ummalqura | egypt | karachi | isna
    Q_PROPERTY(int asr READ asr WRITE setAsr NOTIFY settingsChanged)                   // 1 Shafi'i (and most), 2 Hanafi
    Q_PROPERTY(int beforeMinutes READ beforeMinutes WRITE setBeforeMinutes NOTIFY settingsChanged) // 0: no heads-up
    Q_PROPERTY(bool inCapsule READ inCapsule WRITE setInCapsule NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList cities READ cities CONSTANT)                               // [{name, country}]
    Q_PROPERTY(QVariantList methods READ methods CONSTANT)                             // [{id, name}]
    Q_PROPERTY(bool ready READ ready NOTIFY changed)                                   // a place is set
    Q_PROPERTY(QString place READ place NOTIFY changed)
    Q_PROPERTY(QVariantList today READ today NOTIFY changed)                           // [{name, time, next, prayer}]
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY changed)
    Q_PROPERTY(QString nextName READ nextName NOTIFY changed)
    Q_PROPERTY(QString nextTime READ nextTime NOTIFY changed)
    Q_PROPERTY(QString nextRemaining READ nextRemaining NOTIFY changed)
    Q_PROPERTY(bool soon READ soon NOTIFY changed)                                     // the next one is within the heads-up time

public:
    explicit Prayer(QObject *parent = nullptr);

    bool enabled() const { return m_settings.value(QStringLiteral("enabled"), true).toBool(); }
    void setEnabled(bool on);
    QString city() const { return m_settings.value(QStringLiteral("city")).toString(); }
    void setCity(const QString &city);
    double customLat() const { return m_settings.value(QStringLiteral("lat"), 0.0).toDouble(); }
    double customLon() const { return m_settings.value(QStringLiteral("lon"), 0.0).toDouble(); }
    QString method() const { return m_settings.value(QStringLiteral("method"), QStringLiteral("kemenag")).toString(); }
    void setMethod(const QString &id);
    int asr() const { return m_settings.value(QStringLiteral("asr"), 1).toInt() == 2 ? 2 : 1; }
    void setAsr(int factor);
    int beforeMinutes() const { return qBound(0, m_settings.value(QStringLiteral("before"), 10).toInt(), 60); }
    void setBeforeMinutes(int minutes);
    bool inCapsule() const { return m_settings.value(QStringLiteral("inCapsule"), true).toBool(); }
    void setInCapsule(bool on);
    QVariantList cities() const;
    QVariantList methods() const;
    bool ready() const { return m_ready; }
    QString place() const { return m_place; }
    QVariantList today() const { return m_today; }
    bool hasNext() const { return m_next >= 0; }
    QString nextName() const;
    QString nextTime() const;
    QString nextRemaining() const;
    bool soon() const;

    Q_INVOKABLE void setCustomPlace(double lat, double lon); // any latitude and longitude

signals:
    void changed();
    void settingsChanged();
    void prayerTime(const QString &name, const QString &time);      // it is time
    void prayerSoon(const QString &name, int minutes);              // a few minutes to go
    void placeNote(const QString &text);                            // "Prayer times for Jakarta..." / "Choose your city..." (once)

private:
    QDateTime now() const;
    bool place(double *lat, double *lon);
    PrayerModel::Times timesFor(const QDate &date);
    void refresh();
    void tick();
    bool fired(const QDate &date, int prayer, const char *kind) const;
    void mark(const QDate &date, int prayer, const char *kind);

    QSettings m_settings;
    QTimer m_tick;
    QDateTime m_fakeBase;
    QElapsedTimer m_fakeClock;
    bool m_ready = false;
    QString m_place;
    QVariantList m_today;
    PrayerModel::Times m_times, m_tomorrow;
    QDate m_date;
    int m_next = -1;
    bool m_nextTomorrow = false;
};
