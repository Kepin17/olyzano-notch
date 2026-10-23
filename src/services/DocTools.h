#pragma once

#include <QList>
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

// "2-5, 8, 10-" → 1-based page numbers within 1..pageCount, in order, no duplicates.
// Empty list + error when the text is not a valid range.
QList<int> parsePageRanges(const QString &text, int pageCount, QString *error = nullptr);

}
