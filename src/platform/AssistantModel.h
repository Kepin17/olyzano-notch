#pragma once

#include "platform/AssistantText.h"

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

// What was said, as a command: the words from the speech recognizer (Indonesian or English) become one Intent, and
// a spoken app name is matched to the installed apps. No device, no recognizer, no clock, so tests can drive it.
// Nothing that destroys anything (close, shut down, delete) is understood on purpose.
namespace AssistantModel {

enum class Kind { Unknown, OpenApp, OpenBrowser, OpenSite, Search, Volume, Media, Timer, Task, Note };
enum class VolumeAction { Set, Up, Down, Mute, Unmute };
enum class MediaAction { Play, Pause, Next, Previous };

struct Intent {
    Kind kind = Kind::Unknown;
    QString text;           // the app asked for, the search words, the task or the note
    QString engine;         // Search: google, youtube, ... ("" = the person's choice)
    QString filter;         // Search on YouTube: "video" or "channel" ("" = anything)
    int number = -1;        // Volume: the percent (or the step), Timer: the amount (in seconds)
    VolumeAction volume = VolumeAction::Set;
    MediaAction media = MediaAction::Play;
    double score = 1.0;     // 1: the words were understood as said; lower: command words had to be put right first
};

// Lower case, no punctuation (but kept inside "17:30" and "3.5"), one space between words, and the polite and
// calling words taken out: "Hei Notch, tolong buka Firefox dong!" -> "buka firefox".
inline bool isFiller(const QString &w);

// Of the filler words, the ones that are only politeness: kept inside a search ("the best ..."), dropped from its ends.
inline bool isPolite(const QString &w)
{
    static const QSet<QString> polite{QStringLiteral("tolong"), QStringLiteral("dong"), QStringLiteral("deh"), QStringLiteral("ya"), QStringLiteral("please"),
                                      QStringLiteral("hey"), QStringLiteral("hei"), QStringLiteral("hai"), QStringLiteral("notch"), QStringLiteral("ok"),
                                      QStringLiteral("okay"), QStringLiteral("saja"), QStringLiteral("aja"), QStringLiteral("kak")};
    return polite.contains(w);
}

inline QString normalize(const QString &said, bool keepFillers = false)
{
    QString s = said.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}:.]+")), QStringLiteral(" "));
    s.replace(QRegularExpression(QStringLiteral("(?<![\\p{L}\\p{N}])[.:]|[.:](?![\\p{L}\\p{N}])")), QStringLiteral(" "));
    QStringList out;
    for (const QString &w : s.split(u' ', Qt::SkipEmptyParts))
        if (keepFillers || !isFiller(w))
            out << w;
    return out.join(u' ');
}

// The polite and calling words that are no part of a command ("tolong", "please", "hei notch").
inline bool isFiller(const QString &w)
{
    static const QSet<QString> filler{QStringLiteral("tolong"), QStringLiteral("dong"), QStringLiteral("deh"), QStringLiteral("ya"),
                                      QStringLiteral("please"), QStringLiteral("hey"), QStringLiteral("hei"), QStringLiteral("hai"),
                                      QStringLiteral("notch"), QStringLiteral("ok"), QStringLiteral("okay"), QStringLiteral("saja"),
                                      QStringLiteral("aja"), QStringLiteral("kak"), QStringLiteral("bisa"), QStringLiteral("can"),
                                      QStringLiteral("you"), QStringLiteral("could"), QStringLiteral("saya"), QStringLiteral("aku"),
                                      QStringLiteral("me"), QStringLiteral("a"), QStringLiteral("the"), QStringLiteral("an")};
    return filler.contains(w);
}

// ---- numbers: digits, and Indonesian or English words ("lima belas", "twenty five") up to 999 ----
inline int unitWord(const QString &w)
{
    static const QHash<QString, int> units{
        {QStringLiteral("nol"), 0}, {QStringLiteral("kosong"), 0}, {QStringLiteral("zero"), 0},
        {QStringLiteral("satu"), 1}, {QStringLiteral("one"), 1}, {QStringLiteral("dua"), 2}, {QStringLiteral("two"), 2},
        {QStringLiteral("tiga"), 3}, {QStringLiteral("three"), 3}, {QStringLiteral("empat"), 4}, {QStringLiteral("four"), 4},
        {QStringLiteral("lima"), 5}, {QStringLiteral("five"), 5}, {QStringLiteral("enam"), 6}, {QStringLiteral("six"), 6},
        {QStringLiteral("tujuh"), 7}, {QStringLiteral("seven"), 7}, {QStringLiteral("delapan"), 8}, {QStringLiteral("eight"), 8},
        {QStringLiteral("sembilan"), 9}, {QStringLiteral("nine"), 9}};
    return units.value(w, -1);
}

inline int englishTeen(const QString &w)
{
    static const QHash<QString, int> v{
        {QStringLiteral("ten"), 10}, {QStringLiteral("eleven"), 11}, {QStringLiteral("twelve"), 12}, {QStringLiteral("thirteen"), 13},
        {QStringLiteral("fourteen"), 14}, {QStringLiteral("fifteen"), 15}, {QStringLiteral("sixteen"), 16}, {QStringLiteral("seventeen"), 17},
        {QStringLiteral("eighteen"), 18}, {QStringLiteral("nineteen"), 19}, {QStringLiteral("sepuluh"), 10}, {QStringLiteral("sebelas"), 11}};
    return v.value(w, -1);
}

inline int englishTens(const QString &w)
{
    static const QHash<QString, int> v{
        {QStringLiteral("twenty"), 20}, {QStringLiteral("thirty"), 30}, {QStringLiteral("forty"), 40}, {QStringLiteral("fifty"), 50},
        {QStringLiteral("sixty"), 60}, {QStringLiteral("seventy"), 70}, {QStringLiteral("eighty"), 80}, {QStringLiteral("ninety"), 90}};
    return v.value(w, -1);
}

// The number that starts at words[at]; `used` is how many words it took; -1 when there is none.
inline int numberAt(const QStringList &words, int at, int *used)
{
    *used = 0;
    if (at >= words.size())
        return -1;
    const QString &w = words.at(at);
    bool digits = false;
    const int d = w.toInt(&digits);
    if (digits) {
        *used = 1;
        return d;
    }
    const auto next = [&](int i) { return at + i < words.size() ? words.at(at + i) : QString(); };
    int value = -1, n = 0;
    if (w == QLatin1String("seratus") || (w == QLatin1String("a") && next(1) == QLatin1String("hundred")) || w == QLatin1String("hundred")) {
        value = 100;
        n = w == QLatin1String("a") ? 2 : 1;
    } else if (const int teen = englishTeen(w); teen >= 0) {
        value = teen;
        n = 1;
    } else if (const int tens = englishTens(w); tens >= 0) {
        value = tens;
        n = 1;
        if (const int u = unitWord(next(1)); u > 0) { // twenty five
            value += u;
            n = 2;
        }
    } else if (const int u = unitWord(w); u >= 0) {
        value = u;
        n = 1;
        const QString a = next(1);
        if (a == QLatin1String("belas") && u > 0) { // lima belas
            value = 10 + u;
            n = 2;
        } else if (a == QLatin1String("puluh") && u > 0) { // dua puluh (lima)
            value = u * 10;
            n = 2;
            if (const int u2 = unitWord(next(2)); u2 > 0) {
                value += u2;
                n = 3;
            }
        } else if (a == QLatin1String("ratus") && u > 0) { // dua ratus ...
            value = u * 100;
            n = 2;
            int u2used = 0;
            const int rest = numberAt(words, at + 2, &u2used);
            if (rest > 0 && rest < 100) {
                value += rest;
                n += u2used;
            }
        } else if (a == QLatin1String("hundred") && u > 0) {
            value = u * 100;
            n = 2;
        }
    }
    *used = value >= 0 ? n : 0;
    return value;
}

// The first number anywhere in the words, with the index it was found at; -1 when none.
inline int findNumber(const QStringList &words, int from, int *at, int *used)
{
    for (int i = from; i < words.size(); ++i) {
        const int v = numberAt(words, i, used);
        if (v >= 0) {
            *at = i;
            return v;
        }
    }
    *at = -1;
    return -1;
}

// ---- the words of each command ----
inline bool hasAny(const QStringList &words, std::initializer_list<const char *> any)
{
    for (const char *a : any)
        if (words.contains(QLatin1String(a)))
            return true;
    return false;
}

inline QString joinFrom(const QStringList &words, int from)
{
    return from < words.size() ? words.mid(from).join(u' ') : QString();
}

inline QString dropWords(QString text, std::initializer_list<const char *> words)
{
    QStringList kept;
    for (const QString &w : text.split(u' ', Qt::SkipEmptyParts)) {
        bool drop = false;
        for (const char *d : words)
            drop = drop || w == QLatin1String(d);
        if (!drop)
            kept << w;
    }
    return kept.join(u' ');
}

// An Indonesian (or English) time in a task as the words the task reader (TasksModel::parseQuick) knows: "besok jam 5 sore" ->
// "tomorrow 17:00", "nanti malam" -> "today 20:00", "senin jam 9" -> "mon 9:00", "lusa" -> "in 2 days".
inline QString taskText(const QString &text)
{
    QString s = text;
    const auto sw = [&s](const char *re, const char *to) { s.replace(QRegularExpression(QString::fromLatin1(re)), QString::fromLatin1(to)); };
    sw("\\bhari ini\\b", "today");
    sw("\\bbesok\\b", "tomorrow");
    sw("\\blusa\\b", "in 2 days");
    sw("\\bsenin\\b", "mon");
    sw("\\bselasa\\b", "tue");
    sw("\\brabu\\b", "wed");
    sw("\\bkamis\\b", "thu");
    sw("\\bjum'?at\\b|\\bjumat\\b", "fri");
    sw("\\bsabtu\\b", "sat");
    sw("\\bminggu\\b(?!\\s+(?:depan|ini|lalu))", "sun");
    // "jam 5 sore", "pukul 17.30", "at 5 pm": the hour on a 24 hour clock
    static const QRegularExpression at(QStringLiteral("\\b(?:jam|pukul|at)\\s+(\\d{1,2})(?:[:.](\\d{2}))?\\s*(pagi|siang|sore|malam|am|pm)?\\b"));
    QString out;
    qsizetype last = 0;
    bool timed = false;
    for (auto it = at.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        int h = m.captured(1).toInt();
        const QString part = m.captured(3);
        if (part == QLatin1String("pagi") || part == QLatin1String("am"))
            h = h == 12 ? 0 : h;
        else if (part == QLatin1String("siang"))
            h = h < 11 ? h + 12 : h;
        else if (part == QLatin1String("sore") || part == QLatin1String("pm"))
            h = h < 12 ? h + 12 : h;
        else if (part == QLatin1String("malam"))
            h = h == 12 ? 0 : h >= 6 && h < 12 ? h + 12 : h;
        if (h < 24) {
            out += s.mid(last, m.capturedStart() - last) + QString::number(h) + u':' + (m.captured(2).isEmpty() ? QStringLiteral("00") : m.captured(2));
            last = m.capturedEnd();
            timed = true;
        }
    }
    s = out + s.mid(last);
    if (!timed) { // a part of the day without an hour
        sw("\\b(tomorrow|besok)\\s+pagi\\b", "tomorrow 9:00");
        sw("\\b(tomorrow|besok)\\s+siang\\b", "tomorrow 12:00");
        sw("\\b(tomorrow|besok)\\s+sore\\b", "tomorrow 17:00");
        sw("\\b(tomorrow|besok)\\s+malam\\b", "tomorrow 20:00");
        sw("\\b(?:nanti\\s+)?malam(?:\\s+ini)?\\b", "today 20:00");
        sw("\\b(?:nanti\\s+)?sore(?:\\s+ini)?\\b", "today 17:00");
        sw("\\bsiang(?:\\s+ini)?\\b", "today 12:00");
        sw("\\bpagi(?:\\s+ini)?\\b", "today 9:00");
    }
    return s.simplified();
}

inline int editDistance(const QString &a, const QString &b);

// A web site by its name ("youtube", "google maps") or as a domain ("github.com"): its address, "" when it is neither.
inline const QHash<QString, QString> &siteTable()
{
    static const QHash<QString, QString> sites{
        {QStringLiteral("youtube"), QStringLiteral("https://www.youtube.com/")}, {QStringLiteral("google"), QStringLiteral("https://www.google.com/")},
        {QStringLiteral("gmail"), QStringLiteral("https://mail.google.com/")}, {QStringLiteral("google maps"), QStringLiteral("https://maps.google.com/")},
        {QStringLiteral("maps"), QStringLiteral("https://maps.google.com/")}, {QStringLiteral("google drive"), QStringLiteral("https://drive.google.com/")},
        {QStringLiteral("drive"), QStringLiteral("https://drive.google.com/")}, {QStringLiteral("github"), QStringLiteral("https://github.com/")},
        {QStringLiteral("facebook"), QStringLiteral("https://www.facebook.com/")}, {QStringLiteral("instagram"), QStringLiteral("https://www.instagram.com/")},
        {QStringLiteral("twitter"), QStringLiteral("https://x.com/")}, {QStringLiteral("x"), QStringLiteral("https://x.com/")},
        {QStringLiteral("tiktok"), QStringLiteral("https://www.tiktok.com/")}, {QStringLiteral("netflix"), QStringLiteral("https://www.netflix.com/")},
        {QStringLiteral("linkedin"), QStringLiteral("https://www.linkedin.com/")}, {QStringLiteral("reddit"), QStringLiteral("https://www.reddit.com/")},
        {QStringLiteral("wikipedia"), QStringLiteral("https://www.wikipedia.org/")}, {QStringLiteral("tokopedia"), QStringLiteral("https://www.tokopedia.com/")},
        {QStringLiteral("shopee"), QStringLiteral("https://shopee.co.id/")}, {QStringLiteral("bing"), QStringLiteral("https://www.bing.com/")},
        {QStringLiteral("duckduckgo"), QStringLiteral("https://duckduckgo.com/")}, {QStringLiteral("chatgpt"), QStringLiteral("https://chatgpt.com/")}};
    return sites;
}

inline QString siteUrl(const QString &said)
{
    const QString q = said.trimmed().toLower();
    if (const QString hit = siteTable().value(q); !hit.isEmpty())
        return hit;
    static const QRegularExpression domain(QStringLiteral("^[a-z0-9-]+(\\.[a-z0-9-]+)*\\.(com|id|net|org|io|co|dev|app|ai|me|tv)(/\\S*)?$"));
    if (domain.match(q).hasMatch())
        return QStringLiteral("https://") + q;
    return {};
}

// The same, for a site name heard a little off ("youtueb", "google masp"): the whole phrase, or its last one or two words
// ("situ tokopedia"). Strict, so a real app name is not taken for a site.
inline QString siteUrlHeard(const QString &said)
{
    if (const QString exact = siteUrl(said); !exact.isEmpty())
        return exact;
    const QStringList words = said.trimmed().toLower().split(u' ', Qt::SkipEmptyParts);
    QStringList names;
    const auto keys = siteTable().keys();
    for (const QString &k : keys)
        names << k;
    for (int take = std::min<int>(2, int(words.size())); take >= 1; --take) {
        const QString phrase = words.mid(words.size() - take).join(u' ');
        if (phrase.size() < 4)
            continue;
        double sc = 0;
        const QString near = AssistantText::nearest(phrase, names, &sc, take == 1 ? 0.84 : 0.9);
        if (!near.isEmpty())
            return siteTable().value(near);
    }
    return {};
}

// Heard a little off ("brouser", "perambah"): is it the browser that was asked for?
inline bool looksLikeBrowser(const QString &said)
{
    const QString q = said.trimmed().toLower().remove(u' ');
    if (q.size() < 5)
        return false;
    return editDistance(q, QStringLiteral("browser")) <= 2 || editDistance(q, QStringLiteral("peramban")) <= 2 || editDistance(q, QStringLiteral("webbrowser")) <= 2
           || q == QLatin1String("internet") || q == QLatin1String("browsernya");
}

inline Intent parseExact(const QString &said)
{
    Intent in;
    const QString clean = normalize(said);
    const QStringList w = clean.split(u' ', Qt::SkipEmptyParts);
    if (w.isEmpty())
        return in;
    const QString first = w.first();
    const auto rest = [&](int from = 1) { return joinFrom(w, from); };

    // a timer: "timer 10 menit", "atur timer lima menit untuk teh", "set timer for ten minutes", "setengah jam",
    // "satu setengah jam", "seperempat jam", "1.5 jam"
    if (hasAny(w, {"timer", "pengatur", "alarm"})) {
        const auto unitSeconds = [](const QString &u) {
            return (u == QLatin1String("detik") || u.startsWith(QLatin1String("sec"))) ? 1
                 : (u == QLatin1String("menit") || u.startsWith(QLatin1String("min"))) ? 60
                 : (u == QLatin1String("jam") || u.startsWith(QLatin1String("hour")) || u == QLatin1String("hr")) ? 3600 : 0;
        };
        double seconds = 0;
        int numberAtWord = -1;
        for (int i = 0; i < w.size() && seconds <= 0; ++i) {
            bool dec = false;
            const double d = w.at(i).contains(u'.') ? w.at(i).toDouble(&dec) : 0.0;
            int used = 0;
            const double n = dec ? d : double(numberAt(w, i, &used));
            const int after = i + (dec ? 1 : std::max(used, 1));
            if (!dec && used == 0) {
                // no number: "setengah jam", "seperempat jam", "half an hour"
                const QString x = w.at(i);
                const bool half = x == QLatin1String("setengah") || x == QLatin1String("half") || x == QLatin1String("separuh");
                const bool quarter = x == QLatin1String("seperempat") || x == QLatin1String("quarter");
                if ((half || quarter) && after < w.size()) {
                    const int u = unitSeconds(w.at(after));
                    if (u > 0)
                        seconds = u * (half ? 0.5 : 0.25);
                }
                continue;
            }
            if (n < 0 || after > w.size())
                continue;
            numberAtWord = i;
            double more = 0; // "satu setengah jam": and a half
            int unitAt = after;
            if (unitAt + 1 < w.size() && (w.at(unitAt) == QLatin1String("setengah") || w.at(unitAt) == QLatin1String("half"))) {
                more = 0.5;
                ++unitAt;
            }
            if (unitAt < w.size()) {
                const int u = unitSeconds(w.at(unitAt));
                if (u > 0)
                    seconds = (n + more) * u;
            }
        }
        if (seconds > 0) {
            in.kind = Kind::Timer;
            in.number = int(seconds + 0.5);
            const int lab = std::max(w.indexOf(QStringLiteral("untuk")), w.indexOf(QStringLiteral("for")));
            int at = 0, used = 0;
            if (lab >= 0 && lab + 1 < w.size() && findNumber(w, lab + 1, &at, &used) < 0)
                in.text = rest(lab + 1);
            Q_UNUSED(numberAtWord)
            return in;
        }
    }

    // a task: "tambah tugas beli susu besok jam 5", "add task buy milk tomorrow 17:00", "ingatkan ... "
    if (hasAny(w, {"tugas", "task", "todo", "pengingat", "reminder"}) && hasAny(w, {"tambah", "tambahkan", "buat", "buatkan", "add", "create", "new", "baru"})) {
        QString t = dropWords(clean, {"tambah", "tambahkan", "buat", "buatkan", "add", "create", "new", "baru", "tugas", "task", "todo", "pengingat", "reminder", "untuk", "to", "bernama", "named", "called"});
        if (!t.isEmpty()) {
            in.kind = Kind::Task;
            in.text = taskText(t);
            return in;
        }
    }
    if ((first == QLatin1String("ingatkan") || first == QLatin1String("remind")) && w.size() > 1) {
        in.kind = Kind::Task;
        in.text = taskText(dropWords(rest(), {"untuk", "to", "bahwa", "that"}));
        return in;
    }

    // a note: "catat beli susu", "note call mom"
    if (first == QLatin1String("catat") || first == QLatin1String("catatkan") || first == QLatin1String("note") || first == QLatin1String("tulis")) {
        const QString t = rest();
        if (!t.isEmpty()) {
            in.kind = Kind::Note;
            in.text = t;
            return in;
        }
    }

    // a search: "cari resep nasi goreng", "search for cats on youtube", "cari video kucing", "cari channel Nadia di youtube",
    // "buka youtube dan cari musik", "di youtube cari kucing"; "cari youtube" alone just opens it
    {
        static const QStringList searchVerbs{QStringLiteral("cari"), QStringLiteral("carikan"), QStringLiteral("search"), QStringLiteral("telusuri"),
                                             QStringLiteral("find"), QStringLiteral("google")};
        static const QStringList engines{QStringLiteral("google"), QStringLiteral("youtube"), QStringLiteral("bing"), QStringLiteral("duckduckgo"),
                                         QStringLiteral("wikipedia")};
        static const QStringList lead{QStringLiteral("buka"), QStringLiteral("open"), QStringLiteral("di"), QStringLiteral("in"), QStringLiteral("on"),
                                      QStringLiteral("ke"), QStringLiteral("dan"), QStringLiteral("and"), QStringLiteral("lalu"), QStringLiteral("then"),
                                      QStringLiteral("terus"), QStringLiteral("pada")};
        const auto isPlace = [](const QString &x) { return x == QLatin1String("di") || x == QLatin1String("in") || x == QLatin1String("on") || x == QLatin1String("pada"); };
        const QStringList wk = normalize(said, true).split(u' ', Qt::SkipEmptyParts); // every word, so the search is for what was said
        int vi = -1;
        for (int i = 0; i < wk.size() && vi < 0; ++i) {
            if (!searchVerbs.contains(wk.at(i)))
                continue;
            if (wk.at(i) == QLatin1String("google") && i != 0)
                continue; // "buka google" is a place, not a verb
            bool prefixOk = true;
            for (int k = 0; k < i; ++k)
                prefixOk = prefixOk && (lead.contains(wk.at(k)) || engines.contains(wk.at(k)) || isFiller(wk.at(k)));
            if (prefixOk)
                vi = i;
        }
        if (vi >= 0) {
            QStringList q = wk.mid(vi + 1);
            while (!q.isEmpty() && isPolite(q.first()))
                q.removeFirst();
            while (!q.isEmpty() && isPolite(q.last()))
                q.removeLast();
            for (int k = 0; k < vi; ++k)
                if (engines.contains(wk.at(k)))
                    in.engine = wk.at(k);
            if (wk.at(vi) == QLatin1String("google") && in.engine.isEmpty())
                in.engine = QStringLiteral("google");
            const auto dropLead = [&] {
                while (!q.isEmpty() && (q.first() == QLatin1String("for") || q.first() == QLatin1String("tentang") || q.first() == QLatin1String("untuk")
                                        || q.first() == QLatin1String("about") || q.first() == QLatin1String("dari") || q.first() == QLatin1String("from")))
                    q.removeFirst();
            };
            dropLead();
            // a search engine heard a little off ("youutube") is still that engine
            const auto engineOf = [&](const QString &x) {
                if (engines.contains(x))
                    return x;
                double sc = 0;
                return x.size() >= 5 ? AssistantText::nearest(x, engines, &sc, 0.84) : QString();
            };
            if (q.size() >= 2 && isPlace(q.first()) && !engineOf(q.at(1)).isEmpty()) { // "cari di youtube kucing"
                in.engine = engineOf(q.at(1));
                q = q.mid(2);
            } else if (!q.isEmpty() && !engineOf(q.first()).isEmpty() && engineOf(q.first()) != QLatin1String("google")) { // "cari youtube kucing"
                in.engine = engineOf(q.first());
                q.removeFirst();
            }
            if (q.size() >= 2 && isPlace(q.at(q.size() - 2)) && !engineOf(q.last()).isEmpty()) { // "... di youtube"
                in.engine = engineOf(q.last());
                q = q.mid(0, q.size() - 2);
            } else if (q.size() >= 3 && isPlace(q.at(q.size() - 3)) && (q.last() == QLatin1String("internet") || q.last() == QLatin1String("web"))) {
                q = q.mid(0, q.size() - 3);
            }
            dropLead();
            // "video ..." or "channel ..." means YouTube, with that kind of result
            if (!q.isEmpty()) {
                static const QStringList kinds{QStringLiteral("video"), QStringLiteral("videos"), QStringLiteral("vidio"), QStringLiteral("channel"),
                                               QStringLiteral("channels"), QStringLiteral("saluran"), QStringLiteral("kanal")};
                double fsc = 0;
                const QString near = q.first().size() >= 5 ? AssistantText::nearest(q.first(), kinds, &fsc, 0.84) : QString(); // "viddeo", "khannel"
                const QString f = near.isEmpty() ? q.first() : near;
                if (f == QLatin1String("video") || f == QLatin1String("videos") || f == QLatin1String("vidio")) {
                    in.filter = QStringLiteral("video");
                    q.removeFirst();
                } else if (f == QLatin1String("channel") || f == QLatin1String("channels") || f == QLatin1String("saluran") || f == QLatin1String("kanal")) {
                    in.filter = QStringLiteral("channel");
                    q.removeFirst();
                }
                if (!in.filter.isEmpty()) {
                    if (in.engine.isEmpty())
                        in.engine = QStringLiteral("youtube");
                    dropLead();
                    if (!q.isEmpty() && (q.first() == QLatin1String("bernama") || q.first() == QLatin1String("named") || q.first() == QLatin1String("called")))
                        q.removeFirst();
                }
            }
            if (!q.isEmpty()) {
                in.kind = Kind::Search;
                in.text = q.join(u' ');
                return in;
            }
            if (!in.engine.isEmpty()) { // "cari youtube": no words to look for, so just open it
                in.kind = Kind::OpenSite;
                in.text = siteUrl(in.engine);
                in.engine.clear();
                in.filter.clear();
                return in;
            }
        }
    }

    // volume: "volume 50", "volume lima puluh persen", "naikkan volume", "turunkan volume 20", "mute", "bisukan"
    if (hasAny(w, {"volume", "suara", "mute", "unmute", "bisukan", "senyap"})) {
        int at = 0, used = 0;
        const int n = findNumber(w, 0, &at, &used);
        if (hasAny(w, {"unmute", "hidupkan", "nyalakan"}) && hasAny(w, {"suara", "unmute", "volume"})) {
            in.kind = Kind::Volume;
            in.volume = VolumeAction::Unmute;
            return in;
        }
        if (hasAny(w, {"mute", "bisukan", "senyap", "matikan"})) {
            in.kind = Kind::Volume;
            in.volume = VolumeAction::Mute;
            return in;
        }
        if (n >= 0 && n <= 100 && hasAny(w, {"ke", "to", "jadi", "menjadi", "sampai", "at"})) { // "naikkan volume ke 80": to 80, not by 80
            in.kind = Kind::Volume;
            in.volume = VolumeAction::Set;
            in.number = n;
            return in;
        }
        if (hasAny(w, {"penuh", "maksimal", "maksimum", "full", "max", "maximum", "tertinggi"})) {
            in.kind = Kind::Volume;
            in.volume = VolumeAction::Set;
            in.number = 100;
            return in;
        }
        if (hasAny(w, {"setengah", "half", "separuh"}) && n < 0) {
            in.kind = Kind::Volume;
            in.volume = VolumeAction::Set;
            in.number = 50;
            return in;
        }
        if (hasAny(w, {"naik", "naikkan", "up", "increase", "raise", "louder", "keras", "kerasin", "besarkan", "tambah"})) {
            in.kind = Kind::Volume;
            in.volume = VolumeAction::Up;
            in.number = n >= 0 ? n : 10;
            return in;
        }
        if (hasAny(w, {"turun", "turunkan", "down", "decrease", "lower", "quieter", "pelan", "pelankan", "kecilkan", "kurangi"})) {
            in.kind = Kind::Volume;
            in.volume = VolumeAction::Down;
            in.number = n >= 0 ? n : 10;
            return in;
        }
        if (n >= 0 && n <= 100) {
            in.kind = Kind::Volume;
            in.volume = VolumeAction::Set;
            in.number = n;
            return in;
        }
    }

    // music: only when it is about music, or the command is the bare word ("pause", "jeda")
    const bool aboutMusic = hasAny(w, {"musik", "music", "lagu", "song", "track", "lagunya"});
    const bool bare = w.size() <= 2;
    if (aboutMusic || bare) {
        if (hasAny(w, {"berikutnya", "selanjutnya", "next", "skip", "lewati", "ganti"}) && (aboutMusic || bare)) {
            in.kind = Kind::Media;
            in.media = MediaAction::Next;
            return in;
        }
        if (hasAny(w, {"sebelumnya", "previous", "prev"})) {
            in.kind = Kind::Media;
            in.media = MediaAction::Previous;
            return in;
        }
        if (hasAny(w, {"jeda", "pause", "hentikan", "stop", "berhenti"})) {
            in.kind = Kind::Media;
            in.media = MediaAction::Pause;
            return in;
        }
        if (hasAny(w, {"putar", "play", "mainkan", "lanjut", "lanjutkan", "resume", "setel"}) && (aboutMusic || bare)) {
            in.kind = Kind::Media;
            in.media = MediaAction::Play;
            return in;
        }
    }

    // a question or a wish for information: the whole sentence is the search ("info vga yang murah", "harga laptop gaming",
    // "what is a vpn"); nothing else is guessed, so talk in the room does not open the browser
    {
        static const QSet<QString> asks{QStringLiteral("info"), QStringLiteral("informasi"), QStringLiteral("tentang"), QStringLiteral("apa"),
                                        QStringLiteral("siapa"), QStringLiteral("kapan"), QStringLiteral("dimana"), QStringLiteral("berapa"),
                                        QStringLiteral("harga"), QStringLiteral("review"), QStringLiteral("tutorial"), QStringLiteral("cara"),
                                        QStringLiteral("bagaimana"), QStringLiteral("mengapa"), QStringLiteral("kenapa"), QStringLiteral("what"),
                                        QStringLiteral("who"), QStringLiteral("when"), QStringLiteral("where"), QStringLiteral("why"), QStringLiteral("how"),
                                        QStringLiteral("price"), QStringLiteral("rekomendasi"), QStringLiteral("recommend"), QStringLiteral("definisi"),
                                        QStringLiteral("artinya"), QStringLiteral("meaning"), QStringLiteral("berita"), QStringLiteral("news")};
        QStringList all = normalize(said, true).split(u' ', Qt::SkipEmptyParts);
        while (!all.isEmpty() && isFiller(all.first()))
            all.removeFirst();
        while (!all.isEmpty() && isPolite(all.last()))
            all.removeLast();
        if (all.size() >= 2 && asks.contains(all.first())) {
            in.kind = Kind::Search;
            in.text = all.join(u' ');
            return in;
        }
    }

    // open: "buka firefox", "open the browser", "jalankan terminal"
    static const QStringList openVerbs{QStringLiteral("buka"), QStringLiteral("bukakan"), QStringLiteral("open"), QStringLiteral("launch"),
                                       QStringLiteral("jalankan"), QStringLiteral("start"), QStringLiteral("run"), QStringLiteral("nyalakan")};
    if (openVerbs.contains(first) && w.size() > 1) {
        const QString what = dropWords(rest(), {"aplikasi", "app", "application", "program", "the", "ke", "situs", "website", "web", "halaman", "laman", "site"});
        const QString plain = dropWords(rest(), {"aplikasi", "app", "application", "program", "the", "ke"});
        if (plain == QLatin1String("browser") || plain == QLatin1String("peramban") || plain == QLatin1String("internet") || plain == QLatin1String("web browser")
            || plain == QLatin1String("web") || plain == QLatin1String("browsers") || looksLikeBrowser(plain)) {
            in.kind = Kind::OpenBrowser;
            return in;
        }
        if (const QString url = siteUrlHeard(what); !url.isEmpty()) {
            in.kind = Kind::OpenSite;
            in.text = url;
            return in;
        }
        if (!what.isEmpty()) {
            in.kind = Kind::OpenApp;
            in.text = what;
            return in;
        }
    }
    return in;
}

// What was said, as a command. A sentence that is not understood as it stands is tried again with its garbled command words put
// right ("buku youtub" -> "buka youtube"): the first word towards the verbs, later long words towards video, channel, menit, ...
inline bool sameIntent(const Intent &a, const Intent &b)
{
    return a.kind == b.kind && a.text == b.text && a.number == b.number && a.volume == b.volume && a.media == b.media && a.engine == b.engine
           && a.filter == b.filter;
}

inline Intent parse(const QString &said)
{
    Intent in = parseExact(said);
    const QString heard = normalize(said, true);
    QStringList words = heard.split(u' ', Qt::SkipEmptyParts);
    if (words.isEmpty())
        return in;
    int firstAt = 0;
    while (firstAt + 1 < words.size() && isPolite(words.at(firstAt)))
        ++firstAt;
    // A first word that is a verb as it stands is trusted: only the marker words after it (video, menit, down, twenty ...) are put
    // right, never the words of a search. Any other first word may be a verb heard a little off.
    const bool strong = AssistantText::verbs().contains(words.at(firstAt));
    const AssistantText::Repair fix = AssistantText::repair(heard, !strong);
    if (fix.changed && fix.score >= 0.86) {
        const Intent again = parseExact(fix.text);
        // a different reading counts when the action differs, not when only the words of a search do
        const bool differs = in.kind != again.kind || in.volume != again.volume || in.media != again.media || in.number != again.number
                             || in.engine != again.engine || in.filter != again.filter;
        if (again.kind != Kind::Unknown && (in.kind == Kind::Unknown || differs)) {
            Intent out = again;
            out.score = fix.score;
            return out;
        }
    }
    if (in.kind != Kind::Unknown)
        return in;
    // The first word may have been one of several verbs ("bua": buka or buat): the sentence says which makes sense.
    if (!strong && words.size() >= 2) {
        QStringList repaired = fix.text.split(u' ', Qt::SkipEmptyParts);
        for (const QString &verb : AssistantText::ranked(words.first(), AssistantText::verbs())) {
            repaired[0] = verb;
            Intent again = parseExact(repaired.join(u' '));
            if (again.kind != Kind::Unknown) {
                again.score = 0.85;
                return again;
            }
        }
    }
    return in;
}

// ---- which app was meant ----
inline int editDistance(const QString &a, const QString &b)
{
    const int n = a.size(), m = b.size();
    QList<int> prev(m + 1), cur(m + 1);
    for (int j = 0; j <= m; ++j)
        prev[j] = j;
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        for (int j = 1; j <= m; ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a.at(i - 1) == b.at(j - 1) ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[m];
}

struct AppMatch { int index = -1; double score = 0; bool sure = false; double second = 0; };

// An installed app as the launcher knows it.
struct AppInfo {
    QString name;     // "Firefox Web Browser"
    QString keywords; // "internet web browser" (the desktop entry's keywords and generic name)
    int uses = 0;     // how often it was opened through Notch
};

// What people say for what the apps are called (Indonesian, English, the common program names): the words an app's name or
// keywords can hold instead.
inline QStringList spokenAlternatives(const QString &q)
{
    static const QHash<QString, QStringList> syn{
        {QStringLiteral("kalkulator"), {QStringLiteral("calculator"), QStringLiteral("kcalc")}},
        {QStringLiteral("pengaturan"), {QStringLiteral("settings"), QStringLiteral("system settings"), QStringLiteral("configuration")}},
        {QStringLiteral("setelan"), {QStringLiteral("settings")}},
        {QStringLiteral("penjelajah berkas"), {QStringLiteral("files"), QStringLiteral("file manager"), QStringLiteral("dolphin"), QStringLiteral("nautilus")}},
        {QStringLiteral("manajer berkas"), {QStringLiteral("file manager"), QStringLiteral("files"), QStringLiteral("dolphin")}},
        {QStringLiteral("berkas"), {QStringLiteral("files"), QStringLiteral("file manager")}},
        {QStringLiteral("file manager"), {QStringLiteral("files"), QStringLiteral("dolphin"), QStringLiteral("nautilus"), QStringLiteral("thunar")}},
        {QStringLiteral("explorer"), {QStringLiteral("files"), QStringLiteral("file manager"), QStringLiteral("dolphin")}},
        {QStringLiteral("terminal"), {QStringLiteral("konsole"), QStringLiteral("terminal"), QStringLiteral("console")}},
        {QStringLiteral("konsol"), {QStringLiteral("konsole")}},
        {QStringLiteral("editor teks"), {QStringLiteral("text editor"), QStringLiteral("kate"), QStringLiteral("gedit")}},
        {QStringLiteral("teks editor"), {QStringLiteral("text editor"), QStringLiteral("kate"), QStringLiteral("gedit")}},
        {QStringLiteral("catatan"), {QStringLiteral("notes"), QStringLiteral("note")}},
        {QStringLiteral("kalender"), {QStringLiteral("calendar")}},
        {QStringLiteral("kamera"), {QStringLiteral("camera"), QStringLiteral("webcam")}},
        {QStringLiteral("foto"), {QStringLiteral("photos"), QStringLiteral("gallery"), QStringLiteral("gwenview")}},
        {QStringLiteral("galeri"), {QStringLiteral("gallery"), QStringLiteral("photos"), QStringLiteral("gwenview")}},
        {QStringLiteral("gambar"), {QStringLiteral("image viewer"), QStringLiteral("gwenview")}},
        {QStringLiteral("pemutar musik"), {QStringLiteral("music player"), QStringLiteral("music")}},
        {QStringLiteral("musik"), {QStringLiteral("music player"), QStringLiteral("music")}},
        {QStringLiteral("pemutar video"), {QStringLiteral("video player"), QStringLiteral("vlc"), QStringLiteral("media player")}},
        {QStringLiteral("video"), {QStringLiteral("video player"), QStringLiteral("vlc"), QStringLiteral("media player")}},
        {QStringLiteral("pdf"), {QStringLiteral("pdf viewer"), QStringLiteral("okular"), QStringLiteral("document viewer")}},
        {QStringLiteral("dokumen"), {QStringLiteral("document"), QStringLiteral("writer"), QStringLiteral("word processor")}},
        {QStringLiteral("spreadsheet"), {QStringLiteral("calc"), QStringLiteral("spreadsheet")}},
        {QStringLiteral("lembar kerja"), {QStringLiteral("calc"), QStringLiteral("spreadsheet")}},
        {QStringLiteral("presentasi"), {QStringLiteral("impress"), QStringLiteral("presentation")}},
        {QStringLiteral("monitor sistem"), {QStringLiteral("system monitor"), QStringLiteral("task manager")}},
        {QStringLiteral("manajer tugas"), {QStringLiteral("system monitor"), QStringLiteral("task manager")}},
        {QStringLiteral("task manager"), {QStringLiteral("system monitor")}},
        {QStringLiteral("unduhan"), {QStringLiteral("downloads"), QStringLiteral("download")}},
        {QStringLiteral("toko aplikasi"), {QStringLiteral("discover"), QStringLiteral("software"), QStringLiteral("store")}},
        {QStringLiteral("pesan"), {QStringLiteral("messages"), QStringLiteral("messaging")}},
        {QStringLiteral("surel"), {QStringLiteral("email"), QStringLiteral("mail")}},
        {QStringLiteral("email"), {QStringLiteral("mail"), QStringLiteral("thunderbird"), QStringLiteral("kmail")}},
        {QStringLiteral("coding"), {QStringLiteral("code"), QStringLiteral("editor")}},
        {QStringLiteral("vs code"), {QStringLiteral("visual studio code"), QStringLiteral("code")}},
        {QStringLiteral("vscode"), {QStringLiteral("visual studio code"), QStringLiteral("code")}}};
    QStringList out{q};
    out << syn.value(q);
    return out;
}

// Which app was meant. The spoken name is compared, with its spelling and its sound (AssistantText::closeness), with every
// app's name and keywords, also through what people call things (spokenAlternatives); the apps opened most through Notch get
// a little extra, so "code" is the editor you use and not the first one that has the word. `sure` when the best is good and
// clearly ahead of the second.
inline AppMatch matchApp(const QString &said, const QList<AppInfo> &apps)
{
    const QString spoken = normalize(said);
    AppMatch best;
    double second = 0;
    if (spoken.isEmpty())
        return best;
    const QStringList alternatives = spokenAlternatives(spoken);
    const auto strip = [](const QString &x) { return x.toLower().simplified().remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N} ]"))); };
    for (int i = 0; i < apps.size(); ++i) {
        const QString name = strip(apps.at(i).name);
        if (name.isEmpty())
            continue;
        const QString flat = QString(name).remove(u' ');
        const QStringList nameWords = name.split(u' ', Qt::SkipEmptyParts);
        const QStringList keyWords = strip(apps.at(i).keywords).split(u' ', Qt::SkipEmptyParts);
        double s = 0;
        for (int a = 0; a < alternatives.size(); ++a) {
            const QString alt = strip(alternatives.at(a));
            const QString altFlat = QString(alt).remove(u' ');
            if (altFlat.isEmpty())
                continue;
            const double penalty = a == 0 ? 1.0 : 0.97; // what was said beats what it may stand for
            double t;
            if (flat == altFlat)
                t = 1.0;
            else if (flat.startsWith(altFlat) || altFlat.startsWith(flat))
                t = 0.92;
            else if (flat.contains(altFlat) && altFlat.size() >= 3)
                t = 0.88;
            else
                t = AssistantText::closeness(altFlat, flat);
            // one word of the name ("writer" in "LibreOffice Writer"), or of the keywords ("calculator", "file manager")
            for (const QString &word : nameWords)
                if (word.size() >= 3 && altFlat.size() >= 3)
                    t = std::max(t, 0.95 * (word == altFlat ? 1.0 : AssistantText::closeness(altFlat, word)));
            for (const QString &word : keyWords)
                if (word.size() >= 3 && altFlat.size() >= 3)
                    t = std::max(t, 0.9 * (word == altFlat ? 1.0 : AssistantText::closeness(altFlat, word)));
            s = std::max(s, t * penalty);
        }
        s += std::min(0.1, 0.03 * std::log2(1.0 + apps.at(i).uses)); // the ones that are used win a tie
        if (s > best.score) {
            second = best.score;
            best = {i, s, false, 0};
        } else if (s > second) {
            second = s;
        }
    }
    best.second = second;
    best.sure = best.index >= 0 && best.score >= 0.7 && (best.score - second >= 0.08 || best.score >= 0.97);
    return best;
}

// The same with names only (no keywords, no usage).
inline AppMatch matchApp(const QString &said, const QStringList &names)
{
    QList<AppInfo> apps;
    for (const QString &n : names)
        apps << AppInfo{n, QString(), 0};
    return matchApp(said, apps);
}

// ---- the web ----
inline QUrl searchUrl(const QString &engine, const QString &query, const QString &filter = QString())
{
    const QString e = engine.toLower();
    QString base, key = QStringLiteral("q");
    if (e == QLatin1String("google")) base = QStringLiteral("https://www.google.com/search");
    else if (e == QLatin1String("bing")) base = QStringLiteral("https://www.bing.com/search");
    else if (e == QLatin1String("youtube")) { base = QStringLiteral("https://www.youtube.com/results"); key = QStringLiteral("search_query"); }
    else if (e == QLatin1String("wikipedia")) { base = QStringLiteral("https://wikipedia.org/w/index.php"); key = QStringLiteral("search"); }
    else base = QStringLiteral("https://duckduckgo.com/");
    QUrl url(base);
    QUrlQuery q;
    q.addQueryItem(key, query);
    if (e == QLatin1String("youtube") && filter == QLatin1String("channel"))
        q.addQueryItem(QStringLiteral("sp"), QStringLiteral("EgIQAg==")); // YouTube's own filter: channels only
    else if (e == QLatin1String("youtube") && filter == QLatin1String("video"))
        q.addQueryItem(QStringLiteral("sp"), QStringLiteral("EgIQAQ==")); // videos only
    url.setQuery(q);
    return url;
}

}
