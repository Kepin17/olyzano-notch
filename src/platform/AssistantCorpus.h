#pragma once

#include "platform/AssistantModel.h"

#include <QList>
#include <QString>
#include <QStringList>

// Sentences with what each one must become, and a way to garble them the way a speech recognizer does, so the
// understanding can be measured (tests and `notch --assistant-eval`) instead of guessed at. Pure, no device.
namespace AssistantCorpus {

using namespace AssistantModel;

// What a sentence must become. Fields left empty / -1 are not checked.
struct Case {
    QString said;
    Kind kind = Kind::Unknown;
    QString text;
    QString engine, filter;
    int number = -1;
    int action = -1; // VolumeAction or MediaAction, as int
};

inline Case C(const char *said, Kind kind, const char *text = "", const char *engine = "", const char *filter = "", int number = -1, int action = -1)
{
    return {QString::fromUtf8(said), kind, QString::fromUtf8(text), QString::fromUtf8(engine), QString::fromUtf8(filter), number, action};
}

inline const QList<Case> &cases()
{
    using K = Kind;
    constexpr int VSet = int(VolumeAction::Set), VUp = int(VolumeAction::Up), VDown = int(VolumeAction::Down), VMute = int(VolumeAction::Mute),
                  VUnmute = int(VolumeAction::Unmute);
    constexpr int MPlay = int(MediaAction::Play), MPause = int(MediaAction::Pause), MNext = int(MediaAction::Next), MPrev = int(MediaAction::Previous);
    static const QList<Case> all{
        // open an app
        C("buka terminal", K::OpenApp, "terminal"), C("open terminal", K::OpenApp, "terminal"), C("buka kalkulator", K::OpenApp, "kalkulator"),
        C("open calculator", K::OpenApp, "calculator"), C("jalankan visual studio code", K::OpenApp, "visual studio code"),
        C("buka file manager", K::OpenApp, "file manager"), C("open settings", K::OpenApp, "settings"), C("launch spotify", K::OpenApp, "spotify"),
        C("buka discord", K::OpenApp, "discord"), C("tolong buka libreoffice writer", K::OpenApp, "libreoffice writer"), C("open vlc", K::OpenApp, "vlc"),
        C("buka firefox", K::OpenApp, "firefox"), C("jalankan aplikasi konsole", K::OpenApp, "konsole"), C("bukakan steam", K::OpenApp, "steam"),
        C("hei notch buka pengaturan", K::OpenApp, "pengaturan"), C("start gimp", K::OpenApp, "gimp"), C("run blender", K::OpenApp, "blender"),
        // the browser
        C("buka browser", K::OpenBrowser), C("open the browser", K::OpenBrowser), C("buka peramban", K::OpenBrowser), C("buka web browser", K::OpenBrowser),
        C("buka brouser", K::OpenBrowser), C("open web browser", K::OpenBrowser), C("tolong buka browser dong", K::OpenBrowser), C("jalankan browser", K::OpenBrowser),
        // sites
        C("buka youtube", K::OpenSite, "https://www.youtube.com/"), C("open youtube", K::OpenSite, "https://www.youtube.com/"),
        C("buka gmail", K::OpenSite, "https://mail.google.com/"), C("open github", K::OpenSite, "https://github.com/"),
        C("buka google maps", K::OpenSite, "https://maps.google.com/"), C("buka situs tokopedia", K::OpenSite, "https://www.tokopedia.com/"),
        C("open github.com", K::OpenSite, "https://github.com"), C("buka facebook", K::OpenSite, "https://www.facebook.com/"),
        C("open netflix", K::OpenSite, "https://www.netflix.com/"), C("buka instagram", K::OpenSite, "https://www.instagram.com/"),
        C("cari youtube", K::OpenSite, "https://www.youtube.com/"), C("buka shopee", K::OpenSite, "https://shopee.co.id/"),
        C("open reddit", K::OpenSite, "https://www.reddit.com/"), C("buka chatgpt", K::OpenSite, "https://chatgpt.com/"),
        C("tolong buka youtube ya", K::OpenSite, "https://www.youtube.com/"),
        // search
        C("cari resep nasi goreng", K::Search, "resep nasi goreng"), C("cari harga laptop", K::Search, "harga laptop"), C("carikan tutorial python", K::Search, "tutorial python"),
        C("info vga yang murah", K::Search, "info vga yang murah"), C("cari info vga yang murah", K::Search, "info vga yang murah"),
        C("harga laptop gaming", K::Search, "harga laptop gaming"), C("apa itu blockchain", K::Search, "apa itu blockchain"),
        C("bagaimana cara membuat kopi", K::Search, "bagaimana cara membuat kopi"), C("siapa presiden indonesia", K::Search, "siapa presiden indonesia"),
        C("berapa harga iphone", K::Search, "berapa harga iphone"), C("cari video kucing lucu", K::Search, "kucing lucu", "youtube", "video"),
        C("cari channel nadia di youtube", K::Search, "nadia", "youtube", "channel"), C("buka youtube dan cari musik santai", K::Search, "musik santai", "youtube"),
        C("di youtube cari tutorial qt", K::Search, "tutorial qt", "youtube"), C("cari di google cuaca jakarta", K::Search, "cuaca jakarta", "google"),
        C("search for cute cats on youtube", K::Search, "cute cats", "youtube"), C("google weather today", K::Search, "weather today", "google"),
        C("search for channel fireship", K::Search, "fireship", "youtube", "channel"), C("what is a vpn", K::Search, "what is a vpn"),
        C("how to tie a tie", K::Search, "how to tie a tie"), C("who is the president of france", K::Search, "who is the president of france"),
        C("price of rtx 4060", K::Search, "price of rtx 4060"), C("search the best budget graphics card", K::Search, "the best budget graphics card"),
        C("find tutorial rust on youtube", K::Search, "tutorial rust", "youtube"), C("tolong cari harga laptop gaming yang bagus untuk kuliah dong", K::Search, "harga laptop gaming yang bagus untuk kuliah"),
        C("cari video tutorial blender", K::Search, "tutorial blender", "youtube", "video"), C("cari channel fireship", K::Search, "fireship", "youtube", "channel"),
        C("telusuri berita hari ini", K::Search, "berita hari ini"), C("cari lagu dewa 19", K::Search, "lagu dewa 19"),
        C("review iphone 15", K::Search, "review iphone 15"), C("rekomendasi laptop murah", K::Search, "rekomendasi laptop murah"),
        // volume
        C("volume 50", K::Volume, "", "", "", 50, VSet), C("set volume to thirty", K::Volume, "", "", "", 30, VSet), C("volume lima puluh", K::Volume, "", "", "", 50, VSet),
        C("naikkan volume", K::Volume, "", "", "", 10, VUp), C("naikkan volume dua puluh", K::Volume, "", "", "", 20, VUp), C("turunkan volume", K::Volume, "", "", "", 10, VDown),
        C("turunkan volume ke 30", K::Volume, "", "", "", 30, VSet), C("volume up", K::Volume, "", "", "", 10, VUp), C("volume down", K::Volume, "", "", "", 10, VDown),
        C("mute", K::Volume, "", "", "", -1, VMute), C("bisukan suara", K::Volume, "", "", "", -1, VMute), C("unmute", K::Volume, "", "", "", -1, VUnmute),
        C("hidupkan suara", K::Volume, "", "", "", -1, VUnmute), C("matikan suara", K::Volume, "", "", "", -1, VMute),
        C("turn the volume up", K::Volume, "", "", "", 10, VUp), C("lower the volume", K::Volume, "", "", "", 10, VDown), C("besarkan volume", K::Volume, "", "", "", 10, VUp),
        C("kecilkan volume", K::Volume, "", "", "", 10, VDown), C("volume penuh", K::Volume, "", "", "", 100, VSet), C("volume setengah", K::Volume, "", "", "", 50, VSet),
        C("volume seratus", K::Volume, "", "", "", 100, VSet), C("pelankan suara", K::Volume, "", "", "", 10, VDown),
        // music
        C("putar musik", K::Media, "", "", "", -1, MPlay), C("play music", K::Media, "", "", "", -1, MPlay), C("jeda musik", K::Media, "", "", "", -1, MPause),
        C("pause", K::Media, "", "", "", -1, MPause), C("jeda", K::Media, "", "", "", -1, MPause), C("lagu berikutnya", K::Media, "", "", "", -1, MNext),
        C("next song", K::Media, "", "", "", -1, MNext), C("skip", K::Media, "", "", "", -1, MNext), C("lagu sebelumnya", K::Media, "", "", "", -1, MPrev),
        C("previous song", K::Media, "", "", "", -1, MPrev), C("lanjutkan musik", K::Media, "", "", "", -1, MPlay), C("stop music", K::Media, "", "", "", -1, MPause),
        C("putar lagu selanjutnya", K::Media, "", "", "", -1, MNext), C("ganti lagu", K::Media, "", "", "", -1, MNext), C("pause the music", K::Media, "", "", "", -1, MPause),
        C("mainkan musik", K::Media, "", "", "", -1, MPlay),
        // timer
        C("timer 10 menit", K::Timer, "", "", "", 600), C("set a timer for five minutes", K::Timer, "", "", "", 300), C("atur timer lima belas menit", K::Timer, "", "", "", 900),
        C("timer 30 detik", K::Timer, "", "", "", 30), C("timer setengah jam", K::Timer, "", "", "", 1800), C("timer dua jam", K::Timer, "", "", "", 7200),
        C("timer satu setengah jam", K::Timer, "", "", "", 5400), C("atur timer sepuluh menit untuk teh", K::Timer, "teh", "", "", 600),
        C("start a timer for 25 minutes", K::Timer, "", "", "", 1500), C("timer seperempat jam", K::Timer, "", "", "", 900), C("pengatur waktu 5 menit", K::Timer, "", "", "", 300),
        C("set timer for one hour", K::Timer, "", "", "", 3600), C("timer dua puluh lima menit", K::Timer, "", "", "", 1500),
        // tasks and notes
        C("tambah tugas beli susu", K::Task, "beli susu"), C("tambah tugas beli susu besok jam 5", K::Task, "beli susu tomorrow 5:00"), C("add task call mom", K::Task, "call mom"),
        C("buat tugas kirim laporan hari ini jam 3 sore", K::Task, "kirim laporan today 15:00"), C("ingatkan saya bayar listrik besok", K::Task, "bayar listrik tomorrow"),
        C("tambahkan pengingat minum obat jam 8 malam", K::Task, "minum obat 20:00"), C("add task buy milk tomorrow 17:00", K::Task, "buy milk tomorrow 17:00"),
        C("catat beli susu", K::Note, "beli susu"), C("catat ide untuk presentasi", K::Note, "ide untuk presentasi"), C("note call mom", K::Note, "call mom"),
        C("catatkan nomor rumah 12", K::Note, "nomor rumah 12"),
        // not commands: nothing may happen
        C("matikan komputer", K::Unknown), C("hapus semua berkas", K::Unknown), C("tutup semua jendela", K::Unknown), C("ceritakan lelucon", K::Unknown),
        C("hari ini cerah sekali", K::Unknown), C("halo apa kabar", K::Unknown), C("terima kasih", K::Unknown), C("shut down the computer", K::Unknown),
        C("delete everything", K::Unknown), C("restart komputer sekarang", K::Unknown), C("good morning everyone", K::Unknown),
    };
    return all;
}

// A deterministic garbler: the mistakes a recognizer makes with a word it half heard (letters swapped or dropped, c for k, v
// for f, a vowel lost, the end of a word cut). Numbers and short words are left alone. `percent` of the long words change.
inline QString garble(const QString &said, quint32 &seed, int percent)
{
    const auto next = [&seed] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    QStringList out;
    for (QString w : said.split(u' ', Qt::SkipEmptyParts)) {
        bool digits = false;
        w.toInt(&digits);
        if (w.size() >= 4 && !digits && !w.contains(u'.') && int(next() % 100) < percent) {
            const int pos = 1 + int(next() % uint(w.size() - 1)); // never the first letter
            switch (next() % 6) {
            case 0: if (pos + 1 < w.size()) std::swap(w[pos], w[pos + 1]); break;                    // abdc
            case 1: w.remove(pos, 1); break;                                                          // a letter lost
            case 2: w.replace(QStringLiteral("c"), QStringLiteral("k")); w.replace(QStringLiteral("v"), QStringLiteral("f")); break;
            case 3: w.insert(pos, w.at(pos)); break;                                                  // a letter doubled
            case 4: w.chop(1); break;                                                                 // the end cut ("youtub")
            default: { const int v = w.indexOf(QRegularExpression(QStringLiteral("[aeiou]")), 1); if (v > 0) w[v] = u'i'; break; } // a vowel blurred
            }
        }
        out << w;
    }
    return out.join(u' ');
}

// Does the understood Intent match what the case wants?
// `strictText`: the words of an app name or a search must be the same too (not asked of garbled sentences: their words are garbled).
inline bool matches(const Intent &in, const Case &c, bool strictText = true)
{
    if (in.kind != c.kind)
        return false;
    if (strictText && !c.text.isEmpty() && in.text != c.text)
        return false;
    if (!c.engine.isEmpty() && in.engine != c.engine)
        return false;
    if (!c.filter.isEmpty() && in.filter != c.filter)
        return false;
    if (c.number >= 0 && in.number != c.number)
        return false;
    if (c.action >= 0 && c.kind == Kind::Volume && int(in.volume) != c.action)
        return false;
    if (c.action >= 0 && c.kind == Kind::Media && int(in.media) != c.action)
        return false;
    return true;
}

struct Score {
    int total = 0, right = 0;
    QStringList wrong; // "said -> got" lines, for the eval printout
};

// `percent` of long words garbled (0: the sentences as written); each sentence is tried `rounds` times with different garbling.
inline Score evaluate(int percent, int rounds = 1, quint32 seed = 12345)
{
    Score s;
    for (const Case &c : cases()) {
        for (int r = 0; r < (percent == 0 ? 1 : rounds); ++r) {
            const QString said = percent == 0 ? c.said : garble(c.said, seed, percent);
            const Intent in = parse(said);
            ++s.total;
            if (matches(in, c, percent == 0))
                ++s.right;
            else
                s.wrong << QStringLiteral("%1  ->  kind %2 \"%3\"  (wanted kind %4 \"%5\")").arg(said).arg(int(in.kind)).arg(in.text).arg(int(c.kind)).arg(c.text);
        }
    }
    return s;
}

}
