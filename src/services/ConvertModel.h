#pragma once

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

// What a file can be converted to, for the Tools menu of the shelf. Pure: no processes are run here (DocTools does the work);
// which programs exist is looked up once and can be overridden by tests.
namespace ConvertModel {

enum class Kind { Other, Image, Svg, Pdf, Document, Text, Audio, Video };
enum class Engine { Qt, Poppler, Office, Ffmpeg };

struct Target {
    QString id;     // the extension it writes ("png", "mp3"); "mp4-small" for the compressed video
    QString label;  // as the menu shows it
    QString group;  // the menu section ("Image", "Document", "Audio", "Video", "Pages")
    Engine engine = Engine::Qt;
};

inline QString suffixOf(const QString &name) { return QFileInfo(name).suffix().toLower(); }

inline Kind kindOf(const QString &mime, const QString &name)
{
    const QString s = suffixOf(name);
    if (mime == QLatin1String("image/svg+xml") || s == QLatin1String("svg"))
        return Kind::Svg;
    if (mime.startsWith(QLatin1String("image/")))
        return Kind::Image;
    if (mime == QLatin1String("application/pdf") || s == QLatin1String("pdf"))
        return Kind::Pdf;
    if (mime.startsWith(QLatin1String("audio/")))
        return Kind::Audio;
    if (mime.startsWith(QLatin1String("video/")))
        return Kind::Video;
    static const QStringList office{"doc", "docx", "odt", "rtf", "xls", "xlsx", "ods", "ppt", "pptx", "odp"};
    if (office.contains(s) || mime.startsWith(QLatin1String("application/vnd.openxmlformats")) || mime.startsWith(QLatin1String("application/vnd.oasis.opendocument"))
        || mime == QLatin1String("application/msword") || mime == QLatin1String("application/rtf") || mime.startsWith(QLatin1String("application/vnd.ms-")))
        return Kind::Document;
    static const QStringList text{"md", "markdown", "html", "htm", "txt", "csv", "log"};
    if (text.contains(s) || mime == QLatin1String("text/plain") || mime == QLatin1String("text/markdown") || mime == QLatin1String("text/html") || mime == QLatin1String("text/csv"))
        return Kind::Text;
    static const QStringList audio{"mp3", "wav", "flac", "ogg", "opus", "m4a", "aac", "wma"};
    if (audio.contains(s)) return Kind::Audio;
    static const QStringList video{"mp4", "mkv", "webm", "mov", "avi", "m4v", "wmv", "flv"};
    if (video.contains(s)) return Kind::Video;
    return Kind::Other;
}

inline QString programFor(Engine e)
{
    switch (e) {
    case Engine::Poppler: return QStringLiteral("pdftoppm");
    case Engine::Office: return QStringLiteral("soffice");
    case Engine::Ffmpeg: return QStringLiteral("ffmpeg");
    case Engine::Qt: break;
    }
    return {};
}

inline QString needs(Engine e)
{
    switch (e) {
    case Engine::Poppler: return QStringLiteral("poppler-utils");
    case Engine::Office: return QStringLiteral("LibreOffice");
    case Engine::Ffmpeg: return QStringLiteral("ffmpeg");
    case Engine::Qt: break;
    }
    return {};
}

// Tests say which programs "exist" (a path, or empty for none) instead of looking.
inline QHash<int, QString> &forcedPrograms() { static QHash<int, QString> h; return h; }

// The program of an engine on this computer, or empty: on PATH, plus where Windows installers put LibreOffice.
inline QString programPath(Engine e)
{
    if (e == Engine::Qt) return QStringLiteral("qt");
    static QHash<int, QString> cache;
    if (forcedPrograms().contains(int(e))) return forcedPrograms().value(int(e));
    if (cache.contains(int(e))) return cache.value(int(e));
    QStringList extra;
#ifdef Q_OS_WIN
    if (e == Engine::Office) extra << QStringLiteral("C:/Program Files/LibreOffice/program") << QStringLiteral("C:/Program Files (x86)/LibreOffice/program");
#endif
    QString path = QStandardPaths::findExecutable(programFor(e), extra);
    if (path.isEmpty() && e == Engine::Office) path = QStandardPaths::findExecutable(QStringLiteral("libreoffice"), extra);
    cache.insert(int(e), path);
    return path;
}
inline bool have(Engine e) { return !programPath(e).isEmpty(); }

// What the file can become. Never its own format.
inline QList<Target> targetsFor(Kind kind, const QString &suffix)
{
    QList<Target> out;
    auto add = [&](const QString &id, const QString &label, const QString &group, Engine e) {
        if (id != suffix && !(id == QLatin1String("jpg") && suffix == QLatin1String("jpeg"))) out.append({id, label, group, e});
    };
    switch (kind) {
    case Kind::Image:
    case Kind::Svg:
        for (const auto &f : {std::pair<const char *, const char *>{"png", "PNG"}, {"jpg", "JPG"}, {"webp", "WebP"}, {"bmp", "BMP"}, {"tiff", "TIFF"}, {"ico", "ICO (icon)"}})
            add(QString::fromLatin1(f.first), QString::fromLatin1(f.second), QStringLiteral("Image"), Engine::Qt);
        add(QStringLiteral("pdf"), QStringLiteral("PDF"), QStringLiteral("Document"), Engine::Qt);
        break;
    case Kind::Pdf:
        add(QStringLiteral("png"), QStringLiteral("PNG, one per page"), QStringLiteral("Pages"), Engine::Poppler);
        add(QStringLiteral("jpg"), QStringLiteral("JPG, one per page"), QStringLiteral("Pages"), Engine::Poppler);
        add(QStringLiteral("txt"), QStringLiteral("Text (.txt)"), QStringLiteral("Document"), Engine::Poppler);
        add(QStringLiteral("docx"), QStringLiteral("Word (.docx)"), QStringLiteral("Document"), Engine::Office);
        add(QStringLiteral("odt"), QStringLiteral("OpenDocument (.odt)"), QStringLiteral("Document"), Engine::Office);
        break;
    case Kind::Document: {
        static const QStringList sheets{"xls", "xlsx", "ods"}, slides{"ppt", "pptx", "odp"};
        add(QStringLiteral("pdf"), QStringLiteral("PDF"), QStringLiteral("Document"), Engine::Office);
        if (sheets.contains(suffix)) {
            add(QStringLiteral("xlsx"), QStringLiteral("Excel (.xlsx)"), QStringLiteral("Document"), Engine::Office);
            add(QStringLiteral("ods"), QStringLiteral("OpenDocument (.ods)"), QStringLiteral("Document"), Engine::Office);
            add(QStringLiteral("csv"), QStringLiteral("CSV"), QStringLiteral("Document"), Engine::Office);
        } else if (slides.contains(suffix)) {
            add(QStringLiteral("pptx"), QStringLiteral("PowerPoint (.pptx)"), QStringLiteral("Document"), Engine::Office);
            add(QStringLiteral("odp"), QStringLiteral("OpenDocument (.odp)"), QStringLiteral("Document"), Engine::Office);
        } else {
            add(QStringLiteral("docx"), QStringLiteral("Word (.docx)"), QStringLiteral("Document"), Engine::Office);
            add(QStringLiteral("odt"), QStringLiteral("OpenDocument (.odt)"), QStringLiteral("Document"), Engine::Office);
            add(QStringLiteral("txt"), QStringLiteral("Text (.txt)"), QStringLiteral("Document"), Engine::Office);
        }
        add(QStringLiteral("html"), QStringLiteral("HTML"), QStringLiteral("Document"), Engine::Office);
        break;
    }
    case Kind::Text:
        add(QStringLiteral("pdf"), QStringLiteral("PDF"), QStringLiteral("Document"), Engine::Qt);
        break;
    case Kind::Audio:
        for (const auto &f : {std::pair<const char *, const char *>{"mp3", "MP3"}, {"wav", "WAV"}, {"flac", "FLAC"}, {"ogg", "OGG"}, {"m4a", "M4A (AAC)"}, {"opus", "Opus"}})
            add(QString::fromLatin1(f.first), QString::fromLatin1(f.second), QStringLiteral("Audio"), Engine::Ffmpeg);
        break;
    case Kind::Video:
        add(QStringLiteral("mp4"), QStringLiteral("MP4 (H.264)"), QStringLiteral("Video"), Engine::Ffmpeg);
        add(QStringLiteral("webm"), QStringLiteral("WebM (VP9)"), QStringLiteral("Video"), Engine::Ffmpeg);
        add(QStringLiteral("mkv"), QStringLiteral("MKV"), QStringLiteral("Video"), Engine::Ffmpeg);
        add(QStringLiteral("gif"), QStringLiteral("Animated GIF"), QStringLiteral("Video"), Engine::Ffmpeg);
        out.append({QStringLiteral("mp4-small"), QStringLiteral("Smaller MP4 (compress)"), QStringLiteral("Video"), Engine::Ffmpeg});
        add(QStringLiteral("mp3"), QStringLiteral("Audio only, MP3"), QStringLiteral("Audio"), Engine::Ffmpeg);
        add(QStringLiteral("m4a"), QStringLiteral("Audio only, M4A"), QStringLiteral("Audio"), Engine::Ffmpeg);
        break;
    case Kind::Other:
        break;
    }
    return out;
}

// One selection: all of one kind (and, for documents, one family of suffixes), else nothing to offer.
struct Pick { Kind kind = Kind::Other; QString suffix; };
inline QList<Target> targetsForSelection(const QList<Pick> &picks)
{
    if (picks.isEmpty()) return {};
    QList<Target> common = targetsFor(picks.first().kind, picks.first().suffix);
    for (const Pick &p : picks) {
        if (p.kind != picks.first().kind) return {};
        const QList<Target> mine = targetsFor(p.kind, p.suffix);
        QList<Target> keep;
        for (const Target &t : common)
            for (const Target &m : mine)
                if (m.id == t.id) { keep.append(t); break; }
        common = keep;
    }
    return common;
}

} // namespace ConvertModel
