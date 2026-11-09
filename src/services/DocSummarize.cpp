#include "DocSummarize.h"

#include "DocTools.h"
#include "OcrPacks.h"
#include "platform/ProcessRunner.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>

namespace DocSummary {

namespace {
QJsonObject loadJson(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}
void saveJson(const QString &path, const QJsonObject &o)
{
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) >= 0)
        f.commit();
}
QString digest(const QString &text) { return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16)); }
}

Result summarize(IPageSource &source, const QString &cacheDir, const Options &opt, const Audio::ChatFn &chat, const ProgressFn &progress, const std::atomic_bool *cancel)
{
    Result r;
    const auto report = [&](const char *stage, double f) {
        if (progress)
            progress(QString::fromLatin1(stage), f);
    };
    const auto stopped = [&] { return cancel && cancel->load(); };
    const auto cancelled = [&] { r.error = QStringLiteral("cancelled"); return r; };
    QDir().mkpath(cacheDir);

    const int total = source.pageCount();
    if (total <= 0) {
        r.error = QObject::tr("The PDF could not be read (is it damaged or locked with a password?)");
        return r;
    }
    const int first = std::clamp(opt.firstPage, 1, total), last = opt.lastPage > 0 ? std::clamp(opt.lastPage, first, total) : total;
    if (last - first + 1 > opt.maxPages) {
        r.error = QObject::tr("The PDF has %1 pages in that range; up to %2 are summarized at once. Choose a range of pages.").arg(last - first + 1).arg(opt.maxPages);
        return r;
    }
    r.pages = last - first + 1;

    // ---- the words of each page: what it has, and the pictures read ----
    const QString pagesFile = cacheDir + QStringLiteral("/pages.json");
    QJsonObject cached = loadJson(pagesFile).value(QStringLiteral("pages")).toObject();
    QList<Pdf::Page> pages;
    int emptyPages = 0;
    for (int n = first; n <= last; ++n) {
        if (stopped())
            return cancelled();
        report("pages", 0.2 * double(n - first) / r.pages);
        Pdf::Page page;
        page.number = n;
        const QJsonObject c = cached.value(QString::number(n)).toObject();
        if (c.contains(QStringLiteral("t"))) {
            page.text = c.value(QStringLiteral("t")).toString();
            page.ocr = c.value(QStringLiteral("o")).toBool();
        } else {
            page.text = source.textLayer(n);
            const bool picture = Pdf::looksScanned(page.text);
            if (opt.ocr == QLatin1String("always") || (opt.ocr == QLatin1String("auto") && picture)) {
                QString err;
                const QString read = source.recognize(n, source.ocrLanguages(), cancel, &err);
                if (stopped())
                    return cancelled();
                if (!read.isEmpty()) {
                    page.text = read;
                    page.ocr = true;
                } else if (!err.isEmpty() && !r.warnings.contains(err)) {
                    r.warnings << err;
                }
                report("ocr", 0.2 * double(n - first + 1) / r.pages);
            }
            cached.insert(QString::number(n), QJsonObject{{QStringLiteral("t"), page.text}, {QStringLiteral("o"), page.ocr}});
            saveJson(pagesFile, QJsonObject{{QStringLiteral("pages"), cached}});
        }
        if (page.ocr)
            ++r.ocrPages;
        if (Pdf::looksScanned(page.text))
            ++emptyPages;
        pages << page;
    }
    pages = Pdf::clean(pages);
    int letterTotal = 0;
    QString everything;
    for (const Pdf::Page &p : std::as_const(pages)) {
        letterTotal += Pdf::letters(p.text);
        everything += p.text + u'\n';
    }
    if (letterTotal < 200) {
        r.error = emptyPages > 0 && opt.ocr == QLatin1String("never") ? QObject::tr("The PDF has no text: its pages are pictures. Turn on reading pages as pictures (OCR).")
                  : QObject::tr("No text could be read from this PDF.");
        if (!r.warnings.isEmpty())
            r.error += QStringLiteral(" ") + r.warnings.join(QStringLiteral(" "));
        return r;
    }
    if (emptyPages > 0)
        r.warnings << QObject::tr("%n page(s) had no text that could be read and are left out.", nullptr, emptyPages);
    if (r.ocrPages > 0 && source.ocrLanguages() == QLatin1String("eng") && opt.language != QLatin1String("en"))
        r.warnings << QObject::tr("Pages read as pictures were read with the English language pack only: add the language of the document in Settings → Languages.");

    QString lang = Lang::known(opt.language) ? opt.language : Pdf::detectLanguage(everything, opt.enabled);
    const QString asked = lang.isEmpty() ? QStringLiteral("auto") : lang;
    if (lang.isEmpty())
        lang = QStringLiteral("en");

    // ---- read piece by piece ----
    QHash<int, QString> byNumber;
    for (const Pdf::Page &p : std::as_const(pages))
        byNumber.insert(p.number, p.text);
    const QList<Pdf::Chunk> chunks = Pdf::chunkPages(pages, opt.chunkChars);
    r.parts = int(chunks.size());
    QList<Notes> parts;
    const QString system = mapPrompt(opt.style, opt.length, asked);
    for (int i = 0; i < chunks.size(); ++i) {
        if (stopped())
            return cancelled();
        report("read", 0.2 + 0.7 * double(i) / chunks.size());
        const QString key = cacheDir + QStringLiteral("/part-") + digest(styleId(opt.style) + lengthId(opt.length) + asked + chunks[i].text) + QStringLiteral(".json");
        QString answer = loadJson(key).value(QStringLiteral("answer")).toString();
        if (answer.isEmpty()) {
            const QString user = wrapDocument(chunks[i].text)
                                 + QStringLiteral("\n\nRead the document text above%1 and answer with the JSON object.")
                                       .arg(chunks.size() > 1 ? QStringLiteral(" (part %1 of %2, pages %3-%4)").arg(i + 1).arg(chunks.size()).arg(chunks[i].firstPage).arg(chunks[i].lastPage) : QString());
            answer = chat(system, user, mapSchema(opt.length));
            if (stopped())
                return cancelled();
            if (!answer.isEmpty())
                saveJson(key, QJsonObject{{QStringLiteral("answer"), answer}});
        }
        if (answer.isEmpty()) {
            ++r.failedParts;
            continue;
        }
        parts << validate(parsePiece(answer.toUtf8(), chunks[i].firstPage, chunks[i].lastPage), byNumber, chunks[i].firstPage, chunks[i].lastPage);
    }
    if (parts.isEmpty()) {
        r.error = QObject::tr("The language model did not answer");
        return r;
    }
    if (r.failedParts > 0)
        r.warnings << QObject::tr("%n part(s) of the document could not be read by the language model and are missing from the notes.", nullptr, r.failedParts);
    Notes notes = merge(parts);
    notes.language = lang;
    if (notes.points.isEmpty() && notes.terms.isEmpty() && notes.formulas.isEmpty()) { // answers that give nothing the pages support are no notes
        r.error = QObject::tr("The language model's answers could not be used (nothing in them matched the pages). Try again, or choose a shorter range of pages.");
        return r;
    }

    // ---- the front: overview, objectives, questions ----
    report("write", 0.92);
    if (stopped())
        return cancelled();
    if (!notes.isEmpty()) {
        const QString answer = chat(writePrompt(opt.style, opt.length, asked),
                                    wrapDocument(notesAsText(notes), QStringLiteral("notes")) + QStringLiteral("\n\nWrite the front of the study notes from the notes above and answer with the JSON object."),
                                    writeSchema());
        if (stopped())
            return cancelled();
        const Notes front = parseFront(answer.toUtf8());
        notes.overview = front.overview;
        notes.objectives = front.objectives;
        for (Question q : front.questions) {
            if (q.page < first || q.page > last)
                q.page = 0; // a page the document does not have is not shown
            notes.questions.append(q);
        }
        if (notes.overview.isEmpty()) {
            QStringList lead;
            for (const Section &s : std::as_const(notes.outline))
                if (!s.summary.isEmpty() && lead.size() < 4)
                    lead << s.summary;
            notes.overview = lead.join(u' ');
            r.warnings << QObject::tr("The overview could not be written; it is made of the summaries of the parts.");
        }
    }
    report("write", 1.0);
    r.notes = notes;
    return r;
}

// ---- the real pages -------------------------------------------------------------------------------------------

int PdfPageSource::pageCount() { return DocTools::pdfPageCount(m_pdf); }

QString PdfPageSource::textLayer(int page)
{
    const auto res = ProcessRunner::run(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), QStringLiteral("-f"), QString::number(page), QStringLiteral("-l"), QString::number(page), m_pdf, QStringLiteral("-")}, 60000);
    return res.ok ? QString::fromUtf8(res.out) : QString();
}

QStringList PdfPageSource::installedOcrLanguages() { return OcrPacks::systemLanguages(); }

// the languages the pages are read in: the one asked for (when the person chose one) and those in use that have a pack, English too
QString PdfPageSource::ocrLanguages() { return OcrPacks::current(m_firstTess).languages; }

QString PdfPageSource::recognize(int page, const QString &language, const std::atomic_bool *cancel, QString *error)
{
    Q_UNUSED(cancel)
    QTemporaryDir tmp;
    const QString prefix = tmp.filePath(QStringLiteral("page"));
    const auto render = ProcessRunner::run(QStringLiteral("pdftoppm"), {QStringLiteral("-r"), QStringLiteral("300"), QStringLiteral("-f"), QString::number(page), QStringLiteral("-l"), QString::number(page),
                                                                         QStringLiteral("-png"), QStringLiteral("-singlefile"), m_pdf, prefix}, 120000);
    if (!render.ok) {
        *error = render.error.isEmpty() ? QObject::tr("The page could not be drawn for reading") : render.error;
        return {};
    }
    const OcrPacks::Setup setup = OcrPacks::current(m_firstTess);
    QStringList args{prefix + QStringLiteral(".png"), QStringLiteral("stdout"), QStringLiteral("-l"), language.isEmpty() ? setup.languages : language};
    if (!setup.tessdataDir.isEmpty())
        args << QStringLiteral("--tessdata-dir") << setup.tessdataDir;
    const auto ocr = ProcessRunner::run(QStringLiteral("tesseract"), args, 180000);
    if (!ocr.ok) {
        *error = ocr.failedToStart ? QObject::tr("tesseract (OCR) is not installed, so pages that are pictures cannot be read.") : QObject::tr("A page could not be read: %1").arg(ocr.error);
        return {};
    }
    return QString::fromUtf8(ocr.out).trimmed();
}

} // namespace DocSummary
