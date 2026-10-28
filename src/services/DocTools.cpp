#include "DocTools.h"

#include "services/FileService.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

namespace DocTools {

namespace {
// Runs a program to completion; empty string on success.
QString run(const QString &program, const QStringList &args, QByteArray *stdOut = nullptr, int timeoutMs = 120000)
{
    QProcess p;
#ifdef Q_OS_WIN
    // Ghostscript's console program has another name there; the others keep theirs (put on PATH by their installers).
    p.start(program == QLatin1String("gs") ? QStringLiteral("gswin64c") : program, args);
#else
    p.start(program, args);
#endif
    if (!p.waitForStarted(5000))
        return QStringLiteral("%1 is not installed (see the README for the PDF and OCR tools)").arg(program);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        return QStringLiteral("%1 took too long").arg(program);
    }
    if (stdOut)
        *stdOut = p.readAllStandardOutput();
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed().section(u'\n', 0, 0);
        return err.isEmpty() ? QStringLiteral("%1 failed (exit %2)").arg(program).arg(p.exitCode()) : err;
    }
    return {};
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

QString compressPdf(const QString &pdf, const QString &out)
{
    const QString err = run(QStringLiteral("gs"),
                            {QStringLiteral("-sDEVICE=pdfwrite"), QStringLiteral("-dCompatibilityLevel=1.5"),
                             QStringLiteral("-dPDFSETTINGS=/ebook"), QStringLiteral("-dNOPAUSE"), QStringLiteral("-dBATCH"),
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
    for (const QString &image : images) {
        QByteArray out;
        if (const QString err = run(QStringLiteral("tesseract"), {image, QStringLiteral("stdout")}, &out, 120000); !err.isEmpty())
            return err;
        pages << QString::fromUtf8(out).trimmed();
    }
    *text = pages.join(QStringLiteral("\n\n"));
    return text->isEmpty() ? QStringLiteral("no text found") : QString();
}

QString convertImage(const QString &image, const QString &format, int maxWidth, const QString &out)
{
    QImageReader reader(image);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull())
        return reader.errorString();
    if (maxWidth > 0 && img.width() > maxWidth)
        img = img.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    if (format == QLatin1String("jpg") && img.hasAlphaChannel()) { // JPEG has no transparency: put it on white
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter(&flat).drawImage(0, 0, img);
        img = flat;
    }
    // For PNG Qt reads "quality" as (inverse) compression: keep its default, which compresses well.
    const int quality = format == QLatin1String("png") ? -1 : 90;
    return img.save(out, format.toLatin1().constData(), quality) ? QString() : QStringLiteral("cannot write %1").arg(format);
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
            if (const QString err = compressPdf(in.path, target); !err.isEmpty()) {
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
    } else if (kind == QLatin1String("convert") || kind == QLatin1String("resize")) {
        for (const ToolInput &in : inputs) {
            QString format = option, suffix;
            int width = 0;
            if (kind == QLatin1String("resize")) {
                width = option.toInt();
                const QString ext = QFileInfo(in.name).suffix().toLower();
                format = ext == QLatin1String("jpeg") ? QStringLiteral("jpg")
                       : (ext == QLatin1String("jpg") || ext == QLatin1String("webp")) ? ext : QStringLiteral("png");
                suffix = QStringLiteral(" (%1px)").arg(width);
            }
            const QString target = out(in.base + suffix + u'.' + format);
            if (const QString err = convertImage(in.path, format, width, target); !err.isEmpty()) {
                r.outputs.removeLast();
                r.error = in.name + QStringLiteral(": ") + err;
            }
        }
        r.summary = QStringLiteral("%1 %2 image%3").arg(kind == QLatin1String("resize") ? QStringLiteral("Resized") : QStringLiteral("Converted"))
                        .arg(r.outputs.size()).arg(r.outputs.size() == 1 ? QString() : QStringLiteral("s"));
    } else {
        r.error = QStringLiteral("unknown tool %1").arg(kind);
    }
    return r;
}

}
