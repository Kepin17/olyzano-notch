#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Document and image tools. Each writes its result to `out` (or files under outDir) and returns
// an empty string on success, else a user-facing reason. Blocking: run them on a worker thread.
// External programs: pdfunite / pdfseparate / pdfinfo / pdftoppm (poppler-utils), gs
// (ghostscript), tesseract. Images → PDF and image conversion use Qt only.
namespace DocTools {

QString mergePdfs(const QStringList &pdfs, const QString &out);
QString imagesToPdf(const QStringList &images, const QString &out); // one A4 page per image, fitted
QString compressPdf(const QString &pdf, const QString &out);          // ghostscript /ebook (150 dpi)
int pdfPageCount(const QString &pdf);                                 // -1 if unreadable
QString splitPdf(const QString &pdf, const QString &outDir, const QString &baseName, QStringList *written);
QString extractPages(const QString &pdf, const QList<int> &pages, const QString &out);
// OCR of an image, or of a PDF's pages (rendered at 300 dpi). Text in *text.
QString recognizeText(const QString &file, bool isPdf, QString *text);
// format: "png" | "jpg" | "webp"; maxWidth 0 keeps the size (never upscales).
QString convertImage(const QString &image, const QString &format, int maxWidth, const QString &out);

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
// Runs one tool on inputs, writing results into workDir. kind: merge | images-pdf | compress |
// split | extract (option: page ranges) | ocr-copy | ocr-note | convert (option: format) |
// resize (option: max width).
ToolResult runTool(const QString &kind, const QList<ToolInput> &inputs, const QString &option, const QString &workDir);

// "2-5, 8, 10-" → 1-based page numbers within 1..pageCount, in order, no duplicates.
// Empty list + error when the text is not a valid range.
QList<int> parsePageRanges(const QString &text, int pageCount, QString *error = nullptr);

}
