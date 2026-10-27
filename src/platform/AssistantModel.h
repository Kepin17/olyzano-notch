#pragma once

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

// What was said, as a command: the words from the speech recognizer (Indonesian or English) become one Intent, and
// a spoken app name is matched to the installed apps. No device, no recognizer, no clock, so tests can drive it.
// Nothing that destroys anything (close, shut down, delete) is understood on purpose.
namespace AssistantModel {

enum class Kind { Unknown, OpenApp, OpenBrowser, Search, Volume, Media, Timer, Task, Note };
enum class VolumeAction { Set, Up, Down, Mute, Unmute };
enum class MediaAction { Play, Pause, Next, Previous };

struct Intent {
    Kind kind = Kind::Unknown;
    QString text;           // the app asked for, the search words, the task or the note
    QString engine;         // Search: google, youtube, ... ("" = the person's choice)
    int number = -1;        // Volume: the percent (or the step), Timer: the amount (in seconds)
    VolumeAction volume = VolumeAction::Set;
    MediaAction media = MediaAction::Play;
};

// Lower case, no punctuation (but kept inside "17:30" and "3.5"), one space between words, and the polite and
// calling words taken out: "Hei Notch, tolong buka Firefox dong!" -> "buka firefox".
inline QString normalize(const QString &said)
{
    QString s = said.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}:.]+")), QStringLiteral(" "));
    s.replace(QRegularExpression(QStringLiteral("(?<![0-9])[.:]|[.:](?![0-9])")), QStringLiteral(" "));
    static const QSet<QString> filler{QStringLiteral("tolong"), QStringLiteral("dong"), QStringLiteral("deh"), QStringLiteral("ya"),
                                      QStringLiteral("please"), QStringLiteral("hey"), QStringLiteral("hei"), QStringLiteral("hai"),
                                      QStringLiteral("notch"), QStringLiteral("ok"), QStringLiteral("okay"), QStringLiteral("saja"),
                                      QStringLiteral("aja"), QStringLiteral("kak"), QStringLiteral("bisa"), QStringLiteral("can"),
                                      QStringLiteral("you"), QStringLiteral("could"), QStringLiteral("saya"), QStringLiteral("aku"),
                                      QStringLiteral("me"), QStringLiteral("a"), QStringLiteral("the"), QStringLiteral("an")};
    QStringList out;
    for (const QString &w : s.split(u' ', Qt::SkipEmptyParts))
        if (!filler.contains(w))
            out << w;
    return out.join(u' ');
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

// An Indonesian time in a task ("besok jam 5", "jam 17") as the English words the task reader (TasksModel) knows.
inline QString taskText(const QString &text)
{
    QString s = text;
    s.replace(QRegularExpression(QStringLiteral("\\bhari ini\\b")), QStringLiteral("today"));
    s.replace(QRegularExpression(QStringLiteral("\\bbesok\\b")), QStringLiteral("tomorrow"));
    s.replace(QRegularExpression(QStringLiteral("\\b(?:jam|pukul)\\s+(\\d{1,2})(?:[:.](\\d{2}))?\\s*(pagi|siang|sore|malam)?")),
              QStringLiteral("\\1:\\2"));
    // "tomorrow 5:" -> "tomorrow 5:00"; "sore" / "malam" hours after noon are not guessed: say 17
    s.replace(QRegularExpression(QStringLiteral("(\\d{1,2}):(?!\\d)")), QStringLiteral("\\1:00"));
    return s.simplified();
}

inline Intent parse(const QString &said)
{
    Intent in;
    const QString clean = normalize(said);
    const QStringList w = clean.split(u' ', Qt::SkipEmptyParts);
    if (w.isEmpty())
        return in;
    const QString first = w.first();
    const auto rest = [&](int from = 1) { return joinFrom(w, from); };

    // a timer: "timer 10 menit", "atur timer lima menit untuk teh", "set timer for ten minutes", "half an hour"
    if (hasAny(w, {"timer", "pengatur", "alarm"})) {
        int at = 0, used = 0;
        int amount = findNumber(w, 0, &at, &used);
        int unitAt = at >= 0 ? at + used : -1;
        if (amount < 0 && (hasAny(w, {"setengah", "half"}) && hasAny(w, {"jam", "hour"}))) {
            amount = 30;
            in.number = amount * 60;
        } else if (amount >= 0 && unitAt < w.size()) {
            const QString u = w.at(unitAt);
            const int mult = (u == QLatin1String("detik") || u.startsWith(QLatin1String("sec"))) ? 1
                           : (u == QLatin1String("menit") || u.startsWith(QLatin1String("min"))) ? 60
                           : (u == QLatin1String("jam") || u.startsWith(QLatin1String("hour")) || u == QLatin1String("hr")) ? 3600 : 0;
            if (mult > 0)
                in.number = amount * mult;
        }
        if (in.number > 0) {
            in.kind = Kind::Timer;
            const int lab = std::max(w.indexOf(QStringLiteral("untuk")), w.indexOf(QStringLiteral("for")));
            if (lab >= 0 && lab + 1 < w.size() && findNumber(w, lab + 1, &at, &used) < 0)
                in.text = rest(lab + 1);
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

    // a search: "cari resep nasi goreng", "search for cats on youtube", "google weather"
    if (first == QLatin1String("cari") || first == QLatin1String("carikan") || first == QLatin1String("search") || first == QLatin1String("google")
        || first == QLatin1String("telusuri")) {
        QStringList q = w.mid(1);
        while (!q.isEmpty() && (q.first() == QLatin1String("for") || q.first() == QLatin1String("tentang") || q.first() == QLatin1String("untuk")
                                || q.first() == QLatin1String("about")))
            q.removeFirst();
        static const QStringList engines{QStringLiteral("google"), QStringLiteral("youtube"), QStringLiteral("bing"), QStringLiteral("duckduckgo"),
                                         QStringLiteral("wikipedia")};
        if (q.size() >= 2 && (q.at(q.size() - 2) == QLatin1String("di") || q.at(q.size() - 2) == QLatin1String("in")
                              || q.at(q.size() - 2) == QLatin1String("on") || q.at(q.size() - 2) == QLatin1String("pada"))
            && engines.contains(q.last())) {
            in.engine = q.last();
            q = q.mid(0, q.size() - 2);
        } else if (q.size() >= 3 && (q.at(q.size() - 3) == QLatin1String("di") || q.at(q.size() - 3) == QLatin1String("in") || q.at(q.size() - 3) == QLatin1String("on"))
                   && (q.last() == QLatin1String("internet") || q.last() == QLatin1String("web"))) {
            q = q.mid(0, q.size() - 3);
        }
        if (first == QLatin1String("google") && in.engine.isEmpty())
            in.engine = QStringLiteral("google");
        if (!q.isEmpty()) {
            in.kind = Kind::Search;
            in.text = q.join(u' ');
            return in;
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

    // open: "buka firefox", "open the browser", "jalankan terminal"
    static const QStringList openVerbs{QStringLiteral("buka"), QStringLiteral("bukakan"), QStringLiteral("open"), QStringLiteral("launch"),
                                       QStringLiteral("jalankan"), QStringLiteral("start"), QStringLiteral("run"), QStringLiteral("nyalakan")};
    if (openVerbs.contains(first) && w.size() > 1) {
        const QString what = dropWords(rest(), {"aplikasi", "app", "application", "program", "the", "ke"});
        if (what == QLatin1String("browser") || what == QLatin1String("peramban") || what == QLatin1String("internet") || what == QLatin1String("web browser")
            || what == QLatin1String("web") || what == QLatin1String("browsers")) {
            in.kind = Kind::OpenBrowser;
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

struct AppMatch { int index = -1; double score = 0; bool sure = false; };

// Compares what was said with each app's name: 1 for the same name, high when one holds the other or a word of it
// matches, else how close the spelling is ("fairfox" ~ "firefox"). `sure` when the best is good and clearly ahead.
inline AppMatch matchApp(const QString &said, const QStringList &names)
{
    const QString q = normalize(said).remove(u' ');
    AppMatch best;
    double second = 0;
    if (q.isEmpty())
        return best;
    for (int i = 0; i < names.size(); ++i) {
        const QString full = names.at(i).toLower();
        const QString name = full.simplified().remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N} ]"))).remove(u' ');
        if (name.isEmpty())
            continue;
        double s;
        if (name == q)
            s = 1.0;
        else if (name.startsWith(q) || q.startsWith(name))
            s = 0.92;
        else if (name.contains(q) && q.size() >= 3)
            s = 0.85;
        else {
            s = 1.0 - double(editDistance(q, name)) / double(std::max(q.size(), name.size()));
            for (const QString &word : full.split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), Qt::SkipEmptyParts))
                if (word.size() >= 3)
                    s = std::max(s, 0.95 * (1.0 - double(editDistance(q, word)) / double(std::max<qsizetype>(q.size(), word.size()))));
        }
        if (s > best.score) {
            second = best.score;
            best = {i, s, false};
        } else if (s > second) {
            second = s;
        }
    }
    best.sure = best.index >= 0 && best.score >= 0.7 && best.score - second >= 0.08;
    return best;
}

// ---- the web ----
inline QUrl searchUrl(const QString &engine, const QString &query)
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
    url.setQuery(q);
    return url;
}

}
