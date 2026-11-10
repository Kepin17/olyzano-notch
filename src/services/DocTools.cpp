#include "DocTools.h"
#include "OcrPacks.h"

#include "platform/ProcessRunner.h"
#include "services/ConvertModel.h"
#include "services/FileService.h"

#include <QDir>
#include <QScopeGuard>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>
#include <QTextDocument>
#include <QUrl>
#include <QUuid>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace DocTools {

namespace {
// Runs a program to completion; empty string on success.
QString run(const QString &program, const QStringList &args, QByteArray *stdOut = nullptr, int timeoutMs = 120000)
{
#ifdef Q_OS_WIN
    // Ghostscript's console program has another name there; the others keep theirs (put on PATH by their installers).
    const QString prog = program == QLatin1String("gs") ? QStringLiteral("gswin64c") : program;
#else
    const QString prog = program;
#endif
    const auto r = ProcessRunner::run(prog, args, timeoutMs);
    if (stdOut)
        *stdOut = r.out;
    if (r.ok)
        return {};
    if (r.failedToStart)
        return QStringLiteral("%1 is not installed (see the README)").arg(program);
    return r.error;
}

// A PNG's colour type in its IHDR chunk: 4 (grey + alpha) and 6 (RGBA) carry transparency.
bool pngHasAlpha(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.read(8) != QByteArray::fromHex("89504e470d0a1a0a") || !f.seek(25))
        return false;
    const QByteArray type = f.read(1);
    return type.size() == 1 && (uchar(type.at(0)) == 4 || uchar(type.at(0)) == 6);
}
}

QString mergePdfs(const QStringList &pdfs, const QString &out)
{
    if (pdfs.size() < 2)
        return QStringLiteral("choose at least two PDFs");
    return run(QStringLiteral("pdfunite"), QStringList(pdfs) << out);
}

QString imagesToPdf(const QStringList &images, const QString &out)
{
    if (images.isEmpty())
        return QStringLiteral("choose at least one image");
    QPdfWriter writer(out);
    writer.setResolution(150);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);
    QPainter painter;
    for (int i = 0; i < images.size(); ++i) {
        QImageReader reader(images.at(i));
        reader.setAutoTransform(true); // respect phone photos' EXIF rotation
        const QImage image = reader.read();
        if (image.isNull())
            return QStringLiteral("%1: %2").arg(QFileInfo(images.at(i)).fileName(), reader.errorString());
        // Landscape pictures get a landscape page. Orientation applies to the page started next.
        writer.setPageOrientation(image.width() > image.height() ? QPageLayout::Landscape : QPageLayout::Portrait);
        if (i == 0) {
            if (!painter.begin(&writer))
                return QStringLiteral("cannot write the PDF");
        } else {
            writer.newPage();
        }
        const QRect page = painter.viewport();
        const QSize fitted = image.size().scaled(page.size(), Qt::KeepAspectRatio);
        const QRect target(page.x() + (page.width() - fitted.width()) / 2, page.y() + (page.height() - fitted.height()) / 2,
                           fitted.width(), fitted.height());
        painter.drawImage(target, image);
    }
    painter.end();
    return {};
}

QString compressPdf(const QString &pdf, const QString &out, const QString &level)
{
    // low keeps the most (300 dpi), max throws the most away (72 dpi)
    const QString settings = level == QLatin1String("low")    ? QStringLiteral("/printer")
                           : level == QLatin1String("max")    ? QStringLiteral("/screen")
                                                              : QStringLiteral("/ebook");
    const QString err = run(QStringLiteral("gs"),
                            {QStringLiteral("-sDEVICE=pdfwrite"), QStringLiteral("-dCompatibilityLevel=1.5"),
                             QStringLiteral("-dPDFSETTINGS=") + settings,
                             // The presets alone do not rank: /screen may store the picture uncompressed and
                             // /printer passes a JPEG through untouched, so re-encode every colour image.
                             QStringLiteral("-dAutoFilterColorImages=false"), QStringLiteral("-dColorImageFilter=/DCTEncode"),
                             QStringLiteral("-dPassThroughJPEGImages=false"),
                             QStringLiteral("-dNOPAUSE"), QStringLiteral("-dBATCH"),
                             QStringLiteral("-dQUIET"), QStringLiteral("-sOutputFile=") + out, pdf},
                            nullptr, 600000);
    if (!err.isEmpty())
        return err;
    if (QFileInfo(out).size() >= QFileInfo(pdf).size()) {
        QFile::remove(out);
        return QStringLiteral("already as small as it gets");
    }
    return {};
}

QString watermarkPdf(const QString &pdf, const QString &out, const Watermark::Options &options)
{
    if (const QString bad = Watermark::validate(options.text); !bad.isEmpty())
        return bad;
    QFile head(pdf);
    if (!head.open(QIODevice::ReadOnly) || !head.read(1024).contains("%PDF-"))
        return QStringLiteral("this is not a PDF");
    head.close();
    const int pages = pdfPageCount(pdf);
    if (pages < 1)
        return pdfStatus(pdf) == QLatin1String("protected") ? QStringLiteral("this PDF is protected with a password: unlock it first (Convert → Protect)") : QStringLiteral("cannot read the PDF (it is damaged)");
    QList<int> chosen; // empty: every page
    if (!options.pages.isEmpty()) {
        QString err;
        chosen = parsePageRanges(options.pages, pages, &err);
        if (chosen.isEmpty())
            return err;
        if (chosen.size() == pages)
            chosen.clear();
    }
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return QStringLiteral("cannot make a work folder");
    const QString ps = tmp.filePath(QStringLiteral("watermark.ps"));
    QFile f(ps);
    if (!f.open(QIODevice::WriteOnly) || f.write(Watermark::postscript(options, chosen).toLatin1()) < 0)
        return QStringLiteral("cannot write the work file");
    f.close();
    const QString tmpOut = tmp.filePath(QStringLiteral("out.pdf"));
    if (const QString err = run(QStringLiteral("gs"), Watermark::gsArgs(ps, pdf, tmpOut), nullptr, 600000); !err.isEmpty())
        return err;
    if (QFileInfo(tmpOut).size() == 0 || pdfPageCount(tmpOut) != pages)
        return QStringLiteral("the watermark could not be added to this PDF");
    QFile::remove(out);
    return QFile::copy(tmpOut, out) ? QString() : QStringLiteral("cannot write %1").arg(QFileInfo(out).fileName());
}

QString stampPdf(const QString &pdf, const QString &out, const Stamp::Options &options)
{
    Stamp::Picture picture;
    if (options.picture) {
        QImageReader reader(options.image);
        reader.setAutoTransform(true);
        if (reader.size().isValid() && qint64(reader.size().width()) * reader.size().height() > 40000000)
            return QStringLiteral("the picture is too large");
        picture = Stamp::preparePicture(reader.read());
        if (picture.isEmpty())
            return QStringLiteral("cannot read the picture of the signature");
    } else if (const QString bad = Stamp::validate(options.text); !bad.isEmpty()) {
        return bad;
    }
    QFile head(pdf);
    if (!head.open(QIODevice::ReadOnly) || !head.read(1024).contains("%PDF-"))
        return QStringLiteral("this is not a PDF");
    head.close();
    const int pages = pdfPageCount(pdf);
    if (pages < 1)
        return pdfStatus(pdf) == QLatin1String("protected") ? QStringLiteral("this PDF is protected with a password: unlock it first (Convert → Protect)") : QStringLiteral("cannot read the PDF (it is damaged)");
    QList<int> chosen; // empty: every page
    if (options.pages == QLatin1String("last")) {
        chosen = {pages};
    } else if (options.pages == QLatin1String("first")) {
        chosen = {1};
    } else if (options.pages != QLatin1String("all")) {
        QString err;
        chosen = parsePageRanges(options.pages, pages, &err);
        if (chosen.isEmpty())
            return err;
    }
    if (chosen.size() == pages)
        chosen.clear();
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return QStringLiteral("cannot make a work folder");
    const QString ps = tmp.filePath(QStringLiteral("stamp.ps"));
    QFile f(ps);
    if (!f.open(QIODevice::WriteOnly) || f.write(Stamp::postscript(options, chosen, picture).toLatin1()) < 0)
        return QStringLiteral("cannot write the work file");
    f.close();
    const QString tmpOut = tmp.filePath(QStringLiteral("out.pdf"));
    if (const QString err = run(QStringLiteral("gs"), Stamp::gsArgs(ps, pdf, tmpOut), nullptr, 600000); !err.isEmpty())
        return err;
    if (QFileInfo(tmpOut).size() == 0 || pdfPageCount(tmpOut) != pages)
        return QStringLiteral("the stamp could not be added to this PDF");
    QFile::remove(out);
    return QFile::copy(tmpOut, out) ? QString() : QStringLiteral("cannot write %1").arg(QFileInfo(out).fileName());
}

QString pdfStatus(const QString &pdf)
{
    const auto r = ProcessRunner::run(QStringLiteral("pdfinfo"), {pdf}, 20000);
    if (r.ok)
        return QStringLiteral("ok");
    return PdfSecurity::isPasswordProblem(QString::fromLocal8Bit(r.err)) ? QStringLiteral("protected") : QStringLiteral("damaged");
}

namespace {
bool hasPdfHeader(const QString &pdf)
{
    QFile head(pdf);
    return head.open(QIODevice::ReadOnly) && head.read(1024).contains("%PDF-");
}

// Runs ghostscript with the passwords in an argument file of its own (mode 0600, in a folder only this account can enter); the result stays in `tmp`.
ProcessRunner::Result runWithArgFile(QTemporaryDir &tmp, const QString &argText, const QString &in, const QString &tmpOut)
{
    const QString argFile = tmp.filePath(QStringLiteral("args.txt"));
    QFile f(argFile);
    if (!f.open(QIODevice::WriteOnly) || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || f.write(argText.toUtf8()) < 0) {
        ProcessRunner::Result r;
        r.error = QStringLiteral("cannot write the work file");
        return r;
    }
    f.close();
#ifdef Q_OS_WIN
    const QString gs = QStringLiteral("gswin64c");
#else
    const QString gs = QStringLiteral("gs");
#endif
    ProcessRunner::Result r = ProcessRunner::run(gs, PdfSecurity::gsArgs(argFile, in, tmpOut), 600000);
    QFile::remove(argFile);
    if (r.failedToStart)
        r.error = QStringLiteral("gs is not installed (see the README)");
    return r;
}
}

QString protectPdf(const QString &pdf, const QString &out, const PdfSecurity::LockOptions &options)
{
    if (const QString bad = PdfSecurity::validatePassword(options.userPassword); !bad.isEmpty())
        return bad;
    if (!hasPdfHeader(pdf))
        return QStringLiteral("this is not a PDF");
    const QString status = pdfStatus(pdf);
    if (status == QLatin1String("protected"))
        return QStringLiteral("this PDF already has a password: unlock it first");
    if (status != QLatin1String("ok"))
        return QStringLiteral("cannot read the PDF (it is damaged)");
    PdfSecurity::LockOptions o = options;
    if (o.ownerPassword.isEmpty()) // nobody knows it: the permissions below are then not open to change by whoever opens the file
        o.ownerPassword = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(u'-').left(24);
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return QStringLiteral("cannot make a work folder");
    const QString tmpOut = tmp.filePath(QStringLiteral("out.pdf"));
    const auto r = runWithArgFile(tmp, PdfSecurity::lockArgs(o), pdf, tmpOut);
    if (!r.ok || QFileInfo(tmpOut).size() == 0)
        return r.error.isEmpty() ? QStringLiteral("the PDF could not be locked") : QStringLiteral("the PDF could not be locked (%1)").arg(r.error);
    if (pdfStatus(tmpOut) != QLatin1String("protected")) // what was written must ask for the password
        return QStringLiteral("the PDF could not be locked");
    QFile::remove(out);
    return QFile::copy(tmpOut, out) ? QString() : QStringLiteral("cannot write %1").arg(QFileInfo(out).fileName());
}

QString unlockPdf(const QString &pdf, const QString &out, const QString &password)
{
    if (password.isEmpty())
        return QStringLiteral("Type the password of the PDF.");
    if (!hasPdfHeader(pdf))
        return QStringLiteral("this is not a PDF");
    const QString status = pdfStatus(pdf);
    if (status == QLatin1String("ok"))
        return QStringLiteral("this PDF has no password");
    if (status != QLatin1String("protected"))
        return QStringLiteral("cannot read the PDF (it is damaged)");
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return QStringLiteral("cannot make a work folder");
    const QString tmpOut = tmp.filePath(QStringLiteral("out.pdf"));
    const auto r = runWithArgFile(tmp, PdfSecurity::unlockArgs(password), pdf, tmpOut);
    if (r.failedToStart)
        return r.error;
    if (!r.ok || PdfSecurity::isPasswordProblem(QString::fromLocal8Bit(r.err)) || pdfPageCount(tmpOut) < 1) { // a wrong password still gives exit 0 and an empty page
        QFile::remove(tmpOut);
        return QStringLiteral("the password did not work");
    }
    QFile::remove(out);
    return QFile::copy(tmpOut, out) ? QString() : QStringLiteral("cannot write %1").arg(QFileInfo(out).fileName());
}

QString extractArchive(const QString &file, const QString &outDir)
{
    const QString suffix = QFileInfo(file).suffix().toLower();
    QDir().mkpath(outDir);
    const auto tar = [&](const QString &program) { return run(program, {QStringLiteral("-xf"), file, QStringLiteral("-C"), outDir}); };

    if (suffix == QLatin1String("zip")) {
#ifdef Q_OS_WIN
        // Windows 10 and newer ship bsdtar as tar.exe, and it reads zip.
        const QString err = tar(QStringLiteral("tar"));
#else
        // -P "" so an encrypted entry fails at once instead of prompting on stdin forever.
        const QString err = run(QStringLiteral("unzip"),
                                {QStringLiteral("-o"), QStringLiteral("-q"), QStringLiteral("-P"), QString(),
                                 QStringLiteral("-d"), outDir, file});
#endif
        if (err.isEmpty())
            return {};
        // Info-ZIP exits 1 on warnings (it skips "../" entries for security): accept what it wrote.
        return QDir(outDir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty() ? err : QString();
    }
    if (suffix == QLatin1String("tar") || suffix == QLatin1String("gz") || suffix == QLatin1String("tgz")
        || suffix == QLatin1String("bz2") || suffix == QLatin1String("tbz2") || suffix == QLatin1String("xz")
        || suffix == QLatin1String("txz") || suffix == QLatin1String("zst"))
        return tar(QStringLiteral("tar"));
    if (suffix == QLatin1String("7z") || suffix == QLatin1String("rar")) {
        QStringList extra;
#ifdef Q_OS_WIN
        extra << QStringLiteral("C:/Program Files/7-Zip") << QStringLiteral("C:/Program Files (x86)/7-Zip");
#endif
        QString program = QStandardPaths::findExecutable(QStringLiteral("7z"), extra);
        if (program.isEmpty())
            program = QStandardPaths::findExecutable(QStringLiteral("7za"), extra);
        if (program.isEmpty())
            return QStringLiteral("7-Zip is not installed (see the README)");
        // -p: an empty password, so an encrypted archive fails instead of prompting.
        return run(program, {QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-p"),
                             QStringLiteral("-o") + outDir, file});
    }
    return QStringLiteral("cannot extract .%1").arg(suffix);
}

int pdfPageCount(const QString &pdf)
{
    QByteArray info;
    if (!run(QStringLiteral("pdfinfo"), {pdf}, &info, 20000).isEmpty())
        return -1;
    static const QRegularExpression pages(QStringLiteral(R"(^Pages:\s+(\d+))"), QRegularExpression::MultilineOption);
    const auto m = pages.match(QString::fromUtf8(info));
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

QString splitPdf(const QString &pdf, const QString &outDir, const QString &baseName, QStringList *written)
{
    const QString pattern = outDir + u'/' + baseName + QStringLiteral(" - page %d.pdf");
    if (const QString err = run(QStringLiteral("pdfseparate"), {pdf, pattern}); !err.isEmpty())
        return err;
    const int count = pdfPageCount(pdf);
    for (int i = 1; i <= count; ++i)
        if (written)
            *written << QString(pattern).replace(QStringLiteral("%d"), QString::number(i));
    return {};
}

QString extractPages(const QString &pdf, const QList<int> &pages, const QString &out)
{
    if (pages.isEmpty())
        return QStringLiteral("no pages chosen");
    QTemporaryDir tmp;
    QStringList parts;
    for (int page : pages) {
        const QString part = tmp.filePath(QStringLiteral("p%1.pdf").arg(page));
        if (const QString err = run(QStringLiteral("pdfseparate"),
                                    {QStringLiteral("-f"), QString::number(page), QStringLiteral("-l"), QString::number(page), pdf, part});
            !err.isEmpty())
            return err;
        parts << part;
    }
    if (parts.size() == 1)
        return QFile::copy(parts.first(), out) ? QString() : QStringLiteral("cannot write the PDF");
    return run(QStringLiteral("pdfunite"), QStringList(parts) << out);
}

QString recognizeText(const QString &file, bool isPdf, QString *text)
{
    QStringList images{file};
    QTemporaryDir tmp;
    if (isPdf) {
        if (const QString err = run(QStringLiteral("pdftoppm"),
                                    {QStringLiteral("-r"), QStringLiteral("300"), QStringLiteral("-png"), file, tmp.filePath(QStringLiteral("page"))},
                                    nullptr, 300000);
            !err.isEmpty())
            return err;
        images.clear();
        const QStringList rendered = QDir(tmp.path()).entryList({QStringLiteral("page*.png")}, QDir::Files, QDir::Name);
        for (const QString &name : rendered)
            images << tmp.filePath(name);
    }
    QStringList pages;
    const OcrPacks::Setup packs = OcrPacks::current(); // the languages in use that have a pack (Settings → Languages), and English
    for (const QString &image : images) {
        QByteArray out;
        QStringList args{image, QStringLiteral("stdout")};
        if (!packs.languages.isEmpty())
            args << QStringLiteral("-l") << packs.languages;
        if (!packs.tessdataDir.isEmpty())
            args << QStringLiteral("--tessdata-dir") << packs.tessdataDir;
        if (const QString err = run(QStringLiteral("tesseract"), args, &out, 120000); !err.isEmpty())
            return err;
        pages << QString::fromUtf8(out).trimmed();
    }
    *text = pages.join(QStringLiteral("\n\n"));
    return text->isEmpty() ? QStringLiteral("no text found") : QString();
}

QString convertImage(const QString &image, const QString &format, int maxWidth, const QString &out, int quality)
{
    QImageReader reader(image);
    reader.setAutoTransform(true);
    if (reader.format().startsWith("svg")) { // a drawing has no pixels of its own: twice its nominal size reads sharp
        const QSize nominal = reader.size();
        if (nominal.isValid())
            reader.setScaledSize(nominal.scaled(QSize(4096, 4096), Qt::KeepAspectRatio).boundedTo(nominal * 2));
    }
    QImage img = reader.read();
    if (img.isNull())
        return reader.errorString();
    if (maxWidth > 0 && img.width() > maxWidth)
        img = img.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    if (format == QLatin1String("ico") && (img.width() > 256 || img.height() > 256)) // an icon file holds at most 256 px
        img = img.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (format == QLatin1String("jpg") && img.hasAlphaChannel()) { // JPEG has no transparency: put it on white
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter(&flat).drawImage(0, 0, img);
        img = flat;
    }
    // For PNG Qt reads "quality" as (inverse) compression: keep its default, which compresses well.
    const bool fixedFormat = format == QLatin1String("png") || format == QLatin1String("bmp") || format == QLatin1String("ico") || format == QLatin1String("tiff");
    const int saveQuality = fixedFormat ? -1 : (quality >= 0 ? quality : 90);
    return img.save(out, format.toLatin1().constData(), saveQuality) ? QString() : QStringLiteral("cannot write %1").arg(format);
}

namespace {
using ConvertModel::Engine;
using ConvertModel::Kind;

QString runEngine(Engine e, const QStringList &args, int timeoutMs)
{
    const QString path = ConvertModel::programPath(e);
    if (path.isEmpty())
        return QStringLiteral("%1 is not installed (needed for this conversion)").arg(ConvertModel::needs(e));
    const auto r = ProcessRunner::run(path, args, timeoutMs);
    return r.ok ? QString() : r.error;
}

QString textToPdf(const QString &in, const QString &suffix, const QString &out)
{
    QFile f(in);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 8 * 1024 * 1024)
        return QStringLiteral("cannot read the file, or it is larger than 8 MB");
    const QString text = QString::fromUtf8(f.readAll());
    QTextDocument doc;
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown"))
        doc.setMarkdown(text);
    else if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        doc.setHtml(text);
    else {
        QFont mono(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::Monospace);
        doc.setDefaultFont(mono);
        doc.setPlainText(text);
    }
    QPdfWriter writer(out);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    doc.print(&writer);
    return QFileInfo(out).size() > 0 ? QString() : QStringLiteral("cannot write the PDF");
}

// Codec arguments per target. -vn drops the picture when the target is sound only.
QStringList ffmpegArgs(const QString &t)
{
    if (t == QLatin1String("mp3")) return {"-vn", "-c:a", "libmp3lame", "-q:a", "2"};
    if (t == QLatin1String("wav")) return {"-vn", "-c:a", "pcm_s16le"};
    if (t == QLatin1String("flac")) return {"-vn", "-c:a", "flac"};
    if (t == QLatin1String("ogg")) return {"-vn", "-c:a", "libvorbis", "-q:a", "5"};
    if (t == QLatin1String("m4a")) return {"-vn", "-c:a", "aac", "-b:a", "192k"};
    if (t == QLatin1String("opus")) return {"-vn", "-c:a", "libopus", "-b:a", "128k"};
    if (t == QLatin1String("mp4")) return {"-c:v", "libx264", "-crf", "23", "-preset", "medium", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "160k", "-movflags", "+faststart"};
    if (t == QLatin1String("mp4-small")) return {"-c:v", "libx264", "-crf", "28", "-preset", "medium", "-vf", "scale='min(1280,iw)':-2", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "96k", "-movflags", "+faststart"};
    if (t == QLatin1String("webm")) return {"-c:v", "libvpx-vp9", "-crf", "32", "-b:v", "0", "-c:a", "libopus"};
    if (t == QLatin1String("mkv")) return {"-c", "copy"};
    if (t == QLatin1String("gif")) return {"-t", "30", "-filter_complex", "fps=12,scale=480:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse", "-loop", "0"};
    return {};
}
}

QString convertFile(const QString &in, const QString &name, const QString &base, const QString &target, const QString &workDir,
                    const std::function<QString(const QString &)> &reserve, QList<QPair<QString, QString>> *outputs)
{
    const QString suffix = ConvertModel::suffixOf(name);
    const Kind kind = ConvertModel::kindOf(QMimeDatabase().mimeTypeForFile(in).name(), name);
    const auto offered = ConvertModel::targetsFor(kind, suffix);
    const auto hit = std::find_if(offered.begin(), offered.end(), [&](const ConvertModel::Target &t) { return t.id == target; });
    if (hit == offered.end())
        return QStringLiteral("cannot convert this file to %1").arg(target);
    if (!ConvertModel::have(hit->engine))
        return QStringLiteral("%1 is not installed (needed for this conversion)").arg(ConvertModel::needs(hit->engine));
    const QString ext = target == QLatin1String("mp4-small") ? QStringLiteral("mp4") : target;
    const QString stem = target == QLatin1String("mp4-small") ? base + QStringLiteral(" (small)") : base;
    auto done = [&](const QString &path) { outputs->append(qMakePair(path, QFileInfo(path).fileName())); };

    if (kind == Kind::Image || kind == Kind::Svg) {
        const QString out = reserve(stem + u'.' + ext);
        const QString err = ext == QLatin1String("pdf") ? imagesToPdf({in}, out) : convertImage(in, ext, 0, out);
        if (err.isEmpty()) done(out);
        return err;
    }
    if (kind == Kind::Text) {
        const QString out = reserve(stem + QStringLiteral(".pdf"));
        const QString err = textToPdf(in, suffix, out);
        if (err.isEmpty()) done(out);
        return err;
    }
    if (kind == Kind::Pdf && ext == QLatin1String("txt")) {
        const QString out = reserve(stem + QStringLiteral(".txt"));
        const QString err = run(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), in, out});
        if (err.isEmpty()) done(out);
        return err;
    }
    if (kind == Kind::Pdf && (ext == QLatin1String("docx") || ext == QLatin1String("odt"))) {
        constexpr int MaxPages = 200;
        const int pages = pdfPageCount(in);
        if (pages < 1)
            return QStringLiteral("cannot read the PDF (damaged or protected with a password)");
        if (pages > MaxPages)
            return QStringLiteral("the PDF has %1 pages; up to %2 are converted at once (extract the pages you need first)").arg(pages).arg(MaxPages);
        const QString dir = workDir + QStringLiteral("/office-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(dir);
        const auto cleanup = qScopeGuard([&] { QDir(dir).removeRecursively(); });
        // a page with no text is a picture: the words are read with OCR and only they come over (no layout)
        QByteArray words;
        run(QStringLiteral("pdftotext"), {in, QStringLiteral("-")}, &words, 120000);
        QString source = in, filter = QStringLiteral("writer_pdf_import");
        if (QString::fromUtf8(words).simplified().size() < 20) {
            QString text;
            if (const QString err = recognizeText(in, true, &text); !err.isEmpty())
                return QStringLiteral("this PDF is a scan and reading it failed: %1").arg(err);
            if (text.simplified().isEmpty())
                return QStringLiteral("no text was found in this PDF");
            source = dir + QStringLiteral("/scan.txt");
            QFile f(source);
            if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0)
                return QStringLiteral("cannot write the work file");
            f.close();
            filter.clear();
        }
        QStringList args{QStringLiteral("--headless"), QStringLiteral("--norestore"), QStringLiteral("--nologo"),
                         QStringLiteral("-env:UserInstallation=") + QUrl::fromLocalFile(dir + QStringLiteral("/profile")).toString()};
        if (!filter.isEmpty())
            args << QStringLiteral("--infilter=") + filter;
        args << QStringLiteral("--convert-to") << (ext == QLatin1String("docx") ? QStringLiteral("docx:MS Word 2007 XML") : QStringLiteral("odt:writer8"))
             << QStringLiteral("--outdir") << dir + QStringLiteral("/out") << source;
        if (const QString err = runEngine(Engine::Office, args, 300000); !err.isEmpty())
            return err;
        const QStringList made = QDir(dir + QStringLiteral("/out")).entryList({QStringLiteral("*.") + ext}, QDir::Files);
        if (made.isEmpty())
            return QStringLiteral("LibreOffice could not convert this file");
        const QString out = reserve(stem + u'.' + ext);
        if (!QFile::rename(dir + QStringLiteral("/out/") + made.first(), out))
            return QStringLiteral("cannot write %1").arg(QFileInfo(out).fileName());
        done(out);
        return {};
    }
    if (kind == Kind::Pdf) { // one picture per page
        constexpr int MaxPages = 200;
        const int pages = pdfPageCount(in);
        if (pages > MaxPages)
            return QStringLiteral("the PDF has %1 pages; up to %2 are converted at once (extract the pages you need first)").arg(pages).arg(MaxPages);
        const QString dir = workDir + QStringLiteral("/render-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(dir);
        if (const QString err = run(QStringLiteral("pdftoppm"), {QStringLiteral("-r"), QStringLiteral("150"), ext == QLatin1String("png") ? QStringLiteral("-png") : QStringLiteral("-jpeg"), in, dir + QStringLiteral("/p")}, nullptr, 600000);
            !err.isEmpty())
            return err;
        const QStringList rendered = QDir(dir).entryList({QStringLiteral("p-*")}, QDir::Files, QDir::Name); // padded to the same width: name order is page order
        int page = 0;
        for (const QString &file : rendered) {
            const QString out = reserve(QStringLiteral("%1 - page %2.%3").arg(stem).arg(++page).arg(ext));
            if (!QFile::rename(dir + u'/' + file, out))
                return QStringLiteral("cannot write %1").arg(QFileInfo(out).fileName());
            done(out);
        }
        QDir(dir).removeRecursively();
        return rendered.isEmpty() ? QStringLiteral("no pages were rendered") : QString();
    }
    if (kind == Kind::Document) {
        const QString dir = workDir + QStringLiteral("/office-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(dir);
        // a profile of its own: a LibreOffice the person has open would otherwise take the job, or refuse it
        const QString filter = ext == QLatin1String("txt") ? QStringLiteral("txt:Text") : ext;
        if (const QString err = runEngine(Engine::Office, {QStringLiteral("--headless"), QStringLiteral("--norestore"), QStringLiteral("--nologo"),
                                                           QStringLiteral("-env:UserInstallation=") + QUrl::fromLocalFile(dir + QStringLiteral("/profile")).toString(),
                                                           QStringLiteral("--convert-to"), filter, QStringLiteral("--outdir"), dir + QStringLiteral("/out"), in}, 180000);
            !err.isEmpty())
            return err;
        const QStringList made = QDir(dir + QStringLiteral("/out")).entryList(QDir::Files, QDir::Name);
        if (made.isEmpty())
            return QStringLiteral("LibreOffice could not convert this file");
        const QString out = reserve(stem + u'.' + ext);
        if (!QFile::rename(dir + QStringLiteral("/out/") + made.first(), out))
            return QStringLiteral("cannot write %1").arg(QFileInfo(out).fileName());
        QDir(dir).removeRecursively();
        done(out);
        return {};
    }
    if (kind == Kind::Audio || kind == Kind::Video) {
        const QString out = reserve(stem + u'.' + ext);
        QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y"), QStringLiteral("-i"), in};
        args << ffmpegArgs(target) << out;
        const QString err = runEngine(Engine::Ffmpeg, args, kind == Kind::Video ? 30 * 60 * 1000 : 10 * 60 * 1000);
        if (err.isEmpty() && QFileInfo(out).size() > 0) { done(out); return {}; }
        QFile::remove(out);
        return err.isEmpty() ? QStringLiteral("ffmpeg wrote nothing") : err;
    }
    return QStringLiteral("cannot convert this file");
}

QList<int> parsePageRanges(const QString &text, int pageCount, QString *error)
{
    QList<int> pages;
    QSet<int> seen;
    static const QRegularExpression part(QStringLiteral(R"(^\s*(\d*)\s*(-)?\s*(\d*)\s*$)"));
    for (const QString &chunk : text.split(u',', Qt::SkipEmptyParts)) {
        const auto m = part.match(chunk);
        if (!m.hasMatch() || (m.captured(1).isEmpty() && m.captured(3).isEmpty())) {
            if (error)
                *error = QStringLiteral("“%1” is not a page or range").arg(chunk.trimmed());
            return {};
        }
        const bool range = !m.captured(2).isEmpty();
        const int from = m.captured(1).isEmpty() ? 1 : m.captured(1).toInt();
        const int to = range ? (m.captured(3).isEmpty() ? pageCount : m.captured(3).toInt()) : from;
        if (from < 1 || to > pageCount || from > to) {
            if (error)
                *error = QStringLiteral("pages must be within 1–%1").arg(pageCount);
            return {};
        }
        for (int p = from; p <= to; ++p)
            if (!seen.contains(p)) {
                seen.insert(p);
                pages << p;
            }
    }
    if (pages.isEmpty() && error)
        *error = QStringLiteral("no pages chosen");
    return pages;
}


ToolResult runTool(const QString &kind, const QList<ToolInput> &inputs, const QString &option, const QString &workDir)
{
    ToolResult r;
    const auto out = [&](const QString &name) {
        const QString path = workDir + u'/' + FileService::uniqueName(workDir, FileService::safeFileName(name));
        r.outputs << qMakePair(path, QFileInfo(path).fileName());
        return path;
    };
    QStringList paths;
    for (const ToolInput &in : inputs)
        paths << in.path;
    if (kind == QLatin1String("merge")) {
        r.error = mergePdfs(paths, out(QStringLiteral("Merged (%1 PDFs).pdf").arg(paths.size())));
        r.summary = QStringLiteral("Merged %1 PDFs").arg(paths.size());
    } else if (kind == QLatin1String("images-pdf")) {
        const QString name = paths.size() == 1 ? inputs.first().base + QStringLiteral(".pdf")
                                               : QStringLiteral("Images (%1 pages).pdf").arg(paths.size());
        r.error = imagesToPdf(paths, out(name));
        r.summary = QStringLiteral("Made a %1-page PDF").arg(paths.size());
    } else if (kind == QLatin1String("compress")) {
        qint64 before = 0, after = 0;
        for (const ToolInput &in : inputs) {
            const QString target = out(in.base + QStringLiteral(" (compressed).pdf"));
            if (const QString err = compressPdf(in.path, target, option); !err.isEmpty()) {
                r.outputs.removeLast();
                r.error = in.name + QStringLiteral(": ") + err;
                continue;
            }
            before += QFileInfo(in.path).size();
            after += QFileInfo(target).size();
        }
        if (!r.outputs.isEmpty())
            r.summary = QStringLiteral("Compressed: %1 → %2")
                            .arg(QLocale().formattedDataSize(before), QLocale().formattedDataSize(after));
    } else if (kind == QLatin1String("unzip")) {
        int archive = 0;
        for (const ToolInput &in : inputs) {
            ++archive;
            const QString dir = workDir + QStringLiteral("/extract-") + QString::number(archive);
            if (const QString err = extractArchive(in.path, dir); !err.isEmpty()) {
                r.error = in.name + QStringLiteral(": ") + err;
                continue;
            }
            // Only what came out of the archive and sits under `dir` is taken onto the shelf.
            int taken = 0;
            const auto files = FileService::expandFolder(dir, 1000);
            for (const FileService::FolderEntry &entry : files) {
                const QString target = workDir + u'/'
                                       + FileService::uniqueName(workDir, FileService::safeFileName(QFileInfo(entry.path).fileName()));
                if (QFile::rename(entry.path, target)) {
                    r.outputs << qMakePair(target, QFileInfo(target).fileName());
                    ++taken;
                } else if (r.error.isEmpty()) {
                    r.error = in.name + QStringLiteral(": cannot write ") + QFileInfo(entry.path).fileName();
                }
            }
            if (taken == 0 && r.error.isEmpty())
                r.error = in.name + QStringLiteral(": the archive has no files");
        }
        r.summary = QStringLiteral("Extracted %1 file%2").arg(r.outputs.size()).arg(r.outputs.size() == 1 ? QString() : QStringLiteral("s"));
    } else if (kind == QLatin1String("compress-image")) {
        // jpg/webp keep their format, png becomes jpg (webp when it carries transparency), the rest become jpg
        const int quality = option == QLatin1String("low") ? 85 : option == QLatin1String("max") ? 40 : 65;
        qint64 before = 0, after = 0;
        for (const ToolInput &in : inputs) {
            const QString suffix = QFileInfo(in.name).suffix().toLower();
            const QString format = suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg") ? QStringLiteral("jpg")
                                 : suffix == QLatin1String("webp") ? QStringLiteral("webp")
                                 : suffix == QLatin1String("png")  ? (pngHasAlpha(in.path) ? QStringLiteral("webp") : QStringLiteral("jpg"))
                                                                  : QStringLiteral("jpg");
            const QString target = out(in.base + QStringLiteral(" (compressed).") + format);
            if (const QString err = convertImage(in.path, format, 0, target, quality); !err.isEmpty()) {
                r.outputs.removeLast();
                r.error = in.name + QStringLiteral(": ") + err;
                continue;
            }
            if (QFileInfo(target).size() >= QFileInfo(in.path).size()) { // nothing gained: no point keeping it
                QFile::remove(target);
                r.outputs.removeLast();
                r.error = in.name + QStringLiteral(": already as small as it gets");
                continue;
            }
            before += QFileInfo(in.path).size();
            after += QFileInfo(target).size();
        }
        if (!r.outputs.isEmpty())
            r.summary = QStringLiteral("Compressed: %1 → %2")
                            .arg(QLocale().formattedDataSize(before), QLocale().formattedDataSize(after));
    } else if (kind == QLatin1String("split")) {
        QStringList pages;
        r.error = splitPdf(inputs.first().path, workDir, inputs.first().base, &pages);
        for (const QString &page : pages)
            r.outputs << qMakePair(page, QFileInfo(page).fileName());
        r.summary = QStringLiteral("Split into %1 pages").arg(pages.size());
    } else if (kind == QLatin1String("extract")) {
        QString err;
        const QList<int> pages = parsePageRanges(option, pdfPageCount(inputs.first().path), &err);
        r.error = pages.isEmpty() ? err
                                  : extractPages(inputs.first().path, pages,
                                                           out(inputs.first().base + QStringLiteral(" (pages %1).pdf").arg(QString(option.simplified()).remove(QRegularExpression(QStringLiteral("[^0-9,\\- ]"))))));
        r.summary = QStringLiteral("Extracted %1 pages").arg(pages.size());
    } else if (kind == QLatin1String("ocr-copy") || kind == QLatin1String("ocr-note")) {
        const bool pdf = inputs.first().name.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive);
        r.error = recognizeText(inputs.first().path, pdf, &r.text);
    } else if (kind == QLatin1String("convert")) {
        const auto reserve = [&](const QString &name) { return workDir + u'/' + FileService::uniqueName(workDir, FileService::safeFileName(name)); };
        int files = 0;
        for (const ToolInput &in : inputs) {
            if (const QString err = convertFile(in.path, in.name, in.base, option, workDir, reserve, &r.outputs); !err.isEmpty())
                r.error = in.name + QStringLiteral(": ") + err;
            else
                ++files;
        }
        r.summary = QStringLiteral("Converted %1 file%2 → %3 file%4").arg(files).arg(files == 1 ? QString() : QStringLiteral("s"))
                        .arg(r.outputs.size()).arg(r.outputs.size() == 1 ? QString() : QStringLiteral("s"));
    } else if (kind == QLatin1String("watermark")) {
        const Watermark::Options options = Watermark::fromJson(option);
        QTemporaryDir tmp; // Word and the like become a PDF here first
        for (const ToolInput &in : inputs) {
            QString source = in.path;
            if (!in.name.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
                QList<QPair<QString, QString>> made;
                const auto reserve = [&](const QString &name) { return tmp.filePath(FileService::uniqueName(tmp.path(), FileService::safeFileName(name))); };
                if (const QString err = convertFile(in.path, in.name, in.base, QStringLiteral("pdf"), tmp.path(), reserve, &made); !err.isEmpty() || made.isEmpty()) {
                    r.error = in.name + QStringLiteral(": ") + (err.isEmpty() ? QStringLiteral("cannot make a PDF") : err);
                    continue;
                }
                source = made.first().first;
            }
            const QString target = out(in.base + QStringLiteral(" (%1).pdf").arg(Watermark::label(options.text)));
            if (const QString err = watermarkPdf(source, target, options); !err.isEmpty()) {
                r.outputs.removeLast();
                r.error = in.name + QStringLiteral(": ") + err;
            }
        }
        r.summary = QStringLiteral("Added “%1” to %2 PDF%3").arg(options.text.simplified()).arg(r.outputs.size()).arg(r.outputs.size() == 1 ? QString() : QStringLiteral("s"));
    } else if (kind == QLatin1String("stamp")) {
        const Stamp::Options stamp = Stamp::fromJson(option);
        for (const ToolInput &in : inputs) {
            const QString source = in.path;
            if (!in.name.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
                r.error = in.name + QStringLiteral(": only a PDF can be stamped (convert it to PDF first)");
                continue;
            }
            const QString target = out(in.base + QStringLiteral(" (%1).pdf").arg(Stamp::label(stamp)));
            if (const QString err = stampPdf(source, target, stamp); !err.isEmpty()) {
                r.outputs.removeLast();
                r.error = in.name + QStringLiteral(": ") + err;
            }
        }
        r.summary = stamp.picture ? QStringLiteral("Signature added") : QStringLiteral("Stamp “%1” added").arg(stamp.text.simplified());
    } else if (kind == QLatin1String("lock") || kind == QLatin1String("unlock")) {
        for (const ToolInput &in : inputs) {
            QString err;
            QString target;
            if (kind == QLatin1String("lock")) {
                const QJsonObject j = QJsonDocument::fromJson(option.toUtf8()).object();
                PdfSecurity::LockOptions lock;
                lock.userPassword = j.value(QStringLiteral("user")).toString();
                lock.allowPrint = j.value(QStringLiteral("print")).toBool(true);
                lock.allowCopy = j.value(QStringLiteral("copy")).toBool(true);
                lock.allowChange = j.value(QStringLiteral("change")).toBool(true);
                target = out(in.base + QStringLiteral(" (locked).pdf"));
                err = protectPdf(in.path, target, lock);
            } else {
                target = out(in.base + QStringLiteral(" (unlocked).pdf"));
                err = unlockPdf(in.path, target, option);
            }
            if (!err.isEmpty()) {
                r.outputs.removeLast();
                r.error = in.name + QStringLiteral(": ") + err;
            }
        }
        r.summary = kind == QLatin1String("lock") ? QStringLiteral("PDF locked with a password") : QStringLiteral("PDF opened without a password");
    } else if (kind == QLatin1String("resize")) {
        for (const ToolInput &in : inputs) {
            const int width = option.toInt();
            const QString ext = QFileInfo(in.name).suffix().toLower();
            const QString format = ext == QLatin1String("jpeg") ? QStringLiteral("jpg")
                                 : (ext == QLatin1String("jpg") || ext == QLatin1String("webp")) ? ext : QStringLiteral("png");
            const QString target = out(in.base + QStringLiteral(" (%1px)").arg(width) + u'.' + format);
            if (const QString err = convertImage(in.path, format, width, target); !err.isEmpty()) {
                r.outputs.removeLast();
                r.error = in.name + QStringLiteral(": ") + err;
            }
        }
        r.summary = QStringLiteral("Resized %1 image%2").arg(r.outputs.size()).arg(r.outputs.size() == 1 ? QString() : QStringLiteral("s"));
    } else {
        r.error = QStringLiteral("unknown tool %1").arg(kind);
    }
    return r;
}

}
