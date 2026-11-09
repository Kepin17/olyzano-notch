#pragma once

#include "DocSummaryModel.h"
#include "audio/Summarize.h" // ChatFn

#include <QByteArray>
#include <QMap>
#include <QStringList>

#include <atomic>
#include <functional>

// A PDF into study notes: its words page by page (the text it has, and the pages that are pictures read by OCR), cleaned, cut into pieces, each
// piece read by the language model for what a student must know, what the pages support kept, and the front of the notes written from that.
// The model is behind `ChatFn` and the pages behind `IPageSource`, so all of it is tested with fakes. Slow work is saved in a folder as it is
// done, so a stopped job goes on from there.
namespace DocSummary {

class IPageSource {
public:
    virtual ~IPageSource() = default;
    virtual int pageCount() = 0;                    // -1 when the file cannot be read
    virtual QString textLayer(int page) = 0;        // the text the page has (may be empty)
    // The page read as a picture, in the best language there is; "" with *error when it could not be (no OCR program, ...)
    virtual QString recognize(int page, const QString &language, const std::atomic_bool *cancel, QString *error) = 0;
    virtual QString ocrLanguages() { return QStringLiteral("eng"); } // what recognize will use ("ind+eng"), for the notes to say
    virtual QMap<int, int> picturePages() { return {}; }               // pages that hold a figure -> how many (for the picture model)
    virtual QByteArray renderPage(int page, int dpi, QString *error) { Q_UNUSED(page) Q_UNUSED(dpi) Q_UNUSED(error) return {}; } // the page as PNG
};

struct Options {
    Style style = Style::Study;
    Length length = Length::Medium;
    QString language = QStringLiteral("auto"); // auto, or a code of Languages.h
    QStringList enabled = Lang::defaults();    // the languages in use: "auto" tells among these
    QString ocr = QStringLiteral("auto");      // auto (pages with no text) | always | never
    int firstPage = 1, lastPage = 0;           // 0 = to the end
    int maxPages = 150;
    int chunkChars = 6000;
    // The picture model, when the person asked for the figures to be described: it is shown each page that has a figure (up to maxFigurePages) and
    // gives back the text to add to the page ("" with *error when it could not); `doneFigures` is called when it is no longer needed.
    bool figures = false;
    int maxFigurePages = 20;
    std::function<QString(const QByteArray &png, QString *error)> describePage;
    std::function<void()> doneFigures;
};

struct Result {
    QString error; // "" when the notes are made; "cancelled" when stopped
    Notes notes;
    int pages = 0, ocrPages = 0, parts = 0, failedParts = 0, figurePages = 0;
    QStringList warnings;
};

// progress(stage, fraction of the whole job): stage is "pages" | "ocr" | "read" | "write"
using ProgressFn = std::function<void(const QString &stage, double fraction)>;

Result summarize(IPageSource &source, const QString &cacheDir, const Options &options, const Audio::ChatFn &chat, const ProgressFn &progress, const std::atomic_bool *cancel);

// The pages of a real PDF: pdftotext, and pdftoppm + tesseract for the pictures.
class PdfPageSource : public IPageSource {
public:
    // `firstTess`: the tesseract code of the language the person asked for, read first ("" = those in use)
    explicit PdfPageSource(const QString &pdf, const QString &firstTess = {}) : m_pdf(pdf), m_firstTess(firstTess) {}
    int pageCount() override;
    QString textLayer(int page) override;
    QString recognize(int page, const QString &language, const std::atomic_bool *cancel, QString *error) override;
    QString ocrLanguages() override;
    QMap<int, int> picturePages() override;
    QByteArray renderPage(int page, int dpi, QString *error) override;
    static QStringList installedOcrLanguages(); // from `tesseract --list-langs`

private:
    QString m_pdf, m_firstTess;
};

} // namespace DocSummary
