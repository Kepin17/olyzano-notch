#include "DocSummaryController.h"

#include "app/NotchController.h"
#include "platform/NotchPaths.h"
#include "services/DocSummarize.h"
#include "services/LanguageSettings.h"
#include "services/DocSummaryPdf.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QFileInfo>
#include <QLocale>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QTextDocument>
#include <QThread>

namespace {
QString historyPath() { return NotchPaths::dataDir(QStringLiteral("docsummary")) + QStringLiteral("/history.json"); }
}

DocSummaryController::DocSummaryController(NotchController *notch, AudioController *audio, QObject *parent) : QObject(parent), m_notch(notch), m_audio(audio)
{
    m_pool.setMaxThreadCount(1);
    QFile f(historyPath());
    if (f.open(QIODevice::ReadOnly))
        for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array())
            m_history.append(v.toObject().toVariantMap());
}

void DocSummaryController::saveHistory() const
{
    QDir().mkpath(QFileInfo(historyPath()).absolutePath());
    QSaveFile f(historyPath());
    if (f.open(QIODevice::WriteOnly)) {
        QJsonArray a;
        for (const QVariant &v : m_history)
            a.append(QJsonObject::fromVariantMap(v.toMap()));
        f.write(QJsonDocument(a).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

// what was made, with whether its files are still on the shelf and the time as a person reads it
QVariantList DocSummaryController::history() const
{
    QVariantList out;
    for (const QVariant &v : m_history) {
        QVariantMap e = v.toMap();
        const QString pdf = e.value(QStringLiteral("pdfId")).toString(), md = e.value(QStringLiteral("mdId")).toString();
        e.insert(QStringLiteral("pdfThere"), !pdf.isEmpty() && m_notch->documentInfo(pdf).value(QStringLiteral("ok")).toBool());
        e.insert(QStringLiteral("mdThere"), !md.isEmpty() && m_notch->documentInfo(md).value(QStringLiteral("ok")).toBool());
        e.insert(QStringLiteral("whenText"), QLocale().toString(QDateTime::fromString(e.value(QStringLiteral("when")).toString(), Qt::ISODate).toLocalTime(), QLocale::ShortFormat));
        out.append(e);
    }
    return out;
}

void DocSummaryController::removeHistory(int index)
{
    if (index < 0 || index >= m_history.size())
        return;
    m_history.removeAt(index);
    saveHistory();
    emit changed();
}

DocSummaryController::~DocSummaryController()
{
    m_cancel = true;
    m_pool.waitForDone(15000);
}

QString DocSummaryController::stageText() const
{
    if (m_stage == QLatin1String("pages")) return tr("Reading the pages");
    if (m_stage == QLatin1String("ocr")) return tr("Reading pages that are pictures");
    if (m_stage == QLatin1String("read")) return tr("Writing the notes");
    if (m_stage == QLatin1String("write")) return tr("Putting the notes together");
    return {};
}

QVariantMap DocSummaryController::inspect(const QString &itemId)
{
    const QVariantMap info = m_notch->documentInfo(itemId);
    if (!info.value(QStringLiteral("ok")).toBool())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("warning"), tr("This file is not available.")}};
    return describe(info.value(QStringLiteral("path")).toString(), info.value(QStringLiteral("name")).toString());
}

QVariantMap DocSummaryController::inspectPath(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (!QFileInfo(path).isFile() || !path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("warning"), tr("Choose a PDF file.")}};
    return describe(path, QFileInfo(path).fileName());
}

QVariantMap DocSummaryController::describe(const QString &path, const QString &name)
{
    DocSummary::PdfPageSource src(path);
    const int pages = src.pageCount();
    if (pages <= 0)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("warning"), tr("The PDF could not be read.")}};
    // a handful of pages, spread over the document: how many of them are pictures
    int sampled = 0, scanned = 0;
    const int want = qMin(pages, 6);
    for (int i = 0; i < want; ++i) {
        const int page = 1 + (pages - 1) * i / qMax(1, want - 1);
        ++sampled;
        if (Pdf::looksScanned(src.textLayer(page)))
            ++scanned;
    }
    const double pictureShare = sampled > 0 ? double(scanned) / sampled : 0;
    const QString ocrLanguages = src.ocrLanguages();
    const bool ocrInstalled = !DocSummary::PdfPageSource::installedOcrLanguages().isEmpty();
    const int shown = qMin(pages, 150);
    // seconds: the model takes about a minute a page on a CPU; a picture takes 3 s more to read
    const double seconds = shown * (50.0 + 3.0 * pictureShare) + 60; // measured: 2 pages in 3 minutes on this CPU (a minute of that for the front); a picture takes 3 s more
    QStringList warnings;
    if (pages > 150)
        warnings << tr("%1 pages: up to 150 are summarized at once. Choose a range of pages.").arg(pages);
    if (pictureShare > 0.4 && !ocrInstalled)
        warnings << tr("Most pages are pictures and tesseract (OCR) is not installed.");
    else if (pictureShare > 0.4 && !ocrLanguages.contains(QLatin1String("ind")))
        warnings << tr("Most pages are pictures. For Indonesian text install tesseract-ocr-ind (it is read with English only now).");
    return {{QStringLiteral("ok"), true}, {QStringLiteral("name"), name}, {QStringLiteral("pages"), pages},
            {QStringLiteral("pictureShare"), pictureShare}, {QStringLiteral("ocrLanguages"), ocrLanguages}, {QStringLiteral("ocrInstalled"), ocrInstalled},
            {QStringLiteral("estimateMinutes"), int(std::ceil(seconds / 60.0))}, {QStringLiteral("warning"), warnings.join(QStringLiteral(" "))}};
}

void DocSummaryController::clearError()
{
    m_error.clear();
    emit changed();
}

void DocSummaryController::cancel()
{
    m_cancel = true;
}

void DocSummaryController::start(const QString &itemId, const QVariantMap &options)
{
    const QVariantMap info = m_notch->documentInfo(itemId);
    if (!info.value(QStringLiteral("ok")).toBool()) {
        m_error = tr("This file is not available.");
        emit changed();
        return;
    }
    begin(info.value(QStringLiteral("path")).toString(), info.value(QStringLiteral("name")).toString(), info.value(QStringLiteral("base")).toString(), options);
}

void DocSummaryController::startPath(const QString &pathOrUrl, const QVariantMap &options)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    const QFileInfo fi(path);
    if (!fi.isFile()) {
        m_error = tr("This file is not available.");
        emit changed();
        return;
    }
    begin(path, fi.fileName(), fi.completeBaseName(), options);
}

void DocSummaryController::begin(const QString &path, const QString &name, const QString &base, const QVariantMap &options)
{
    if (m_busy)
        return;
    if (!Audio::LlamaSession::available(m_audio->modelStore())) {
        m_error = tr("The notes need the language model and its program: download them first.");
        emit changed();
        return;
    }
    DocSummary::Options opt;
    opt.style = DocSummary::styleFrom(options.value(QStringLiteral("style")).toString());
    opt.length = DocSummary::lengthFrom(options.value(QStringLiteral("length")).toString());
    opt.language = LanguageSettings::valid(options.value(QStringLiteral("language"), QStringLiteral("auto")).toString());
    opt.enabled = LanguageSettings::enabled();
    opt.ocr = options.value(QStringLiteral("ocr"), QStringLiteral("auto")).toString();
    opt.firstPage = options.value(QStringLiteral("firstPage"), 1).toInt();
    opt.lastPage = options.value(QStringLiteral("lastPage"), 0).toInt();
    const QFileInfo fi(path);
    const QString cacheDir = NotchPaths::dataDir(QStringLiteral("docsummary")) + u'/' + QString::fromLatin1(QCryptographicHash::hash((path + QString::number(fi.size()) + QString::number(fi.lastModified().toMSecsSinceEpoch())).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));

    m_busy = true;
    m_cancel = false;
    m_error.clear();
    m_warnings.clear();
    m_stage = QStringLiteral("pages");
    m_progress = 0;
    m_fileName = name;
    emit changed();

    ModelStore *models = m_audio->modelStore();
    const int threads = m_audio->threads();
    m_pool.start([this, path, base, opt, cacheDir, models, threads] {
        const auto progress = [this](const QString &stage, double f) {
            QMetaObject::invokeMethod(this, [this, stage, f] {
                if (!m_busy)
                    return;
                m_stage = stage;
                m_progress = f;
                emit changed();
            }, Qt::QueuedConnection);
        };
        DocSummary::PdfPageSource source(path, Lang::tessCode(opt.language));
        Audio::LlamaSession session(models, [threads] { return threads; });
        QString sessionError;
        bool started = false;
        // the model is started when the first piece needs it: reading the pages (and the pictures) comes first and takes no model
        const Audio::ChatFn chat = [&](const QString &system, const QString &user, const QJsonObject &schema) -> QString {
            if (!started) {
                started = true;
                sessionError = session.start(&m_cancel);
            }
            return sessionError.isEmpty() ? session.chat(system, user, schema, &m_cancel, 1800) : QString();
        };
        DocSummary::Result r = DocSummary::summarize(source, cacheDir, opt, chat, progress, &m_cancel);
        session.stop();
        QString error = r.error;
        if (!sessionError.isEmpty() && sessionError != QLatin1String("cancelled") && !error.isEmpty() && error != QLatin1String("cancelled"))
            error = sessionError;
        QString mdName, pdfName, html;
        QByteArray md;
        if (error.isEmpty()) {
            const QString lang = r.notes.language;
            const QString meta = QStringLiteral("%1 · %2 · %3").arg(QFileInfo(path).fileName(), lang == QLatin1String("en") ? QStringLiteral("%1 pages").arg(r.pages) : QStringLiteral("%1 halaman").arg(r.pages),
                                                                     QLocale().toString(QDate::currentDate(), QLocale::ShortFormat))
                                 + (r.ocrPages > 0 ? (lang == QLatin1String("en") ? QStringLiteral(" · %1 read by OCR").arg(r.ocrPages) : QStringLiteral(" · %1 dibaca dengan OCR").arg(r.ocrPages)) : QString());
            r.notes.title = base;
            md = DocSummary::toMarkdown(r.notes, opt.style, base, meta).toUtf8();
            mdName = base + QStringLiteral(" (summary).md");
            pdfName = base + QStringLiteral(" (summary).pdf");
            html = DocSummary::toHtml(r.notes, opt.style, base, meta);
        }
        const QVariantMap meta{{QStringLiteral("pages"), r.pages}, {QStringLiteral("style"), DocSummary::styleId(opt.style)}, {QStringLiteral("language"), r.notes.language}};
        QMetaObject::invokeMethod(this, [this, error, warnings = r.warnings, base, mdName, md, pdfName, html, cacheDir, meta] {
            done(error, warnings, base, mdName, md, pdfName, html, meta);
            if (error.isEmpty())
                QDir(cacheDir).removeRecursively(); // what was saved to go on after a stop is no use now
        }, Qt::QueuedConnection);
    });
}

void DocSummaryController::done(const QString &error, const QStringList &warnings, const QString &base, const QString &mdName, const QByteArray &md, const QString &pdfName, const QString &html, const QVariantMap &meta)
{
    m_busy = false;
    m_stage.clear();
    m_warnings = warnings;
    if (error == QLatin1String("cancelled")) {
        m_error.clear();
    } else if (!error.isEmpty()) {
        m_error = error;
    } else {
        const QString note = tr("Study notes of %1").arg(base);
        QString pdfId, mdId;
        const bool a = m_notch->addDocument(pdfName, QStringLiteral("application/pdf"), DocSummary::renderPdf(html, base), note, &pdfId); // text layout belongs to this thread
        const bool b = m_notch->addDocument(mdName, QStringLiteral("text/markdown"), md, note, &mdId);
        if (a || b) {
            m_result = base;
            m_history.prepend(QVariantMap{{QStringLiteral("title"), base}, {QStringLiteral("when"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {QStringLiteral("pages"), meta.value(QStringLiteral("pages"))},
                                          {QStringLiteral("style"), meta.value(QStringLiteral("style"))}, {QStringLiteral("language"), meta.value(QStringLiteral("language"))},
                                          {QStringLiteral("pdfId"), a ? pdfId : QString()}, {QStringLiteral("mdId"), b ? mdId : QString()}});
            while (m_history.size() > 50)
                m_history.removeLast();
            saveHistory();
            m_notch->announce(tr("Study notes ready"), pdfName, QStringLiteral("application-pdf"));
        } else {
            m_error = tr("The notes were made but could not be saved on the shelf.");
        }
    }
    m_progress = error.isEmpty() ? 1.0 : m_progress;
    emit changed();
    emit finished(error.isEmpty());
}
