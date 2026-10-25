#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// Doomscrolling, without timers or OS calls so tests can drive it. A short-form feed (Instagram, TikTok, YouTube
// Shorts...) in front while the person is active counts as scrolling; the minutes and the videos swiped add up over a
// rolling hour, and when they pass a limit the person is warned (then again now and then while it goes on).
namespace DoomscrollModel {

enum class Site { None, Instagram, TikTok, Shorts, Facebook, Custom };

inline QString siteId(Site s)
{
    switch (s) {
    case Site::Instagram: return QStringLiteral("instagram");
    case Site::TikTok: return QStringLiteral("tiktok");
    case Site::Shorts: return QStringLiteral("shorts");
    case Site::Facebook: return QStringLiteral("facebook");
    case Site::Custom: return QStringLiteral("custom");
    default: return {};
    }
}
inline QString siteName(Site s)
{
    switch (s) {
    case Site::Instagram: return QStringLiteral("Instagram");
    case Site::TikTok: return QStringLiteral("TikTok");
    case Site::Shorts: return QStringLiteral("YouTube Shorts");
    case Site::Facebook: return QStringLiteral("Facebook Reels");
    case Site::Custom: return QStringLiteral("Short videos");
    default: return {};
    }
}

struct What {                    // what is in front of the person
    QString app, windowClass, title;
    qint64 mprisLengthMs = 0;     // the browser's now-playing item, if any
    bool mprisPlaying = false;
};
struct Options {                  // which sites count
    bool instagram = true, tiktok = true, shorts = true, facebook = false;
    QStringList custom;           // other words in a window's name or title
};

// Which short-form feed the window in front is, if any. YouTube is a Short when the browser's playing item lasts a
// minute or less (a Short's title carries no mark), or when the title says so.
inline Site classify(const What &w, const Options &o)
{
    const QString text = (w.app + u' ' + w.windowClass + u' ' + w.title).toLower();
    if (o.instagram && text.contains(QLatin1String("instagram")))
        return Site::Instagram;
    if (o.tiktok && text.contains(QLatin1String("tiktok")))
        return Site::TikTok;
    if (o.shorts && text.contains(QLatin1String("youtube"))
        && (text.contains(QLatin1String("shorts")) || (w.mprisPlaying && w.mprisLengthMs > 0 && w.mprisLengthMs <= 61000)))
        return Site::Shorts;
    if (o.facebook && text.contains(QLatin1String("facebook")) && text.contains(QLatin1String("reel")))
        return Site::Facebook;
    for (const QString &word : o.custom)
        if (!word.trimmed().isEmpty() && text.contains(word.trimmed().toLower()))
            return Site::Custom;
    return Site::None;
}

struct Tracker {
    enum class Event { None, Warn };
    struct Entry { qint64 at, ms; Site site; bool swipe; };

    qint64 windowMs = 60 * 60000;    // what counts: the last hour
    qint64 awayResetMs = 5 * 60000;  // this long without scrolling: it was a break, start over
    qint64 limitMs = 15 * 60000;     // warn at this much...
    int swipeLimit = 30;             // ...or this many videos
    qint64 repeatMs = 10 * 60000;    // and again this often while it goes on

    QList<Entry> entries;
    qint64 lastTick = 0, lastScrollAt = 0, nextWarnAt = 0, mutedUntil = 0;
    QString lastSwipeId;

    qint64 totalMs() const { qint64 t = 0; for (const Entry &e : entries) t += e.ms; return t; }
    int swipes() const { int n = 0; for (const Entry &e : entries) n += e.swipe ? 1 : 0; return n; }
    QHash<int, qint64> perSite() const { QHash<int, qint64> m; for (const Entry &e : entries) m[int(e.site)] += e.ms; return m; }
    Site mainSite() const
    {
        Site best = Site::None;
        qint64 most = 0;
        const auto m = perSite();
        for (auto it = m.cbegin(); it != m.cend(); ++it)
            if (it.value() > most) { most = it.value(); best = Site(it.key()); }
        return best;
    }
    void snooze(qint64 now, qint64 ms) { nextWarnAt = now + ms; }
    void muteFor(qint64 now, qint64 ms) { mutedUntil = now + ms; }
    void clear() { entries.clear(); nextWarnAt = 0; lastSwipeId.clear(); }

    // Called every couple of seconds. site: what is in front now (None if not a feed); active: the person is
    // interacting (idle time is short); swipeId: what plays now (a new one while scrolling = the next video).
    Event tick(qint64 now, Site site, bool active, const QString &swipeId)
    {
        const qint64 dt = lastTick ? qBound(qint64(0), now - lastTick, qint64(6000)) : 0;
        lastTick = now;
        while (!entries.isEmpty() && entries.first().at < now - windowMs)
            entries.removeFirst();
        if (site == Site::None || !active) {
            if (lastScrollAt && now - lastScrollAt >= awayResetMs) {
                clear();
                lastScrollAt = 0;
            }
            return Event::None;
        }
        const bool swipe = !swipeId.isEmpty() && !lastSwipeId.isEmpty() && swipeId != lastSwipeId;
        if (!swipeId.isEmpty())
            lastSwipeId = swipeId;
        entries.append(Entry{now, dt, site, swipe});
        lastScrollAt = now;
        if ((totalMs() >= limitMs || swipes() >= swipeLimit) && now >= nextWarnAt && now >= mutedUntil) {
            nextWarnAt = now + repeatMs;
            return Event::Warn;
        }
        return Event::None;
    }
};

}
