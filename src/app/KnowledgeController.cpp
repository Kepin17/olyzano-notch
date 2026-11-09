#include "KnowledgeController.h"

#include "NotchController.h"
#include "audio/AudioEngines.h"
#include "platform/NotchPaths.h"
#include "platform/ProcessRunner.h"
#include "services/Chunker.h"
#include "services/DocSummarize.h"
#include "services/DocSummaryModel.h"
#include "services/KnowledgeStore.h"
#include "services/PdfText.h"
#include "services/Retrieval.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QSettings>
#include <QUuid>
#include <QtConcurrent>

namespace {
constexpr qint64 IdleMs = 120000;
constexpr int MaxPages = 400;
constexpr qint64 MaxTextBytes = 256 * 1024;

QSettings settings() { return QSettings(NotchPaths::conf(QStringLiteral("knowledge")), QSettings::IniFormat); }

QString uniqueName() { return QStringLiteral("knowledge-") + QUuid::createUuid().toString(QUuid::Id128); }

bool isPdf(const QString &mime, const QString &name) { return mime == QLatin1String("application/pdf") || name.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive); }

bool isText(const QString &mime)
{
    static QHash<QString, bool> cache;
    if (!cache.contains(mime))
        cache.insert(mime, QMimeDatabase().mimeTypeForName(mime).inherits(QStringLiteral("text/plain")));
    return cache.value(mime);
}

// The passages of a PDF: its text page by page (one pdftotext for the whole file), headers and page numbers cleaned; scanned pages by OCR when asked.
QList<Knowledge::Chunk> pdfChunks(const QString &path, bool ocr, const std::atomic_bool &cancel)
{
    const auto res = ProcessRunner::run(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), path, QStringLiteral("-")}, 180000);
    QList<Pdf::Page> pages;
    if (res.ok) {
        const QStringList raw = QString::fromUtf8(res.out).split(u'\f');
        for (int i = 0; i < raw.size() && i < MaxPages; ++i) {
            if (i == raw.size() - 1 && raw[i].trimmed().isEmpty())
                break; // pdftotext ends the last page with a form feed
            pages.append({i + 1, raw[i], false});
        }
    }
    if (ocr && !pages.isEmpty()) {
        DocSummary::PdfPageSource src(path);
        for (Pdf::Page &p : pages) {
            if (cancel.load())
                return {};
            if (!Pdf::looksScanned(p.text))
                continue;
            QString err;
            const QString read = src.recognize(p.number, src.ocrLanguages(), &cancel, &err);
            if (!read.isEmpty()) {
                p.text = read;
                p.ocr = true;
            }
        }
    }
    QList<Knowledge::Chunk> out;
    for (const Pdf::Page &p : Pdf::clean(pages))
        out += Knowledge::chunkPage(p.number, p.text);
    return out;
}

// Words and, when the embedding model is there and the index has vectors, meaning: the two orders joined by rank. A passage that only the meaning
// found must be near enough to count (a sentence about nothing in the files finds nothing).
QList<KnowledgeStore::Hit> hybridSearch(KnowledgeStore &store, ModelStore *models, const std::function<int()> &threads, const QStringList &words, const QString &question, const KnowledgeStore::Filter &filter,
                                        int limit, const std::atomic_bool &cancel, bool *usedMeaning = nullptr)
{
    QList<KnowledgeStore::Hit> byWords = store.search(KnowledgeStore::matchQuery(words), filter, limit);
    if (usedMeaning)
        *usedMeaning = false;
    if (!Audio::LlamaSession::embedAvailable(models) || store.stats().vectors == 0 || question.isEmpty())
        return byWords;
    Audio::LlamaSession embedder(models, threads, Audio::LlamaSession::Mode::Embed);
    if (!embedder.start(&cancel).isEmpty())
        return byWords;
    const QList<QVector<float>> qv = embedder.embed({QStringLiteral("Instruct: Given a question or a few words, retrieve the passages of the person's files that answer it\nQuery: ") + question}, &cancel);
    embedder.stop();
    if (qv.isEmpty())
        return byWords;
    constexpr double Near = 0.30;
    QList<qint64> meaningIds;
    for (const auto &n : Knowledge::nearest(qv.first(), store.vectors(filter), limit))
        if (n.second >= Near)
            meaningIds << n.first;
    if (usedMeaning)
        *usedMeaning = !meaningIds.isEmpty();
    if (meaningIds.isEmpty())
        return byWords;
    QList<qint64> wordIds;
    QHash<qint64, KnowledgeStore::Hit> have;
    for (const KnowledgeStore::Hit &h : std::as_const(byWords)) {
        wordIds << h.chunkId;
        have.insert(h.chunkId, h);
    }
    QList<qint64> missing;
    for (const qint64 id : std::as_const(meaningIds))
        if (!have.contains(id))
            missing << id;
    for (const KnowledgeStore::Hit &h : store.chunks(missing))
        have.insert(h.chunkId, h);
    QList<KnowledgeStore::Hit> out;
    for (const qint64 id : Knowledge::fuse(wordIds, meaningIds))
        if (have.contains(id) && out.size() < limit)
            out.append(have.value(id));
    return out;
}

QString sizeText(qint64 bytes) { return QLocale().formattedDataSize(bytes); }

// The time the person said is a preference, not a wall: what was added then comes first and alone when there is any; when nothing was, everything
// that matched the words is kept (a bill that was added late is still the bill). Returns true when nothing was in the period.
bool preferPeriod(QList<KnowledgeStore::Hit> *hits, const QDateTime &from, const QDateTime &to)
{
    if (!from.isValid())
        return false;
    QList<KnowledgeStore::Hit> inside;
    for (const KnowledgeStore::Hit &h : std::as_const(*hits))
        if (h.addedAt >= from.toUTC() && h.addedAt <= to.toUTC())
            inside.append(h);
    if (inside.isEmpty())
        return !hits->isEmpty();
    *hits = inside;
    return false;
}
}

KnowledgeController::KnowledgeController(NotchController *notch, AudioController *audio, QObject *parent) : QObject(parent), m_notch(notch), m_audio(audio)
{
    m_indexPool.setMaxThreadCount(1);
    m_pool.setMaxThreadCount(1);
    {
        QSettings s = settings();
        m_enabled = s.value(QStringLiteral("index"), true).toBool();
        m_pdf = s.value(QStringLiteral("pdf"), true).toBool();
        m_ocr = s.value(QStringLiteral("ocr"), false).toBool();
        m_images = s.value(QStringLiteral("images"), false).toBool();
    }
    m_status = m_enabled ? tr("Waiting to look at your files…") : tr("Indexing is off");
    // looking for what changed: a little after start, then every few minutes, and when a recording is done
    m_scanTimer.setSingleShot(true);
    connect(&m_scanTimer, &QTimer::timeout, this, &KnowledgeController::scan);
    m_scanTimer.start(20000);
    auto *again = new QTimer(this);
    again->setInterval(3 * 60 * 1000);
    connect(again, &QTimer::timeout, this, [this] { if (!m_scanTimer.isActive()) m_scanTimer.start(1000); });
    again->start();
    connect(audio, &AudioController::finished, this, [this] { m_scanTimer.start(5000); });
    connect(audio, &AudioController::modelsChanged, this, [this] { if (m_enabled && meaningReady() && m_stats.value(QStringLiteral("vectors")).toInt() < m_stats.value(QStringLiteral("chunks")).toInt()) m_scanTimer.start(4000); }); // the embedding model arrived: the passages get their meaning
    // the index waits while a recording is processed or recorded: both want the processor
    m_pauseTimer.setInterval(1000);
    connect(&m_pauseTimer, &QTimer::timeout, this, [this] { m_pause = m_audio->processing(); });
    m_pauseTimer.start();
    m_idleTimer.setInterval(30000);
    connect(&m_idleTimer, &QTimer::timeout, this, &KnowledgeController::idleCheck);
    m_idleTimer.start();
    // what is in the index already
    (void)QtConcurrent::run([this] {
        KnowledgeStore store(uniqueName());
        if (!store.open(dbPath()).isEmpty())
            return;
        const KnowledgeStore::Stats st = store.stats();
        QMetaObject::invokeMethod(this, [this, st] {
            m_stats = QVariantMap{{QStringLiteral("docs"), st.docs}, {QStringLiteral("chunks"), st.chunks}, {QStringLiteral("vectors"), st.vectors}, {QStringLiteral("sizeText"), sizeText(st.bytes)}};
            emit changed();
        }, Qt::QueuedConnection);
    });
}

KnowledgeController::~KnowledgeController()
{
    m_cancelIndex = true;
    m_cancelAsk = true;
    m_indexPool.waitForDone(8000);
    m_pool.waitForDone(8000);
    if (m_llm)
        m_llm->stop();
}

QString KnowledgeController::dbPath() { return NotchPaths::dataDir(QStringLiteral("knowledge")) + QStringLiteral("/knowledge.db"); }

bool KnowledgeController::modelReady() const { return Audio::LlamaSession::available(m_audio->modelStore()); }

bool KnowledgeController::meaningReady() const { return Audio::LlamaSession::embedAvailable(m_audio->modelStore()); }

void KnowledgeController::setStatus(const QString &s)
{
    m_status = s;
    emit changed();
}

void KnowledgeController::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    { QSettings s = settings(); s.setValue(QStringLiteral("index"), on); }
    if (!on) {
        m_cancelIndex = true;
        m_status = tr("Indexing is off");
    } else {
        m_status = tr("Waiting to look at your files…");
        m_scanTimer.start(500);
    }
    emit changed();
}

void KnowledgeController::setIndexPdf(bool on) { if (on != m_pdf) { m_pdf = on; { QSettings s = settings(); s.setValue(QStringLiteral("pdf"), on); } emit changed(); m_scanTimer.start(500); } }
void KnowledgeController::setIndexOcr(bool on) { if (on != m_ocr) { m_ocr = on; { QSettings s = settings(); s.setValue(QStringLiteral("ocr"), on); } emit changed(); m_scanTimer.start(500); } }
void KnowledgeController::setIndexImages(bool on) { if (on != m_images) { m_images = on; { QSettings s = settings(); s.setValue(QStringLiteral("images"), on); } emit changed(); m_scanTimer.start(500); } }

void KnowledgeController::rescan() { m_scanTimer.start(100); }

void KnowledgeController::rebuild()
{
    m_cancelIndex = true;
    (void)QtConcurrent::run(&m_indexPool, [this] {
        KnowledgeStore store(uniqueName());
        if (store.open(dbPath()).isEmpty())
            store.clear();
        QMetaObject::invokeMethod(this, [this] { m_cancelIndex = false; m_scanTimer.start(200); }, Qt::QueuedConnection);
    });
}

void KnowledgeController::clearIndex()
{
    m_cancelIndex = true;
    (void)QtConcurrent::run(&m_indexPool, [this] {
        KnowledgeStore store(uniqueName());
        if (store.open(dbPath()).isEmpty())
            store.clear();
        const KnowledgeStore::Stats st = store.stats();
        QMetaObject::invokeMethod(this, [this, st] {
            m_cancelIndex = false;
            m_stats = QVariantMap{{QStringLiteral("docs"), 0}, {QStringLiteral("chunks"), 0}, {QStringLiteral("vectors"), 0}, {QStringLiteral("sizeText"), sizeText(st.bytes)}};
            m_status = tr("The index is empty. It is made again when you rebuild it.");
            m_enabled = false;
            { QSettings s = settings(); s.setValue(QStringLiteral("index"), false); }
            emit changed();
        }, Qt::QueuedConnection);
    });
}

// ---- keeping the index ----------------------------------------------------------------------------------

void KnowledgeController::scan()
{
    if (!m_enabled || m_indexing)
        return;
    if (m_audio->processing()) { // the processor is busy: try again soon
        m_scanTimer.start(30000);
        return;
    }
    m_indexing = true;
    m_cancelIndex = false;
    m_status = tr("Looking at your files…");
    emit changed();
    const QVariantList items = m_notch->knowledgeItems(), meetings = m_audio->knowledgeMeetings();
    const bool pdf = m_pdf, ocr = m_ocr, images = m_images;
    (void)QtConcurrent::run(&m_indexPool, [this, items, meetings, pdf, ocr, images] { index(items, meetings, pdf, ocr, images); });
}

void KnowledgeController::index(const QVariantList &items, const QVariantList &meetings, bool pdf, bool ocr, bool images)
{
    Q_UNUSED(images)
    const auto finish = [this](const QString &status, const KnowledgeStore::Stats &st) {
        QMetaObject::invokeMethod(this, [this, status, st] {
            m_indexing = false;
            m_status = status;
            m_stats = QVariantMap{{QStringLiteral("docs"), st.docs}, {QStringLiteral("chunks"), st.chunks}, {QStringLiteral("vectors"), st.vectors}, {QStringLiteral("sizeText"), sizeText(st.bytes)}};
            emit changed();
        }, Qt::QueuedConnection);
    };
    KnowledgeStore store(uniqueName());
    if (const QString err = store.open(dbPath()); !err.isEmpty()) {
        finish(tr("The index could not be opened: %1").arg(err), {});
        return;
    }
    const auto waitIfPaused = [&] {
        while (m_pause.load() && !m_cancelIndex.load())
            QThread::msleep(500);
    };
    const QString opts = QStringLiteral("%1%2").arg(pdf ? 'p' : '-').arg(ocr ? 'o' : '-');
    // what is no longer there goes
    QSet<QString> shelfIds, meetingIds;
    for (const QVariant &v : items)
        shelfIds.insert(v.toMap().value(QStringLiteral("id")).toString());
    for (const QVariant &v : meetings)
        meetingIds.insert(QString::number(v.toMap().value(QStringLiteral("id")).toLongLong()));
    for (const QString &ref : store.refs(QStringLiteral("shelf")))
        if (!shelfIds.contains(ref))
            store.removeDoc(QStringLiteral("shelf"), ref);
    for (const QString &ref : store.refs(QStringLiteral("meeting")))
        if (!meetingIds.contains(ref))
            store.removeDoc(QStringLiteral("meeting"), ref);

    int done = 0, changed = 0;
    const int total = int(items.size() + meetings.size());
    const auto progress = [&] {
        QMetaObject::invokeMethod(this, [this, done, total] { setStatus(tr("Indexing %1 of %2…").arg(done).arg(total)); }, Qt::QueuedConnection);
    };
    for (const QVariant &v : items) {
        if (m_cancelIndex.load())
            break;
        waitIfPaused();
        const QVariantMap it = v.toMap();
        const QString id = it.value(QStringLiteral("id")).toString(), name = it.value(QStringLiteral("name")).toString(), mime = it.value(QStringLiteral("mime")).toString();
        const bool pdfFile = isPdf(mime, name);
        ++done;
        if (!(pdfFile ? pdf : isText(mime)))
            continue;
        const QString fp = QStringLiteral("%1|%2|%3").arg(it.value(QStringLiteral("size")).toLongLong()).arg(it.value(QStringLiteral("mtime")).toLongLong()).arg(pdfFile ? opts : QStringLiteral("t"));
        if (store.fingerprint(QStringLiteral("shelf"), id) == fp)
            continue;
        QList<Knowledge::Chunk> chunks;
        if (pdfFile) {
            chunks = pdfChunks(it.value(QStringLiteral("path")).toString(), ocr, m_cancelIndex);
        } else {
            QFile f(it.value(QStringLiteral("path")).toString());
            if (f.open(QIODevice::ReadOnly))
                chunks = Knowledge::chunkPage(0, QString::fromUtf8(f.read(MaxTextBytes)));
        }
        if (m_cancelIndex.load())
            break;
        store.setDoc(QStringLiteral("shelf"), id, name, QDateTime::fromSecsSinceEpoch(it.value(QStringLiteral("added")).toLongLong(), Qt::UTC), fp, chunks);
        ++changed;
        if (changed % 3 == 0)
            progress();
    }
    for (const QVariant &v : meetings) {
        if (m_cancelIndex.load())
            break;
        waitIfPaused();
        const QVariantMap m = v.toMap();
        const qlonglong id = m.value(QStringLiteral("id")).toLongLong();
        const QString ref = QString::number(id), fp = m.value(QStringLiteral("fingerprint")).toString();
        ++done;
        if (store.fingerprint(QStringLiteral("meeting"), ref) == fp)
            continue;
        QList<Knowledge::Utterance> lines; // the transcript lives with the audio database on the interface thread
        QMetaObject::invokeMethod(this, [&] { lines = m_audio->transcriptLines(id); }, Qt::BlockingQueuedConnection);
        store.setDoc(QStringLiteral("meeting"), ref, m.value(QStringLiteral("title")).toString(), QDateTime::fromSecsSinceEpoch(m.value(QStringLiteral("created")).toLongLong(), Qt::UTC), fp,
                     Knowledge::chunkTranscript(lines));
        ++changed;
    }
    // the meaning of each new passage, when the embedding model is there
    ModelStore *models = m_audio->modelStore();
    if (!m_cancelIndex.load() && Audio::LlamaSession::embedAvailable(models) && !store.chunksWithoutVector(1).isEmpty()) {
        Audio::LlamaSession embedder(models, [this] { return m_audio->threads(); }, Audio::LlamaSession::Mode::Embed);
        if (embedder.start(&m_cancelIndex).isEmpty()) {
            const int before = store.stats().vectors;
            const int target = store.stats().chunks;
            for (;;) {
                if (m_cancelIndex.load())
                    break;
                waitIfPaused();
                const QList<qint64> ids = store.chunksWithoutVector(16);
                if (ids.isEmpty())
                    break;
                QHash<qint64, QString> text;
                for (const KnowledgeStore::Hit &h : store.chunks(ids))
                    text.insert(h.chunkId, h.text);
                QStringList batch;
                for (const qint64 id : ids)
                    batch << text.value(id);
                const QList<QVector<float>> vecs = embedder.embed(batch, &m_cancelIndex);
                if (vecs.size() != ids.size())
                    break;
                for (int i = 0; i < ids.size(); ++i)
                    store.setVector(ids[i], Knowledge::toF16(vecs[i]));
                const int have = store.stats().vectors;
                QMetaObject::invokeMethod(this, [this, have, target, before] { Q_UNUSED(before) setStatus(tr("Adding meaning to the passages: %1 of %2…").arg(have).arg(target)); }, Qt::QueuedConnection);
            }
        }
        embedder.stop();
    }
    const KnowledgeStore::Stats st = store.stats();
    if (m_cancelIndex.load()) {
        finish(m_enabled ? tr("Stopped") : tr("Indexing is off"), st);
        return;
    }
    finish(tr("Up to date: %1 documents, %2 passages").arg(st.docs).arg(st.chunks), st);
}

// ---- the language model, kept warm -----------------------------------------------------------------------

Audio::ChatFn KnowledgeController::chatFn(int maxTokens)
{
    if (!m_llm)
        m_llm = std::make_unique<Audio::LlamaSession>(m_audio->modelStore(), [this] { return m_audio->threads(); });
    if (!m_llmTried) {
        m_llmTried = true;
        m_llmError = Audio::LlamaSession::available(m_audio->modelStore()) ? m_llm->start(&m_cancelAsk) : QStringLiteral("not installed");
    }
    if (!m_llmError.isEmpty())
        return {};
    m_lastUse = QDateTime::currentMSecsSinceEpoch();
    return [this, maxTokens](const QString &s, const QString &u, const QJsonObject &sc) {
        m_lastUse = QDateTime::currentMSecsSinceEpoch();
        return m_llm->chat(s, u, sc, &m_cancelAsk, maxTokens);
    };
}

void KnowledgeController::idleCheck()
{
    if (m_jobRunning.load() || !m_llm || !m_llmTried || QDateTime::currentMSecsSinceEpoch() - m_lastUse.load() < IdleMs)
        return;
    (void)QtConcurrent::run(&m_pool, [this] { // on the thread that uses it
        if (m_llm && QDateTime::currentMSecsSinceEpoch() - m_lastUse.load() >= IdleMs) {
            m_llm->stop();
            m_llm.reset();
            m_llmTried = false;
        }
    });
}

// ---- smart search -------------------------------------------------------------------------------------------

QVariantList KnowledgeController::chips(const Audio::Ask::Query &q) const
{
    QVariantList out;
    if (q.from.isValid())
        out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("period")}, {QStringLiteral("label"), q.from.date() == q.to.date() ? QLocale().toString(q.from.date(), QLocale::ShortFormat)
                                                                                                        : QLocale().toString(q.from.date(), QLocale::ShortFormat) + QStringLiteral(" – ") + QLocale().toString(q.to.date(), QLocale::ShortFormat)}});
    for (const QString &w : q.keywords)
        out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("word")}, {QStringLiteral("label"), w}});
    for (const QString &w : q.synonyms)
        out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("synonym")}, {QStringLiteral("label"), w}});
    return out;
}

void KnowledgeController::smartSearch(const QString &sentence)
{
    const QString text = sentence.simplified();
    if (text.isEmpty() || m_smartBusy)
        return;
    m_smartBusy = true;
    m_smartSentence = text;
    m_smart = QVariantMap{{QStringLiteral("active"), true}, {QStringLiteral("query"), text}, {QStringLiteral("chips"), QVariantList()}, {QStringLiteral("count"), 0}};
    emit changed();
    m_cancelAsk = false;
    (void)QtConcurrent::run(&m_pool, [this, text] {
        m_jobRunning = true;
        const QDateTime now = QDateTime::currentDateTime();
        const Audio::ChatFn chat = chatFn(300);
        const Audio::Ask::Query q = Audio::Ask::understand(chat, text, now);
        m_jobRunning = false;
        QMetaObject::invokeMethod(this, [this, q, text, plain = !chat] { runSmart(q, text, plain, true); }, Qt::QueuedConnection);
    });
}

void KnowledgeController::smartDrop(int chip)
{
    Audio::Ask::Query q = m_smartQuery;
    int i = 0;
    if (q.from.isValid()) {
        if (chip == i++) {
            q.from = q.to = QDateTime();
            q.period.clear();
            runSmart(q, m_smartSentence, m_smartPlain, false);
            return;
        }
    }
    for (int k = 0; k < q.keywords.size(); ++k)
        if (chip == i++) {
            q.keywords.removeAt(k);
            runSmart(q, m_smartSentence, m_smartPlain, false);
            return;
        }
    for (int k = 0; k < q.synonyms.size(); ++k)
        if (chip == i++) {
            q.synonyms.removeAt(k);
            runSmart(q, m_smartSentence, m_smartPlain, false);
            return;
        }
}

void KnowledgeController::runSmart(const Audio::Ask::Query &query, const QString &sentence, bool plain, bool understood)
{
    Q_UNUSED(understood)
    m_smartQuery = query;
    m_smartPlain = plain;
    m_smartBusy = true;
    emit changed();
    ModelStore *models = m_audio->modelStore();
    const std::function<int()> threads = [this] { return m_audio->threads(); };
    (void)QtConcurrent::run(&m_pool, [this, query, sentence, plain, models, threads] {
        KnowledgeStore store(uniqueName());
        QList<KnowledgeStore::Hit> hits;
        QString error;
        if (const QString err = store.open(dbPath()); !err.isEmpty()) {
            error = tr("The index could not be opened: %1").arg(err);
        } else {
            KnowledgeStore::Filter f;
            f.kinds = {QStringLiteral("shelf")};
            if (query.words().isEmpty())
                error = tr("Type a few words to look for.");
            else
                hits = hybridSearch(store, models, threads, query.words(), sentence, f, 200, m_cancelAsk);
        }
        const bool outside = preferPeriod(&hits, query.from, query.to);
        const QStringList ids = Knowledge::docRefsInOrder(hits);
        QMetaObject::invokeMethod(this, [this, query, sentence, plain, ids, error, outside] {
            m_smartBusy = false;
            QString note;
            if (outside)
                note = tr("Nothing was added in that time, so everything that matches the words is shown.");
            if (plain)
                note += (note.isEmpty() ? QString() : QStringLiteral(" ")) + tr("Understood without the language model: only the words and the time in your sentence were used.");
            m_smart = QVariantMap{{QStringLiteral("active"), error.isEmpty()}, {QStringLiteral("query"), sentence}, {QStringLiteral("chips"), chips(query)}, {QStringLiteral("count"), ids.size()},
                                  {QStringLiteral("error"), error}, {QStringLiteral("note"), note}};
            if (error.isEmpty())
                m_notch->showOnly(ids);
            emit changed();
        }, Qt::QueuedConnection);
    });
}

void KnowledgeController::clearSmart()
{
    m_smart.clear();
    m_notch->showAll();
    emit changed();
}

// ---- asking ---------------------------------------------------------------------------------------------------

void KnowledgeController::cancelAsk() { m_cancelAsk = true; }

void KnowledgeController::clearAnswer()
{
    m_answer.clear();
    m_askError.clear();
    emit changed();
}

void KnowledgeController::ask(const QString &questionRaw, const QVariantMap &scope)
{
    const QString question = questionRaw.simplified();
    if (question.isEmpty() || m_asking)
        return;
    if (!modelReady()) {
        m_askError = tr("Download the language model first: Settings → AI.");
        emit changed();
        return;
    }
    m_asking = true;
    m_cancelAsk = false;
    m_askError.clear();
    m_stage = QStringLiteral("understanding");
    emit changed();
    const QString kind = scope.value(QStringLiteral("kind"), QStringLiteral("all")).toString(), ref = scope.value(QStringLiteral("ref")).toString();
    (void)QtConcurrent::run(&m_pool, [this, question, kind, ref] {
        m_jobRunning = true;
        QElapsedTimer clock;
        clock.start();
        const auto stage = [this](const QString &s) { QMetaObject::invokeMethod(this, [this, s] { m_stage = s; emit changed(); }, Qt::QueuedConnection); };
        const auto fail = [this](const QString &why) {
            QMetaObject::invokeMethod(this, [this, why] {
                m_asking = false;
                m_stage.clear();
                m_askError = why;
                emit changed();
            }, Qt::QueuedConnection);
        };
        const QDateTime now = QDateTime::currentDateTime();
        const Audio::ChatFn understanding = chatFn(300);
        if (!understanding) {
            m_jobRunning = false;
            fail(m_llmError.isEmpty() || m_llmError == QLatin1String("not installed") ? tr("The language model is not installed.") : m_llmError);
            return;
        }
        Audio::Ask::Query q = Audio::Ask::understand(understanding, question, now);
        if (m_cancelAsk) { m_jobRunning = false; fail(QString()); return; }
        stage(QStringLiteral("searching"));
        KnowledgeStore store(uniqueName());
        if (const QString err = store.open(dbPath()); !err.isEmpty()) {
            m_jobRunning = false;
            fail(tr("The index could not be opened: %1").arg(err));
            return;
        }
        KnowledgeStore::Filter f;
        QString scopeText = tr("everything");
        if (kind == QLatin1String("file") || kind == QLatin1String("recording")) {
            f.kinds = {kind == QLatin1String("file") ? QStringLiteral("shelf") : QStringLiteral("meeting")};
            f.refs = {ref};
            scopeText = kind == QLatin1String("file") ? tr("this file") : tr("this recording");
        } else {
            if (kind == QLatin1String("files")) { f.kinds = {QStringLiteral("shelf")}; scopeText = tr("your files"); }
            else if (kind == QLatin1String("recordings")) { f.kinds = {QStringLiteral("meeting")}; scopeText = tr("your recordings"); }
            else f.kinds = q.kinds;
        }
        bool meaning = false;
        QList<KnowledgeStore::Hit> ranked = hybridSearch(store, m_audio->modelStore(), [this] { return m_audio->threads(); }, q.words(), question, f, 80, m_cancelAsk, &meaning);
        if (f.refs.isEmpty())
            preferPeriod(&ranked, q.from, q.to);
        if (qEnvironmentVariableIsSet("NOTCH_ASK_DEBUG"))
            qWarning().noquote() << "ASK-DEBUG words" << q.keywords << q.synonyms << "period" << q.period << "hits" << ranked.size() << "kinds" << f.kinds << "meaning" << meaning;
        if (f.refs.size() == 1 && ranked.size() < 4) { // a question about one document that no word finds ("what is this about?"): the beginning of it
            const QList<KnowledgeStore::Hit> head = store.docChunks(f.kinds.value(0), f.refs.value(0), 8);
            QSet<qint64> have;
            for (const KnowledgeStore::Hit &h : std::as_const(ranked))
                have.insert(h.chunkId);
            for (const KnowledgeStore::Hit &h : head)
                if (!have.contains(h.chunkId) && ranked.size() < 8)
                    ranked.append(h);
        }
        if (m_cancelAsk) { m_jobRunning = false; fail(QString()); return; }
        const QList<KnowledgeStore::Hit> picked = Knowledge::pickSources(ranked);
        QList<Audio::Ask::Source> sources;
        for (int i = 0; i < picked.size(); ++i)
            sources.append({i + 1, picked[i]});
        stage(QStringLiteral("reading"));
        Audio::Ask::Answer a;
        if (!sources.isEmpty()) {
            const Audio::ChatFn base = chatFn(900);
            QString raw;
            const Audio::ChatFn spy = [&](const QString &s, const QString &u, const QJsonObject &sc) { raw = base(s, u, sc); return raw; };
            a = Audio::Ask::answer(spy, question, sources);
            if (qEnvironmentVariableIsSet("NOTCH_ASK_DEBUG"))
                qWarning().noquote() << "ASK-DEBUG sources" << sources.size() << "raw:" << raw.left(900);
        }
        else
            a.notFound = true;
        m_jobRunning = false;
        if (m_cancelAsk) { fail(QString()); return; }
        QVariantList shown;
        QSet<int> cited;
        for (const Audio::Ask::Citation &c : a.citations)
            cited.insert(c.n);
        for (const Audio::Ask::Source &s : std::as_const(sources)) {
            const bool isCited = cited.contains(s.n);
            if (!isCited && a.supported())
                continue; // an answer shows what it stands on; "not found" shows the closest passages
            QString quote;
            for (const Audio::Ask::Citation &c : a.citations)
                if (c.n == s.n)
                    quote = c.quote;
            const bool meeting = s.hit.kind == QLatin1String("meeting");
            shown.append(QVariantMap{{QStringLiteral("n"), s.n}, {QStringLiteral("name"), s.hit.name}, {QStringLiteral("kind"), meeting ? QStringLiteral("recording") : QStringLiteral("file")},
                                     {QStringLiteral("ref"), s.hit.ref}, {QStringLiteral("page"), s.hit.page}, {QStringLiteral("startSec"), s.hit.startSec},
                                     {QStringLiteral("where"), meeting ? Audio::clock(s.hit.startSec >= 0 ? s.hit.startSec : 0) : s.hit.page > 0 ? tr("page %1").arg(s.hit.page) : QString()},
                                     {QStringLiteral("quote"), quote}, {QStringLiteral("snippet"), s.hit.text.left(300)}, {QStringLiteral("cited"), isCited}});
            if (!a.supported() && shown.size() >= 3)
                break;
        }
        const QVariantMap result{{QStringLiteral("question"), question}, {QStringLiteral("text"), a.supported() ? a.text : QString()}, {QStringLiteral("notFound"), !a.supported()}, {QStringLiteral("figuresUnchecked"), a.figuresUnchecked},
                                 {QStringLiteral("sources"), shown}, {QStringLiteral("scopeText"), scopeText}, {QStringLiteral("seconds"), double(clock.elapsed()) / 1000.0},
                                 {QStringLiteral("chips"), chips(q)}};
        QMetaObject::invokeMethod(this, [this, result] {
            m_asking = false;
            m_stage.clear();
            m_answer = result;
            emit changed();
        }, Qt::QueuedConnection);
    });
}
