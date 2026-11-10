#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <functional>

// What the models are for: each feature of Notch that needs a download (speech to text, telling speakers apart, notes, reading pictures, …) as
// the set of catalog items it needs, grouped in categories, so the Settings page can show them in one place and install a whole category or
// everything at once. Programs several features need (llama-server, sherpa-onnx, ONNX Runtime) are in each of them and downloaded once. The
// files themselves are in ModelCatalog.h. Pure.
namespace Audio {
namespace Bundles {

struct Category {
    QString id, title, note;
};

struct Feature {
    QString id, category, title, note;
    QStringList ids;    // catalog ids, for this system
    bool extra = false; // better quality at a higher price: left out of "recommended"
};

// the catalog id of a program that has one build per system ("llama-server" -> "llama-server-linux" | "-windows")
inline QString platformId(const QString &base)
{
#ifdef Q_OS_WIN
    return base + QStringLiteral("-windows");
#else
    return base + QStringLiteral("-linux");
#endif
}

inline QList<Category> categories()
{
    return {{QStringLiteral("audio"), QStringLiteral("Audio & recordings"), QStringLiteral("Turning recordings into text, speakers and notes.")},
            {QStringLiteral("documents"), QStringLiteral("Documents & files"), QStringLiteral("Finding and asking about your files.")},
            {QStringLiteral("pictures"), QStringLiteral("Pictures"), QStringLiteral("Reading and changing pictures.")}};
}

inline QList<Feature> features()
{
    const QString llama = platformId(QStringLiteral("llama-server")), sherpa = platformId(QStringLiteral("sherpa")), ort = platformId(QStringLiteral("onnxruntime"));
    return {
        {QStringLiteral("speech"), QStringLiteral("audio"), QStringLiteral("Speech to text"),
         QStringLiteral("Turns a recording into a transcript (Whisper Small). Needed for everything in Audio."), {QStringLiteral("whisper-small")}, false},
        {QStringLiteral("speech-hq"), QStringLiteral("audio"), QStringLiteral("High-quality speech"),
         QStringLiteral("Whisper Large v3 Turbo: more accurate, slower, more memory. Optional; choose it under Quality."), {QStringLiteral("whisper-turbo")}, true},
        {QStringLiteral("speakers"), QStringLiteral("audio"), QStringLiteral("Tell speakers apart"),
         QStringLiteral("Says who spoke when in a recording."), {sherpa, QStringLiteral("diar-segmentation"), QStringLiteral("diar-embedding")}, false},
        {QStringLiteral("notes"), QStringLiteral("audio"), QStringLiteral("Notes, decisions and tasks"),
         QStringLiteral("The language model (Qwen3 4B) that writes the notes of a recording and answers in Ask. About 3.5 GB of memory while it runs."),
         {QStringLiteral("qwen3-4b"), llama}, false},
        {QStringLiteral("sounds"), QStringLiteral("audio"), QStringLiteral("Music and sounds"),
         QStringLiteral("Tells music, applause and crowds from speech, so a transcript marks them instead of making words up."), {QStringLiteral("sound-ced-tiny"), sherpa}, false},
        {QStringLiteral("clean"), QStringLiteral("audio"), QStringLiteral("Reduce noise"),
         QStringLiteral("Takes hum and background noise out of a recording before it is turned into text (GTCRN, tiny). Choose it under Clean up sound."), {QStringLiteral("clean-gtcrn"), sherpa}, false},
        {QStringLiteral("clean-vocals"), QStringLiteral("audio"), QStringLiteral("Voice from music"),
         QStringLiteral("Separates the singing from the music (Spleeter), so a song is written down without words made up for the band. Slow. Optional."), {QStringLiteral("clean-spleeter"), sherpa}, true},
        {QStringLiteral("punct"), QStringLiteral("audio"), QStringLiteral("Punctuation (English)"),
         QStringLiteral("Adds full stops, commas and capitals to English text that has none. English only: Whisper already punctuates Indonesian and English. Optional."), {QStringLiteral("punct-en"), sherpa}, true},
        {QStringLiteral("search"), QStringLiteral("documents"), QStringLiteral("Find by meaning"),
         QStringLiteral("Search follows what a sentence means, also across languages. Asking questions about files uses the notes model above."), {QStringLiteral("embed-qwen3")}, false},
        {QStringLiteral("vision"), QStringLiteral("pictures"), QStringLiteral("Read pictures"),
         QStringLiteral("Says what is in a picture and reads writing and tables (Qwen3-VL). Right-click a picture → Tools → Image (AI). About 4.5 GB of memory while it runs."),
         {QStringLiteral("vision-qwen3vl"), QStringLiteral("vision-mmproj"), llama}, false},
        {QStringLiteral("cutout"), QStringLiteral("pictures"), QStringLiteral("Remove backgrounds"),
         QStringLiteral("Cuts the subject out of a picture (U2-Netp, fast). Right-click a picture → Tools → Remove background, or in Convert."), {QStringLiteral("matting-u2netp"), ort}, false},
        {QStringLiteral("cutout-hq"), QStringLiteral("pictures"), QStringLiteral("Best-quality cut-out"),
         QStringLiteral("IS-Net: finer edges (hair, fur), slower, about 3 GB of memory. Optional."), {QStringLiteral("matting-isnet"), ort}, true}};
}

inline Feature feature(const QString &id)
{
    for (const Feature &f : features())
        if (f.id == id)
            return f;
    return {};
}

inline QStringList featureIds(const QString &category = {}, bool withExtras = true)
{
    QStringList out;
    for (const Feature &f : features())
        if ((category.isEmpty() || f.category == category) && (withExtras || !f.extra))
            out << f.id;
    return out;
}
inline QStringList recommended() { return featureIds({}, false); }
inline QStringList everything() { return featureIds({}, true); }

// the catalog ids of some features, each once, in the order they are met
inline QStringList idsOf(const QStringList &featureList)
{
    QStringList out;
    for (const QString &fid : featureList)
        for (const QString &id : feature(fid).ids)
            if (!out.contains(id))
                out << id;
    return out;
}

inline QStringList missing(const QStringList &ids, const std::function<bool(const QString &)> &ready)
{
    QStringList out;
    for (const QString &id : ids)
        if (!ready(id))
            out << id;
    return out;
}

inline qint64 totalSize(const QStringList &ids, const std::function<qint64(const QString &)> &sizeOf)
{
    qint64 sum = 0;
    for (const QString &id : ids)
        sum += sizeOf(id);
    return sum;
}

// the order to download in: the small ones first (the programs), so a feature starts to work as soon as its big file lands
inline QStringList plan(QStringList ids, const std::function<qint64(const QString &)> &sizeOf)
{
    std::stable_sort(ids.begin(), ids.end(), [&](const QString &a, const QString &b) { return sizeOf(a) < sizeOf(b); });
    return ids;
}

// what removing a feature may delete: its items that no other installed feature needs
inline QStringList removable(const QString &featureId, const QStringList &installedFeatures)
{
    QSet<QString> others;
    for (const QString &fid : installedFeatures)
        if (fid != featureId)
            for (const QString &id : feature(fid).ids)
                others.insert(id);
    QStringList out;
    for (const QString &id : feature(featureId).ids)
        if (!others.contains(id))
            out << id;
    return out;
}

} // namespace Bundles
} // namespace Audio
