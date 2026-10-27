#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <algorithm>

// Words heard a little off, put right: how alike two words sound and look, and which known command word a garbled one was
// meant to be ("buku" / "bika" / "buk" -> "buka", "youtub" -> "youtube"). Pure and deterministic, no device and no clock.
namespace AssistantText {

// Edit distance where swapping two neighbouring letters counts once (Damerau-Levenshtein, restricted).
inline int distance(const QString &a, const QString &b)
{
    const int n = int(a.size()), m = int(b.size());
    QList<QList<int>> d(n + 1, QList<int>(m + 1, 0));
    for (int i = 0; i <= n; ++i)
        d[i][0] = i;
    for (int j = 0; j <= m; ++j)
        d[0][j] = j;
    for (int i = 1; i <= n; ++i)
        for (int j = 1; j <= m; ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a.at(i - 1) == b.at(j - 2) && a.at(i - 2) == b.at(j - 1))
                d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    return d[n][m];
}

// Jaro-Winkler similarity, 0..1 (1 = the same): rewards letters in the same order and a shared beginning.
inline double jaroWinkler(const QString &a, const QString &b)
{
    if (a == b)
        return 1.0;
    const int n = int(a.size()), m = int(b.size());
    if (n == 0 || m == 0)
        return 0.0;
    const int range = std::max(0, std::max(n, m) / 2 - 1);
    QList<bool> am(n, false), bm(m, false);
    int matches = 0;
    for (int i = 0; i < n; ++i) {
        const int lo = std::max(0, i - range), hi = std::min(m - 1, i + range);
        for (int j = lo; j <= hi; ++j)
            if (!bm[j] && a.at(i) == b.at(j)) {
                am[i] = bm[j] = true;
                ++matches;
                break;
            }
    }
    if (matches == 0)
        return 0.0;
    int transpositions = 0, k = 0;
    for (int i = 0; i < n; ++i) {
        if (!am[i])
            continue;
        while (!bm[k])
            ++k;
        if (a.at(i) != b.at(k))
            ++transpositions;
        ++k;
    }
    const double mm = matches;
    const double jaro = (mm / n + mm / m + (mm - transpositions / 2.0) / mm) / 3.0;
    int prefix = 0;
    while (prefix < std::min({4, n, m}) && a.at(prefix) == b.at(prefix))
        ++prefix;
    return jaro + prefix * 0.1 * (1.0 - jaro);
}

// How a word sounds, roughly, for Indonesian and English together: c/q -> k, v/ph -> f, th -> t, y -> i, double letters
// become one, and after the first letter the vowels go ("buka", "bika", "buk" all become "bk"; "youtube" and "yutub" "itb").
inline QString skeleton(const QString &word)
{
    QString s = word.toLower();
    s.replace(QStringLiteral("ph"), QStringLiteral("f"));
    s.replace(QStringLiteral("th"), QStringLiteral("t"));
    s.replace(QStringLiteral("ck"), QStringLiteral("k"));
    s.replace(u'c', u'k').replace(u'q', u'k').replace(u'v', u'f').replace(u'y', u'i').replace(u'z', u's').replace(u'x', u'k');
    QString out;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (!out.isEmpty() && out.back() == c)
            continue;
        if (i > 0 && QStringLiteral("aeiou").contains(c))
            continue;
        out += c;
    }
    return out;
}

// 0..1: how likely two words are the same word heard differently.
inline double closeness(const QString &heard, const QString &known)
{
    if (heard == known)
        return 1.0;
    double s = jaroWinkler(heard, known);
    const int longest = int(std::max(heard.size(), known.size()));
    s = std::max(s, 1.0 - double(distance(heard, known)) / longest);
    if (skeleton(heard) == skeleton(known) && std::abs(int(heard.size() - known.size())) <= 2 && heard.at(0) == known.at(0))
        s = std::max(s, 0.9);
    return s;
}

// The word of `lexicon` that `heard` is most likely, with how sure; "" when nothing is close enough. A word the lexicon already
// has is itself. Short words may differ by one letter only (and must start the same), long words by what sounds alike.
inline QString nearest(const QString &heard, const QStringList &lexicon, double *score, double minimum = 0.86)
{
    *score = 0;
    if (heard.size() < 3)
        return {};
    if (lexicon.contains(heard)) {
        *score = 1;
        return heard;
    }
    QString best;
    double top = 0, second = 0;
    for (const QString &k : lexicon) {
        if (std::abs(int(k.size() - heard.size())) > 3)
            continue;
        if (distance(heard, k) > (heard.size() <= 6 ? 1 : 2) && skeleton(heard) != skeleton(k)) // too different to be a slip
            continue;
        double s = closeness(heard, k);
        if (heard.at(0) != k.at(0) && skeleton(heard) != skeleton(k)) // a recognizer rarely gets the first letter wrong
            continue;
        if (heard.size() <= 4 || k.size() <= 4) // short words: one letter off and the same start, no more
            s = (distance(heard, k) <= 1 && (heard.at(0) == k.at(0) || skeleton(heard) == skeleton(k))) ? std::max(s, 0.88) : std::min(s, 0.7);
        if (s > top) {
            second = top;
            top = s;
            best = k;
        } else if (s > second) {
            second = s;
        }
    }
    Q_UNUSED(second)
    if (top < minimum) // not close enough: no guess (two words as close: the earlier one in the list wins)
        return {};
    *score = top;
    return best;
}

// Every word of the lexicon that `heard` may have been, the likeliest first ("bua" is "buka" or "buat"); the sentence decides.
inline QStringList ranked(const QString &heard, const QStringList &lexicon, double minimum = 0.84)
{
    QList<QPair<double, QString>> hits;
    if (heard.size() < 3)
        return {};
    for (const QString &k : lexicon) {
        if (std::abs(int(k.size() - heard.size())) > 3)
            continue;
        if (distance(heard, k) > (heard.size() <= 6 ? 1 : 2) && skeleton(heard) != skeleton(k))
            continue;
        if (heard.at(0) != k.at(0) && skeleton(heard) != skeleton(k))
            continue;
        double s = closeness(heard, k);
        if (heard.size() <= 4 || k.size() <= 4)
            s = (distance(heard, k) <= 1 && (heard.at(0) == k.at(0) || skeleton(heard) == skeleton(k))) ? std::max(s, 0.88) : std::min(s, 0.7);
        if (s >= minimum)
            hits << qMakePair(s, k);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList out;
    for (const auto &h : hits)
        out << h.second;
    return out;
}

// The verbs that start a command, and the words that mark its parts: the only words a sentence is repaired towards.
inline const QStringList &verbs()
{
    static const char *const v[] = {
        "buka", "bukakan", "open", "launch", "jalankan", "start", "run", "nyalakan", "cari", "carikan", "search", "telusuri", "find", "putar", "mainkan",
        "play", "jeda", "pause", "lanjut", "lanjutkan", "resume", "tambah", "tambahkan", "buat", "buatkan", "add", "create", "catat", "catatkan", "note",
        "tulis", "ingatkan", "remind", "naikkan", "turunkan", "besarkan", "kecilkan", "pelankan", "volume", "timer", "mute", "unmute", "bisukan", "next",
        "skip", "previous", "stop", "info", "informasi", "harga", "review", "tutorial", "bagaimana", "rekomendasi", "google", "atur", "set", "hidupkan",
        "matikan", "lewati", "ganti", "pengatur", "tolong", "please", "hei", "notch", "dong", "siapa", "berapa", "kapan", "dimana", "mengapa", "kenapa", "what",
        "who", "when", "where", "why", "how", "price", "cara", "tentang", "news", "berita", "lower", "raise", "increase", "decrease", "turn"};
    static QStringList list;
    if (list.isEmpty())
        for (const char *w : v)
            list << QString::fromLatin1(w);
    return list;
}

inline const QStringList &markers()
{
    static const char *const m[] = {
        "video", "videos", "channel", "saluran", "kanal", "youtube", "google", "bing", "duckduckgo", "wikipedia", "menit", "detik", "jam", "persen",
        "tugas", "musik", "music", "lagu", "song", "browser", "peramban", "volume", "suara", "setengah", "seperempat", "berikutnya", "sebelumnya",
        "selanjutnya", "pengingat", "reminder", "minutes", "seconds", "hours", "naikkan", "turunkan", "down", "mute", "next", "stop", "pause",
        "lower", "louder", "quieter", "increase", "decrease", "raise", "pelankan", "kecilkan", "besarkan", "penuh", "maksimal", "puluh", "belas", "ratus",
        "seratus", "sepuluh", "sebelas", "delapan", "sembilan", "tujuh", "lima", "enam", "tiga", "four", "five", "seven", "eight", "nine", "ten", "eleven",
        "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty", "thirty", "forty", "fifty", "sixty",
        "seventy", "eighty", "ninety", "hundred", "half", "quarter", "hour", "minute", "second", "detik", "timer", "pengatur", "waktu", "alarm"};
    static QStringList list;
    if (list.isEmpty())
        for (const char *w : m)
            list << QString::fromLatin1(w);
    return list;
}

// A command of one word ("pause", "jeda"): the only single words that are put right.
inline const QStringList &bareCommands()
{
    static const char *const b[] = {"mute", "unmute", "pause", "jeda", "skip", "next", "previous", "stop", "lanjut", "resume", "bisukan"};
    static QStringList list;
    if (list.isEmpty())
        for (const char *w : b)
            list << QString::fromLatin1(w);
    return list;
}

// Words short enough to be common ones (down, time, menit) that are still put right when they stand where a command word does.
inline const QStringList &keywords()
{
    static const char *const k[] = {"timer", "menit", "detik", "down", "mute", "next", "stop", "pause", "suara", "lagu", "song", "tugas", "belas", "puluh", "video",
                                    "hour", "task", "note", "satu", "channel"};
    static QStringList list;
    if (list.isEmpty())
        for (const char *w : k)
            list << QString::fromLatin1(w);
    return list;
}

struct Repair {
    QString text;       // the sentence with the garbled command words put right
    double score = 1.0; // how sure: the lowest closeness among the words that were changed (1 = nothing changed)
    bool changed = false;
};

// Puts right the garbled command words of a sentence (already lower case, single spaces): the first word towards the verbs
// (only when there are two or more words, so a lone "buku" stays a word), and later long words towards the markers
// (video, channel, youtube, menit, ...). The words of a search or a name are not touched: "cari buku" stays "cari buku".
inline Repair repair(const QString &sentence, bool verbsToo = true, const QStringList &extraVerbs = {})
{
    Repair r;
    QStringList words = sentence.split(u' ', Qt::SkipEmptyParts);
    if (words.size() == 1) { // "paus" -> "pause"
        double one = 0;
        const QString b = nearest(words.first(), bareCommands(), &one, 0.86);
        r.text = b.isEmpty() ? sentence : b;
        r.changed = !b.isEmpty() && b != sentence;
        r.score = b.isEmpty() ? 1.0 : one;
        return r;
    }
    if (words.size() < 2) {
        r.text = sentence;
        return r;
    }
    QStringList verbList = verbs();
    verbList << extraVerbs;
    double sc = 0;
    // the verb may follow a polite word or two ("tolong bka terminal"): try the first three words
    for (int i = 0; verbsToo && i < std::min<int>(3, int(words.size()) - 1); ++i) {
        const QString v = nearest(words.at(i), verbList, &sc, 0.84);
        if (!v.isEmpty() && v != words.at(i)) {
            words[i] = v;
            r.score = std::min(r.score, sc);
            r.changed = true;
        }
    }
    static const QStringList politeEnd{QStringLiteral("dong"), QStringLiteral("deh"), QStringLiteral("please"), QStringLiteral("saja"), QStringLiteral("aja")};
    for (int i = 1; i < words.size(); ++i) {
        if (i == words.size() - 1 && words.size() >= 3 && !nearest(words.at(i), politeEnd, &sc, 0.84).isEmpty())
            continue; // a garbled "dong" at the end
        const bool shortWord = words.at(i).size() < 5;
        if (shortWord && words.at(i).size() < 3)
            continue;
        // a long word may be any marker; a short one only the key words (and "for" must not become "four")
        const QString m = nearest(words.at(i), shortWord ? keywords() : markers(), &sc, 0.88);
        if (!m.isEmpty() && m != words.at(i)) {
            words[i] = m;
            r.score = std::min(r.score, sc);
            r.changed = true;
        }
    }
    // "buka youtube dan carri ...": the verb after a conjunction
    for (int i = 2; verbsToo && i < words.size() - 1; ++i) {
        if (words.at(i - 1) != QLatin1String("dan") && words.at(i - 1) != QLatin1String("and") && words.at(i - 1) != QLatin1String("lalu")
            && words.at(i - 1) != QLatin1String("terus") && words.at(i - 1) != QLatin1String("then"))
            continue;
        const QString v = nearest(words.at(i), verbList, &sc, 0.84);
        if (!v.isEmpty() && v != words.at(i)) {
            words[i] = v;
            r.score = std::min(r.score, sc);
            r.changed = true;
        }
    }
    // a garbled "dong" / "please" at the end
    static const QStringList polite{QStringLiteral("dong"), QStringLiteral("deh"), QStringLiteral("please"), QStringLiteral("saja"), QStringLiteral("aja")};
    if (words.size() >= 3) {
        const QString v = nearest(words.last(), polite, &sc, 0.84);
        if (!v.isEmpty() && v != words.last()) {
            words.last() = v;
            r.score = std::min(r.score, sc);
            r.changed = true;
        }
    }
    r.text = words.join(u' ');
    return r;
}

}
