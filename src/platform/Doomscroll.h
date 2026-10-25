#pragma once

#include "platform/DoomscrollModel.h"

#include <QDate>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

class AppDock;
class MediaPlayers;
class Wellbeing;

// Doomscrolling, as `doomscroll` in QML: every couple of seconds it looks at the window in front (its name and title),
// what the browser is playing and how long ago the person touched the keyboard or mouse, decides whether that is a
// short-form feed (Instagram, TikTok, YouTube Shorts...), and warns when it adds up (see DoomscrollModel.h). Only the
// kind of site is kept, never titles or addresses; everything stays on this computer. Settings and today's minutes live
// in notch/doomscroll.conf.
class Doomscroll : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString site READ site NOTIFY changed)               // what is being scrolled now: "", instagram, tiktok, shorts...
    Q_PROPERTY(QString siteName READ siteName NOTIFY changed)       // "YouTube Shorts"
    Q_PROPERTY(int minutes READ minutes NOTIFY changed)             // in the last hour
    Q_PROPERTY(int swipes READ swipes NOTIFY changed)               // videos swiped in the last hour
    Q_PROPERTY(bool reminding READ reminding NOTIFY remindingChanged) // a warning is waiting for an answer
    Q_PROPERTY(QString message READ message NOTIFY remindingChanged)
    Q_PROPERTY(QVariantMap today READ today NOTIFY changed)         // {total, instagram, tiktok, shorts, facebook, custom}: minutes
    Q_PROPERTY(bool quiet READ quiet WRITE setQuiet NOTIFY changed) // set from QML: a game or a call, no warnings
    // settings
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool instagram READ instagram WRITE setInstagram NOTIFY settingsChanged)
    Q_PROPERTY(bool tiktok READ tiktok WRITE setTiktok NOTIFY settingsChanged)
    Q_PROPERTY(bool shorts READ shorts WRITE setShorts NOTIFY settingsChanged)
    Q_PROPERTY(bool facebook READ facebook WRITE setFacebook NOTIFY settingsChanged)
    Q_PROPERTY(QString custom READ custom WRITE setCustom NOTIFY settingsChanged) // other words, separated by commas
    Q_PROPERTY(int limitMinutes READ limitMinutes WRITE setLimitMinutes NOTIFY settingsChanged)
    Q_PROPERTY(int swipeLimit READ swipeLimit WRITE setSwipeLimit NOTIFY settingsChanged)
    Q_PROPERTY(int repeatMinutes READ repeatMinutes WRITE setRepeatMinutes NOTIFY settingsChanged)

public:
    Doomscroll(AppDock *dock, MediaPlayers *media, Wellbeing *wellbeing, QObject *parent = nullptr);

    QString site() const { return DoomscrollModel::siteId(m_site); }
    QString siteName() const { return DoomscrollModel::siteName(m_site); }
    int minutes() const { return int(m_tracker.totalMs() / 60000); }
    int swipes() const { return m_tracker.swipes(); }
    bool reminding() const { return m_reminding; }
    QString message() const { return m_message; }
    QVariantMap today() const;
    bool quiet() const { return m_quiet; }
    void setQuiet(bool q);

    bool enabled() const { return flag("enabled", true); }
    void setEnabled(bool on);
    bool instagram() const { return flag("instagram", true); }
    void setInstagram(bool on) { store("instagram", on); }
    bool tiktok() const { return flag("tiktok", true); }
    void setTiktok(bool on) { store("tiktok", on); }
    bool shorts() const { return flag("shorts", true); }
    void setShorts(bool on) { store("shorts", on); }
    bool facebook() const { return flag("facebook", false); }
    void setFacebook(bool on) { store("facebook", on); }
    QString custom() const { return m_settings.value(QStringLiteral("custom")).toString(); }
    void setCustom(const QString &words) { store("custom", words); }
    int limitMinutes() const { return qBound(5, m_settings.value(QStringLiteral("limit"), 15).toInt(), 120); }
    void setLimitMinutes(int m) { store("limit", qBound(5, m, 120)); }
    int swipeLimit() const { return qBound(10, m_settings.value(QStringLiteral("swipes"), 30).toInt(), 200); }
    void setSwipeLimit(int n) { store("swipes", qBound(10, n, 200)); }
    int repeatMinutes() const { return qBound(3, m_settings.value(QStringLiteral("repeat"), 10).toInt(), 60); }
    void setRepeatMinutes(int m) { store("repeat", qBound(3, m, 60)); }

    Q_INVOKABLE void snooze(int minutes); // "5 more minutes": the warning is answered, and comes back later
    Q_INVOKABLE void dismiss();           // a break was taken (or the person looked): answered, the count starts over
    Q_INVOKABLE void stopToday();         // no more warnings today
    Q_INVOKABLE void resetToday();
    Q_INVOKABLE void debugFeed(const QString &site, int seconds); // `notch --doom-debug`: pretend to scroll that long

    // One look at the screen (the timer calls it; tests and the debug flag too).
    void sample(const DoomscrollModel::What &what, bool active, qint64 now);

signals:
    void changed();
    void settingsChanged();
    void remindingChanged();

private:
    bool flag(const char *key, bool def) const { return m_settings.value(QLatin1String(key), def).toBool(); }
    void store(const char *key, const QVariant &v);
    void applySettings();
    void rollDay();
    void look();

    AppDock *m_dock;
    MediaPlayers *m_media;
    Wellbeing *m_wellbeing;
    QSettings m_settings;
    QTimer m_timer, m_answer;
    DoomscrollModel::Tracker m_tracker;
    DoomscrollModel::Site m_site = DoomscrollModel::Site::None;
    bool m_reminding = false, m_quiet = false;
    QString m_message;
    QString m_day;
    QHash<int, qint64> m_todayMs; // by site
    qint64 m_lastSampleAt = 0;
    qint64 m_savedAt = 0;
};
