#pragma once

#include <QList>
#include <functional>
#include <QPair>
#include <QString>
#include <QStringList>

// Document and image tools. Each writes its result to `out` (or files under outDir) and returns
// an empty string on success, else a user-facing reason. Blocking: run them on a worker thread.
// External programs: pdfunite / pdfseparate / pdfinfo / pdftoppm / pdftotext (poppler-utils), gs
// (ghostscript), tesseract, soffice (LibreOffice, Office documents), ffmpeg (audio and video),
// unzip / tar / 7z (archives). Images → PDF, image conversion and text → PDF use Qt only.
namespace DocTools {

QString mergePdfs(const QStringList &pdfs, const QString &out);
QString imagesToPdf(const QStringList &images, const QString &out); // one A4 page per image, fitted
// ghostscript; level: low (/printer, best quality) | medium (/ebook) | max (/screen, smallest)
QString compressPdf(const QString &pdf, const QString &out, const QString &level = QStringLiteral("medium"));
// Unpacks a .zip / .tar* / .7z / .rar into outDir (created as needed).
QString extractArchive(const QString &file, const QString &outDir);
int pdfPageCount(const QString &pdf);                                 // -1 if unreadable
QString splitPdf(const QString &pdf, const QString &outDir, const QString &baseName, QStringList *written);
QString extractPages(const QString &pdf, const QList<int> &pages, const QString &out);
// OCR of an image, or of a PDF's pages (rendered at 300 dpi). Text in *text.
QString recognizeText(const QString &file, bool isPdf, QString *text);
// format: "png" | "jpg" | "webp"; maxWidth 0 keeps the size (never upscales).
// quality < 0 keeps the format's usual setting, else it is passed to the encoder (jpg/webp).
QString convertImage(const QString &image, const QString &format, int maxWidth, const QString &out, int quality = -1);

// One file to another format (ConvertModel lists the targets: "png", "pdf", "mp3", "mp4-small"...). `reserve(name)` gives a fresh
// path in the work folder; every file written is added to *outputs as {path, display name}. Empty string on success.
QString convertFile(const QString &in, const QString &name, const QString &base, const QString &target, const QString &workDir,
                    const std::function<QString(const QString &)> &reserve, QList<QPair<QString, QString>> *outputs);

struct ToolInput {
    QString path; // the file to work on
    QString base; // its name without extension, for naming results
    QString name;
};
struct ToolResult {
    QString error;
    QList<QPair<QString, QString>> outputs; // written file, display name
    QString text;                           // OCR result
    QString summary;                        // one line for the user
};
// Runs one tool on inputs, writing results into workDir. kind: merge | images-pdf | compress (option:
// low | medium | max, empty = medium) | split | extract (option: page ranges) | unzip |
// ocr-copy | ocr-note | convert (option: target id from ConvertModel) | resize (option: max width) |
// compress-image (option: low | medium | max).
ToolResult runTool(const QString &kind, const QList<ToolInput> &inputs, const QString &option, const QString &workDir);

// "2-5, 8, 10-" → 1-based page numbers within 1..pageCount, in order, no duplicates.
// Empty list + error when the text is not a valid range.
QList<int> parsePageRanges(const QString &text, int pageCount, QString *error = nullptr);

}
