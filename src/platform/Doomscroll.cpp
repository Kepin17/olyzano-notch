#include "platform/NotchPaths.h"
#include "platform/Doomscroll.h"

#include "platform/AppDock.h"
#include "platform/MediaPlayers.h"
#include "platform/Wellbeing.h"

#include <QDateTime>
#include <QStandardPaths>

using namespace DoomscrollModel;

Doomscroll::Doomscroll(AppDock *dock, MediaPlayers *media, Wellbeing *wellbeing, QObject *parent)
    : QObject(parent)
    , m_dock(dock)
    , m_media(media)
    , m_wellbeing(wellbeing)
    , m_settings(NotchPaths::conf(QStringLiteral("doomscroll")), QSettings::IniFormat)
{
    applySettings();
    rollDay();
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &Doomscroll::look);
    if (enabled())
        m_timer.start();
    m_answer.setSingleShot(true);
    m_answer.setInterval(60000); // an unanswered warning counts as "later"
    connect(&m_answer, &QTimer::timeout, this, [this] { snooze(repeatMinutes()); });
}

void Doomscroll::store(const char *key, const QVariant &v)
{
    if (m_settings.value(QLatin1String(key)) == v)
        return;
    m_settings.setValue(QLatin1String(key), v);
    applySettings();
    emit settingsChanged();
}

void Doomscroll::setEnabled(bool on)
{
    store("enabled", on);
    if (on && !m_timer.isActive())
        m_timer.start();
    else if (!on) {
        m_timer.stop();
        m_site = Site::None;
        m_tracker.clear();
        if (m_reminding)
            dismiss();
        emit changed();
    }
}

void Doomscroll::applySettings()
{
    m_tracker.limitMs = qint64(limitMinutes()) * 60000;
    m_tracker.swipeLimit = swipeLimit();
    m_tracker.repeatMs = qint64(repeatMinutes()) * 60000;
}

void Doomscroll::setQuiet(bool q)
{
    if (q == m_quiet)
        return;
    m_quiet = q;
    emit changed();
}

void Doomscroll::rollDay()
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (m_day == today)
        return;
    if (m_settings.value(QStringLiteral("day")).toString() == today) {
        for (Site s : {Site::Instagram, Site::TikTok, Site::Shorts, Site::Facebook, Site::Custom})
            m_todayMs[int(s)] = m_settings.value(QStringLiteral("today/") + siteId(s)).toLongLong();
    } else {
        m_todayMs.clear();
        m_settings.setValue(QStringLiteral("day"), today);
        m_tracker.mutedUntil = 0;
    }
    m_day = today;
}

QVariantMap Doomscroll::today() const
{
    QVariantMap m;
    qint64 total = 0;
    for (Site s : {Site::Instagram, Site::TikTok, Site::Shorts, Site::Facebook, Site::Custom}) {
        const qint64 ms = m_todayMs.value(int(s));
        total += ms;
        m[siteId(s)] = int(ms / 60000);
    }
    m[QStringLiteral("total")] = int(total / 60000);
    return m;
}

void Doomscroll::look()
{
    rollDay();
    What w;
    w.app = m_dock->activeAppName();
    w.title = m_dock->activeTitle();
    w.mprisPlaying = m_media->playing();
    w.mprisLengthMs = m_media->lengthMs();
    // "touching" the computer: a Short plays by itself for half a minute, so a minute of quiet is still scrolling
    const qint64 idle = m_wellbeing ? m_wellbeing->idleMs() : 0;
    sample(w, idle < 0 || idle < 60000, QDateTime::currentMSecsSinceEpoch());
}

void Doomscroll::sample(const What &what, bool active, qint64 now)
{
    Options o;
    o.instagram = instagram();
    o.tiktok = tiktok();
    o.shorts = shorts();
    o.facebook = facebook();
    for (const QString &word : custom().split(u',', Qt::SkipEmptyParts))
        o.custom << word.trimmed();
    const Site site = classify(what, o);
    const QString swipeId = site != Site::None && what.mprisPlaying && what.mprisLengthMs <= 120000 ? m_media->title() : QString();
    const qint64 dt = m_lastSampleAt ? qBound(qint64(0), now - m_lastSampleAt, qint64(6000)) : 0;
    m_lastSampleAt = now;
    if (site != Site::None && active)
        m_todayMs[int(site)] += dt;
    const auto event = m_tracker.tick(now, site, active, swipeId);
    const Site shown = active ? site : Site::None;
    m_site = shown;
    if (event == Tracker::Event::Warn && !m_quiet) {
        const QString name = DoomscrollModel::siteName(m_tracker.mainSite());
        m_message = tr("You have scrolled %1 for %2 min%3.").arg(name).arg(minutes()).arg(swipes() > 0 ? tr(" (%1 videos)").arg(swipes()) : QString());
        m_reminding = true;
        m_answer.start();
        emit remindingChanged();
    } else if (event == Tracker::Event::Warn) {
        m_tracker.snooze(now, 60000); // held back by a game or a call: ask again soon
    }
    if (now - m_savedAt > 30000) {
        m_savedAt = now;
        for (const auto &s : {Site::Instagram, Site::TikTok, Site::Shorts, Site::Facebook, Site::Custom})
            m_settings.setValue(QStringLiteral("today/") + siteId(s), m_todayMs.value(int(s)));
    }
    emit changed();
}

void Doomscroll::snooze(int minutes)
{
    m_tracker.snooze(QDateTime::currentMSecsSinceEpoch(), qint64(minutes) * 60000);
    if (m_reminding) {
        m_reminding = false;
        m_answer.stop();
        emit remindingChanged();
    }
}

void Doomscroll::dismiss()
{
    m_tracker.clear();
    if (m_reminding) {
        m_reminding = false;
        m_answer.stop();
        emit remindingChanged();
    }
    emit changed();
}

void Doomscroll::stopToday()
{
    m_tracker.muteFor(QDateTime::currentMSecsSinceEpoch(), qint64(24) * 3600000);
    if (m_reminding) {
        m_reminding = false;
        m_answer.stop();
        emit remindingChanged();
    }
}

void Doomscroll::resetToday()
{
    m_todayMs.clear();
    for (const auto &s : {Site::Instagram, Site::TikTok, Site::Shorts, Site::Facebook, Site::Custom})
        m_settings.setValue(QStringLiteral("today/") + siteId(s), 0);
    emit changed();
}

void Doomscroll::debugFeed(const QString &siteName, int seconds)
{
    // pretend: `seconds` of scrolling that site, one look every 2 s, at once
    m_timer.stop();
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    What w;
    w.title = siteName == QLatin1String("shorts") ? QStringLiteral("a short - YouTube") : siteName;
    w.mprisPlaying = siteName == QLatin1String("shorts");
    w.mprisLengthMs = 30000;
    for (int i = 0; i < seconds / 2; ++i, now += 2000)
        sample(w, true, now);
}
