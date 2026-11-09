#include "LiveSession.h"

#include "Hardware.h"
#include "LivePrompt.h"
#include "../services/LanguageSettings.h"
#include "../services/Languages.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QtConcurrent>

using Audio::Utterance;

// ---- the lines ---------------------------------------------------------------------------------------------

QVariant LiveModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_lines.size())
        return {};
    const Line &l = m_lines.at(index.row());
    switch (role) {
    case SideRole: return l.side;
    case StartRole: return Audio::clock(double(l.startMs) / 1000.0);
    case TextRole: return l.text;
    case TranslationRole: return l.translation;
    case LanguageRole: return l.language;
    case PartialRole: return l.partial;
    }
    return {};
}

qint64 LiveModel::add(const Line &line)
{
    int row = int(m_lines.size());
    while (row > 0 && m_lines.at(row - 1).startMs > line.startMs)
        --row;
    Line l = line;
    l.id = m_next++;
    beginInsertRows({}, row, row);
    m_lines.insert(row, l);
    endInsertRows();
    return l.id;
}

void LiveModel::setTranslation(qint64 id, const QString &t)
{
    for (int i = 0; i < m_lines.size(); ++i)
        if (m_lines[i].id == id) {
            m_lines[i].translation = t;
            emit dataChanged(index(i), index(i), {TranslationRole});
            return;
        }
}

void LiveModel::setPartial(int side, qint64 startMs, const QString &text, const QString &language)
{
    for (int i = 0; i < m_lines.size(); ++i)
        if (m_lines[i].partial && m_lines[i].side == side) {
            m_lines[i].text = text;
            m_lines[i].language = language;
            emit dataChanged(index(i), index(i), {TextRole, LanguageRole});
            return;
        }
    Line l;
    l.side = side;
    l.startMs = l.endMs = startMs;
    l.text = text;
    l.language = language;
    l.partial = true;
    add(l);
}

void LiveModel::clearPartial(int side, qint64 beforeMs)
{
    for (int i = 0; i < m_lines.size(); ++i)
        if (m_lines[i].partial && m_lines[i].side == side && m_lines[i].startMs < beforeMs) {
            beginRemoveRows({}, i, i);
            m_lines.removeAt(i);
            endRemoveRows();
            return;
        }
}

void LiveModel::clear()
{
    beginResetModel();
    m_lines.clear();
    endResetModel();
}

// ---- the session -------------------------------------------------------------------------------------------

LiveSession::LiveSession(ModelStore *store, std::function<int()> threads, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_threads(threads)
    , m_seg{Audio::LiveSegmenter(0), Audio::LiveSegmenter(1)}
    , m_asr(store, [threads] { return qBound(2, threads(), 6); }, [] { return LanguageSettings::whisperCodes(LanguageSettings::enabled()); })
    , m_llama(store, [threads] { return qBound(2, threads(), 6); })
{
    m_asr.setFast(true); // live: greedy decoding, the lines come sooner
    // The better speech model (Large v3 Turbo, 9 times slower than Small on the processor) is for live only when a graphics card runs it: about a second
    // for twenty seconds of speech on a Radeon 780M. It needs the room, and it must be downloaded.
    {
        const Audio::HwPlan plan = Audio::Hardware::instance().plan(!store->path(Audio::LlamaSession::gpuHelperId()).isEmpty());
        qint64 room = 0;
        for (const Whisper::GpuDevice &g : Whisper::gpus())
            room = qMax(room, g.freeBytes);
        if (plan.whisperGpu && room >= (Q_INT64_C(5) << 29) && !store->path(Audio::WhisperAsr::modelFor(QStringLiteral("high"))).isEmpty())
            m_quality = QStringLiteral("high");
    }
    m_asrPool.setMaxThreadCount(1);
    m_llmPool.setMaxThreadCount(1);
    m_llmReady = Audio::LlamaSession::available(store);
    // the speech model loads now, while the person is still settling in, not when the first words come
    (void)QtConcurrent::run(&m_asrPool, [this] {
        const QString err = m_asr.prepare(m_quality);
        const QString engine = err.isEmpty() ? m_store->item(Audio::WhisperAsr::modelFor(m_quality)).title + QStringLiteral(" · ") + Audio::Hardware::instance().reported(QStringLiteral("speech")) : QString();
        QMetaObject::invokeMethod(this, [this, err, engine] { m_engine = engine; setStatus(err.isEmpty() ? QStringLiteral("listening") : QStringLiteral("failed"), err); }, Qt::QueuedConnection);
    });
    const QStringList on = LanguageSettings::enabled();
    m_target = on.isEmpty() ? QStringLiteral("en") : on.first();
}

LiveSession::~LiveSession()
{
    m_cancelAsr = true;
    m_cancelLlm = true;
    m_asrPool.waitForDone();
    m_llmPool.waitForDone();
    m_llama.stop();
}

void LiveSession::setStatus(const QString &status, const QString &notice)
{
    if (status == m_status && notice == m_notice)
        return;
    m_status = status;
    m_notice = notice;
    emit statusChanged();
}

void LiveSession::setMine(const QString &c) { if (c != m_mine) { { QMutexLocker lock(&m_lock); m_mine = c; } emit optionsChanged(); } }
void LiveSession::setTheirs(const QString &c) { if (c != m_theirs) { { QMutexLocker lock(&m_lock); m_theirs = c; } emit optionsChanged(); } }
void LiveSession::setTarget(const QString &c) { if (c != m_target) { m_target = c; emit optionsChanged(); } }
void LiveSession::setTranslate(bool on) { if (on != m_translate) { m_translate = on; emit optionsChanged(); if (on) warmLlm(); } }

void LiveSession::warmLlm()
{
    if (!m_llmReady || m_finished)
        return;
    (void)QtConcurrent::run(&m_llmPool, [this] {
        if (m_cancelLlm || m_llmStarted)
            return;
        m_llmStarted = true;
        const QString err = m_llama.start(&m_cancelLlm);
        if (!err.isEmpty()) {
            m_llmFailed = true;
            QMetaObject::invokeMethod(this, [this, err] { setStatus(m_status, err); }, Qt::QueuedConnection);
        }
    });
}

QString LiveSession::languageOf(int side) const { return side == 0 ? m_mine : m_theirs; }

void LiveSession::audio(int side, const QByteArray &s16le, qint64 endMs)
{
    if (m_finished || side < 0 || side > 1)
        return;
    enqueue(m_seg[side].feed(s16le, endMs));
    maybePartial(side);
}

void LiveSession::enqueue(const QList<Utterance> &list)
{
    if (list.isEmpty())
        return;
    ++m_gen[list.first().side]; // what a reading in progress was about has ended
    m_partialLen[list.first().side] = 0;
    bool launch = false;
    int dropped = 0;
    {
        QMutexLocker lock(&m_lock);
        m_queue.append(list);
        dropped = Audio::dropOverflow(m_queue, 30.0);
        if (!m_draining) {
            m_draining = true;
            launch = true;
        }
    }
    if (dropped > 0)
        setStatus(QStringLiteral("behind"), tr("The live view is falling behind, so some speech is left out of it. The recording is complete: process it afterwards for the full transcript."));
    if (launch)
        (void)QtConcurrent::run(&m_asrPool, [this] { drain(); });
}

// A first reading of what is being said, every second or so while a side talks, so the words appear while they are said and not only
// after the sentence is over. Only when the recognizer has nothing else to do: finished utterances always come first.
void LiveSession::maybePartial(int side)
{
    const Audio::LiveSegmenter &s = m_seg[side];
    if (m_finished || m_partialRunning || !s.speaking())
        return;
    const qint64 n = s.current().size();
    if (n < Audio::LiveSegmenter::Rate || n - m_partialLen[side] < Audio::LiveSegmenter::Rate * 4 / 5)
        return;
    QString lang;
    {
        QMutexLocker lock(&m_lock);
        if (m_draining || !m_queue.isEmpty())
            return;
        lang = side == 0 ? m_mine : m_theirs;
    }
    if (lang == QLatin1String("auto") && !m_lastLang[side].isEmpty())
        lang = m_lastLang[side]; // detecting again every second costs time: the language of the last finished line will do
    constexpr qint64 Keep = qint64(Audio::LiveSegmenter::Rate) * 15;
    QVector<float> pcm = s.current();
    qint64 start = s.currentStartMs();
    if (pcm.size() > Keep) {
        start += (pcm.size() - Keep) * 1000 / Audio::LiveSegmenter::Rate;
        pcm = pcm.mid(int(pcm.size() - Keep));
    }
    m_partialRunning = true;
    m_partialLen[side] = n;
    const int gen = m_gen[side];
    (void)QtConcurrent::run(&m_asrPool, [this, side, pcm, start, lang, gen] {
        QString text, detected;
        if (!m_cancelAsr && m_asr.prepare(m_quality).isEmpty()) {
            QList<Audio::Segment> segments;
            if (m_asr.transcribe(pcm, lang, &m_cancelAsr, &segments, &detected).isEmpty()) {
                QStringList words;
                for (const Audio::Segment &sg : std::as_const(segments))
                    words << sg.text;
                text = words.join(u' ').simplified();
            }
        }
        QMetaObject::invokeMethod(this, [this, side, start, text, detected, gen] {
            m_partialRunning = false;
            if (gen == m_gen[side] && !m_finished && !text.isEmpty())
                m_model.setPartial(side, start, text, detected);
        }, Qt::QueuedConnection);
    });
}

// on the recognizer's thread
void LiveSession::drain()
{
    const QString err = m_asr.prepare(m_quality);
    if (!err.isEmpty()) {
        {
            QMutexLocker lock(&m_lock);
            m_queue.clear();
            m_draining = false;
        }
        QMetaObject::invokeMethod(this, [this, err] { setStatus(QStringLiteral("failed"), err); }, Qt::QueuedConnection);
        return;
    }
    QMetaObject::invokeMethod(this, [this] { if (m_status == QLatin1String("loading")) setStatus(QStringLiteral("listening")); }, Qt::QueuedConnection);
    for (;;) {
        Utterance u;
        QString lang; // read now; a change takes effect with the next utterance
        {
            QMutexLocker lock(&m_lock);
            if (m_queue.isEmpty() || m_cancelAsr) {
                m_draining = false;
                return;
            }
            u = m_queue.takeFirst();
            lang = u.side == 0 ? m_mine : m_theirs;
        }
        QList<Audio::Segment> segments;
        QString detected;
        if (!m_asr.transcribe(u.samples, lang, &m_cancelAsr, &segments, &detected).isEmpty())
            continue;
        QStringList words;
        for (const Audio::Segment &s : std::as_const(segments))
            words << s.text;
        const QString text = words.join(u' ').simplified();
        QMetaObject::invokeMethod(this, [this, u, text, detected] { heard(u, text, detected); }, Qt::QueuedConnection); // even when nothing was heard: the first reading goes
    }
}

// on the main thread
void LiveSession::heard(const Utterance &u, const QString &text, const QString &detected)
{
    if (text.isEmpty()) {
        m_model.clearPartial(u.side, u.endMs);
        return;
    }
    LiveModel::Line l;
    l.side = u.side;
    l.startMs = u.startMs;
    l.endMs = u.endMs;
    l.text = text;
    l.language = detected;
    m_model.clearPartial(u.side, u.endMs);
    if (!detected.isEmpty())
        m_lastLang[u.side] = detected;
    const qint64 id = m_model.add(l);
    bool idle;
    {
        QMutexLocker lock(&m_lock);
        idle = m_queue.isEmpty();
    }
    if (m_status == QLatin1String("behind") && idle)
        setStatus(QStringLiteral("listening"));
    translateLine(id, u.side, text, detected);
}

void LiveSession::translateLine(qint64 id, int side, const QString &text, const QString &detected)
{
    if (!m_translate)
        return;
    if (!m_llmReady || m_llmFailed || m_llmWaiting >= 6) // too far behind: the original stays
        return;
    // from and to
    const auto nameOf = [](const QString &catalogOrWhisper) {
        if (Lang::known(catalogOrWhisper))
            return Lang::englishName(catalogOrWhisper);
        for (const Lang::Language &l : Lang::all())
            if (l.whisper() == catalogOrWhisper)
                return l.english;
        return QString();
    };
    // every line, yours and theirs, goes into the one language chosen; one that is in it already is left as it is
    const QString spoken = side == 0 ? m_mine : m_theirs;
    const QString from = spoken != QLatin1String("auto") ? nameOf(spoken) : nameOf(detected);
    const QString to = nameOf(m_target);
    if (to.isEmpty() || (!from.isEmpty() && from == to))
        return;
    QStringList before;
    const auto &lines = m_model.lines();
    for (int i = int(lines.size()) - 1; i >= 0 && before.size() < 2; --i)
        if (lines[i].id != id && lines[i].side == side && !lines[i].partial)
            before.prepend(lines[i].text);
    ++m_llmWaiting;
    (void)QtConcurrent::run(&m_llmPool, [this, id, from, to, before, text] {
        QString out;
        if (!m_cancelLlm) {
            if (!m_llmStarted) {
                m_llmStarted = true;
                const QString err = m_llama.start(&m_cancelLlm);
                if (!err.isEmpty()) {
                    m_llmFailed = true;
                    QMetaObject::invokeMethod(this, [this, err] { setStatus(m_status, err); }, Qt::QueuedConnection);
                }
            }
            if (!m_llmFailed)
                out = Audio::Live::translate(m_llama.chatFn(&m_cancelLlm), from, to, before, text);
        }
        --m_llmWaiting;
        if (!out.isEmpty())
            QMetaObject::invokeMethod(this, [this, id, out] { m_model.setTranslation(id, out); }, Qt::QueuedConnection);
    });
}

void LiveSession::suggestAnswer(const QString &note)
{
    if (m_suggesting)
        return;
    if (!m_llmReady) {
        setStatus(m_status, tr("Smart answer needs the language model: download it in Settings → AI."));
        return;
    }
    QStringList lines;
    for (const LiveModel::Line &l : m_model.lines())
        if (!l.partial)
            lines << QStringLiteral("%1: %2").arg(l.side == 0 ? tr("You") : tr("Others"), l.text);
    if (lines.isEmpty())
        return;
    lines = Audio::Live::tail(lines);
    const QString language = Lang::englishName(m_target);
    m_suggesting = true;
    emit suggestionsChanged();
    (void)QtConcurrent::run(&m_llmPool, [this, lines, note, language] {
        QList<Audio::Live::Answer> answers;
        if (!m_cancelLlm) {
            if (!m_llmStarted) {
                m_llmStarted = true;
                const QString err = m_llama.start(&m_cancelLlm);
                if (!err.isEmpty()) {
                    m_llmFailed = true;
                    QMetaObject::invokeMethod(this, [this, err] { setStatus(m_status, err); }, Qt::QueuedConnection);
                }
            }
            if (!m_llmFailed)
                answers = Audio::Live::suggest(m_llama.chatFn(&m_cancelLlm), language, lines, note);
        }
        QMetaObject::invokeMethod(this, [this, answers] {
            QVariantList list;
            for (const Audio::Live::Answer &a : answers)
                list.append(QVariantMap{{QStringLiteral("text"), a.text}, {QStringLiteral("tone"), a.tone}});
            m_suggestions = list;
            m_suggesting = false;
            emit suggestionsChanged();
        }, Qt::QueuedConnection);
    });
}

void LiveSession::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    for (auto &s : m_seg)
        enqueue(s.flush());
    // the recognizer gets a little time to finish what it has; what is still waiting after that is only missing from the live view
    for (int i = 0; i < 200; ++i) {
        {
            QMutexLocker lock(&m_lock);
            if (!m_draining && m_queue.isEmpty())
                break;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(50);
    }
    m_cancelAsr = true;
    m_cancelLlm = true;
    m_asrPool.waitForDone(5000);
    m_llmPool.waitForDone(5000);
    QCoreApplication::sendPostedEvents(this);
    m_llama.stop();
}

QList<Audio::Segment> LiveSession::segments() const
{
    QList<Audio::Segment> out;
    for (const LiveModel::Line &l : m_model.lines())
        if (!l.partial)
            out.append({double(l.startMs) / 1000.0, double(l.endMs) / 1000.0, l.text, l.side == 0 ? tr("You") : tr("Others"), false});
    return out;
}
