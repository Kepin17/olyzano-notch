#include "TranslateController.h"

#include "NotchController.h"
#include "audio/AudioEngines.h"
#include "audio/Translate.h"
#include "audio/TranscriptExport.h"
#include "platform/NotchPaths.h"
#include "platform/ProcessRunner.h"
#include "services/DocSummaryPdf.h"
#include "services/FormModel.h"
#include "services/Languages.h"
#include "services/PdfText.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSaveFile>
#include <QUrl>
#include <QtConcurrent>

namespace {
constexpr int MaxPages = 150;
}

TranslateController::TranslateController(NotchController *notch, AudioController *audio, QObject *parent) : QObject(parent), m_notch(notch), m_audio(audio)
{
    m_pool.setMaxThreadCount(1);
}

TranslateController::~TranslateController()
{
    m_cancel = true;
    m_pool.waitForDone(8000);
}

void TranslateController::cancel() { m_cancel = true; }

void TranslateController::clearError()
{
    m_error.clear();
    emit changed();
}

QString TranslateController::cachePath(qlonglong id, const QString &code) const
{
    QDir().mkpath(NotchPaths::dataDir() + QStringLiteral("/translations"));
    return NotchPaths::dataDir() + QStringLiteral("/translations/%1-%2.json").arg(id).arg(QString(code).remove(QRegularExpression(QStringLiteral("[^A-Za-z-]"))));
}

QStringList TranslateController::cached(qlonglong id, const QString &code) const
{
    QFile f(cachePath(id, code));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QStringList sources;
    for (const Audio::Segment &s : m_audio->segmentsOf(id))
        sources << (Audio::Sound::isSound(s) ? QString() : s.text);
    if (o.value(QStringLiteral("fp")).toString() != Translate::fingerprint(sources))
        return {}; // the transcript was edited since
    QStringList out;
    for (const QJsonValue &v : o.value(QStringLiteral("items")).toArray())
        out << v.toString();
    return out.size() == sources.size() ? out : QStringList();
}

bool TranslateController::hasTranslation(qlonglong id, const QString &code) const { return !cached(id, code).isEmpty(); }

void TranslateController::end(const QString &error, const QStringList &warnings, const QString &status)
{
    m_busy = false;
    m_warnings = warnings;
    if (error == QLatin1String("cancelled")) {
        m_error.clear();
        m_status.clear();
    } else if (!error.isEmpty()) {
        m_error = error;
        m_status.clear();
    } else {
        m_status = status;
        m_progress = 1.0;
    }
    emit changed();
    emit finished(error.isEmpty());
}

void TranslateController::translateMeeting(qlonglong id, const QString &code)
{
    const Lang::Language *target = Lang::find(code);
    if (m_busy || !target || id <= 0)
        return;
    if (!Audio::LlamaSession::available(m_audio->modelStore())) {
        m_error = tr("Translation needs the language model: download it in Settings → AI.");
        emit changed();
        return;
    }
    const QList<Audio::Segment> segments = m_audio->segmentsOf(id);
    QStringList sources;
    for (const Audio::Segment &s : segments)
        sources << (Audio::Sound::isSound(s) ? QString() : s.text);
    if (std::none_of(sources.begin(), sources.end(), [](const QString &s) { return !s.simplified().isEmpty(); })) {
        m_error = tr("This recording has no transcript to translate.");
        emit changed();
        return;
    }
    m_busy = true;
    m_cancel = false;
    m_error.clear();
    m_warnings.clear();
    m_progress = 0;
    m_status = tr("Translating into %1…").arg(target->english);
    emit changed();
    ModelStore *store = m_audio->modelStore();
    const int threads = m_audio->threads();
    const Lang::Language lang = *target;
    const QString path = cachePath(id, code);
    (void)QtConcurrent::run(&m_pool, [this, store, threads, lang, sources, path] {
        QString error;
        Translate::Result result;
        Audio::LlamaSession session(store, [threads] { return threads; });
        error = session.start(&m_cancel);
        if (error.isEmpty()) {
            const Audio::ChatFn chat = [&](const QString &s, const QString &u, const QJsonObject &sc) { return session.chat(s, u, sc, &m_cancel, 1800); };
            result = Translate::translateAll(sources, lang, chat, [this](double p) { QMetaObject::invokeMethod(this, [this, p] { m_progress = p; emit changed(); }, Qt::QueuedConnection); }, &m_cancel);
            if (result.cancelled)
                error = QStringLiteral("cancelled");
        }
        session.stop();
        if (error.isEmpty()) {
            QJsonArray items;
            for (const QString &t : std::as_const(result.texts))
                items.append(t);
            QSaveFile f(path);
            if (f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(QJsonObject{{QStringLiteral("fp"), Translate::fingerprint(sources)}, {QStringLiteral("items"), items}}).toJson(QJsonDocument::Compact)) >= 0)
                f.commit();
        }
        QMetaObject::invokeMethod(this, [this, error, result, lang] {
            QStringList warnings;
            if (result.untranslated > 0)
                warnings << tr("%n line(s) could not be translated and stay as they were.", nullptr, result.untranslated);
            end(error, warnings, tr("Translated into %1.").arg(lang.english));
        }, Qt::QueuedConnection);
    });
}

QString TranslateController::suggestedName(qlonglong id, const QString &code, const QString &format) const
{
    QString title = Forms::sanitizeFileName(m_audio->meetingTitle(id));
    if (title.isEmpty())
        title = QStringLiteral("recording");
    const bool both = format.startsWith(QLatin1String("bilingual"));
    const Audio::Export::Format f = Audio::Export::formatOf(format.section(QLatin1Char('-'), -1));
    return QStringLiteral("%1.%2%3.%4").arg(title, code, both ? QStringLiteral(".2lang") : QString(), Audio::Export::extension(f));
}

QString TranslateController::exportMeeting(qlonglong id, const QString &code, const QString &format, const QString &target) const
{
    const QStringList tr = cached(id, code);
    if (tr.isEmpty())
        return this->tr("Translate the transcript first.");
    const QList<Audio::Segment> segments = m_audio->segmentsOf(id);
    Audio::Export::Options o;
    o.sounds = Audio::Export::Sounds::Marker;
    QString text;
    if (format == QLatin1String("bilingual-srt") || format == QLatin1String("bilingual-vtt"))
        text = Audio::Export::bilingualSubtitles(segments, tr, format == QLatin1String("bilingual-vtt"), o);
    else
        text = Audio::Export::render(Audio::Export::translated(segments, tr), Audio::Export::formatOf(format), o);
    if (text.trimmed().isEmpty())
        return this->tr("There is nothing to save");
    return m_audio->saveText(text, target);
}

void TranslateController::translatePdf(const QString &itemId, const QString &code)
{
    const QVariantMap info = m_notch->documentInfo(itemId);
    if (!info.value(QStringLiteral("ok")).toBool())
        return;
    beginPdf(info.value(QStringLiteral("path")).toString(), info.value(QStringLiteral("name")).toString(), code);
}

void TranslateController::translatePdfPath(const QString &pathOrUrl, const QString &code)
{
    const QString path = pathOrUrl.startsWith(QLatin1String("file:")) ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
    if (QFileInfo(path).isFile())
        beginPdf(path, QFileInfo(path).fileName(), code);
}

void TranslateController::beginPdf(const QString &file, const QString &name, const QString &code)
{
    const Lang::Language *target = Lang::find(code);
    if (m_busy || !target)
        return;
    if (!Audio::LlamaSession::available(m_audio->modelStore())) {
        m_error = tr("Translation needs the language model: download it in Settings → AI.");
        emit changed();
        return;
    }
    m_busy = true;
    m_cancel = false;
    m_error.clear();
    m_warnings.clear();
    m_progress = 0;
    m_status = tr("Reading %1…").arg(name);
    emit changed();
    ModelStore *store = m_audio->modelStore();
    const int threads = m_audio->threads();
    const Lang::Language lang = *target;
    (void)QtConcurrent::run(&m_pool, [this, file, name, store, threads, lang] {
        QString error;
        QStringList warnings;
        QString md, html;
        const QString base = QFileInfo(name).completeBaseName();
        const auto res = ProcessRunner::run(QStringLiteral("pdftotext"), {file, QStringLiteral("-")}, 180000);
        QStringList raw = res.ok ? QString::fromUtf8(res.out).split(QLatin1Char('\f')) : QStringList();
        if (!raw.isEmpty() && raw.last().trimmed().isEmpty())
            raw.removeLast();
        if (raw.isEmpty())
            error = tr("No text could be read from %1.").arg(name);
        if (error.isEmpty() && raw.size() > MaxPages) {
            warnings << tr("Only the first %1 pages are translated.").arg(MaxPages);
            raw = raw.mid(0, MaxPages);
        }
        if (error.isEmpty()) {
            QList<Pdf::Page> pages;
            for (int i = 0; i < raw.size(); ++i)
                pages.append({i + 1, raw[i], false});
            pages = Pdf::clean(pages); // the running headers and footers and the page numbers are not text to translate
            QStringList paragraphs; // all of them, with where each belongs
            QList<int> pageOf;
            for (const Pdf::Page &p : std::as_const(pages)) {
                if (Pdf::looksScanned(p.text)) {
                    warnings << tr("Page %1 is a picture: it is not translated (pictures are not read here).").arg(p.number);
                    continue;
                }
                for (const QString &par : Translate::splitParagraphs(p.text)) {
                    paragraphs << par;
                    pageOf << p.number;
                }
            }
            Audio::LlamaSession session(store, [threads] { return threads; });
            error = paragraphs.isEmpty() ? tr("There is no text to translate.") : session.start(&m_cancel);
            Translate::Result result;
            if (error.isEmpty()) {
                const Audio::ChatFn chat = [&](const QString &s, const QString &u, const QJsonObject &sc) { return session.chat(s, u, sc, &m_cancel, 1800); };
                result = Translate::translateAll(paragraphs, lang, chat, [this](double p) { QMetaObject::invokeMethod(this, [this, p] { m_progress = p; m_status = tr("Translating…"); emit changed(); }, Qt::QueuedConnection); }, &m_cancel);
                if (result.cancelled)
                    error = QStringLiteral("cancelled");
            }
            session.stop();
            if (error.isEmpty()) {
                if (result.untranslated > 0)
                    warnings << tr("%n paragraph(s) could not be translated and stay as they were.", nullptr, result.untranslated);
                md = QStringLiteral("# %1 (%2)\n\n").arg(base, lang.native);
                html = QStringLiteral("<html><body%1><h1>%2</h1>").arg(lang.rtl ? QStringLiteral(" dir=\"rtl\"") : QString(), base.toHtmlEscaped());
                int lastPage = -1;
                for (int i = 0; i < result.texts.size(); ++i) {
                    if (pageOf[i] != lastPage) {
                        lastPage = pageOf[i];
                        md += QStringLiteral("— %1 —\n\n").arg(tr("page %1").arg(lastPage));
                        html += QStringLiteral("<p style=\"color:#778;font-size:9pt\">— %1 —</p>").arg(tr("page %1").arg(lastPage).toHtmlEscaped());
                    }
                    md += result.texts[i] + QStringLiteral("\n\n");
                    html += QStringLiteral("<p>%1</p>").arg(result.texts[i].toHtmlEscaped());
                }
                html += QStringLiteral("</body></html>");
            }
        }
        QMetaObject::invokeMethod(this, [this, error, warnings, md, html, base, lang] {
            if (!error.isEmpty()) {
                end(error, warnings, QString());
                return;
            }
            const QString note = tr("Translation of %1 into %2").arg(base, lang.english);
            const QString stem = QStringLiteral("%1 (%2)").arg(Forms::sanitizeFileName(base), lang.code);
            const bool a = m_notch->addDocument(stem + QStringLiteral(".pdf"), QStringLiteral("application/pdf"), DocSummary::renderPdf(html, base), note); // text layout belongs to this thread
            const bool b = m_notch->addDocument(stem + QStringLiteral(".md"), QStringLiteral("text/markdown"), md.toUtf8(), note);
            if (a || b) {
                m_notch->announce(tr("Translation ready"), stem + QStringLiteral(".pdf"), QStringLiteral("application-pdf"));
                end({}, warnings, tr("%1 is on the shelf.").arg(stem + QStringLiteral(".pdf")));
            } else {
                end(tr("The translation was made but could not be saved on the shelf."), warnings, QString());
            }
        }, Qt::QueuedConnection);
    });
}
