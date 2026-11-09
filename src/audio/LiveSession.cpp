#include "LiveSession.h"

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
    m_asrPool.setMaxThreadCount(1);
    m_llmPool.setMaxThreadCount(1);
    m_llmReady = Audio::LlamaSession::available(store);
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
void LiveSession::setTranslate(bool on) { if (on != m_translate) { m_translate = on; emit optionsChanged(); } }
void LiveSession::setTranslateMine(bool on) { if (on != m_translateMine) { m_translateMine = on; emit optionsChanged(); } }

QString LiveSession::languageOf(int side) const { return side == 0 ? m_mine : m_theirs; }

void LiveSession::audio(int side, const QByteArray &s16le, qint64 endMs)
{
    if (m_finished || side < 0 || side > 1)
        return;
    enqueue(m_seg[side].feed(s16le, endMs));
}

void LiveSession::enqueue(const QList<Utterance> &list)
{
    if (list.isEmpty())
        return;
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

// on the recognizer's thread
void LiveSession::drain()
{
    const QString err = m_asr.prepare(QStringLiteral("standard"));
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
        if (!text.isEmpty())
            QMetaObject::invokeMethod(this, [this, u, text, detected] { heard(u, text, detected); }, Qt::QueuedConnection);
    }
}

// on the main thread
void LiveSession::heard(const Utterance &u, const QString &text, const QString &detected)
{
    LiveModel::Line l;
    l.side = u.side;
    l.startMs = u.startMs;
    l.endMs = u.endMs;
    l.text = text;
    l.language = detected;
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
    if (side == 0 ? !m_translateMine : !m_translate)
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
    QString from, to;
    if (side == 1) {
        from = m_theirs != QLatin1String("auto") ? nameOf(m_theirs) : nameOf(detected);
        to = nameOf(m_target);
    } else {
        from = m_mine != QLatin1String("auto") ? nameOf(m_mine) : nameOf(detected);
        to = m_theirs != QLatin1String("auto") ? nameOf(m_theirs) : QString();
    }
    if (to.isEmpty() || (!from.isEmpty() && from == to))
        return;
    QStringList before;
    const auto &lines = m_model.lines();
    for (int i = int(lines.size()) - 1; i >= 0 && before.size() < 2; --i)
        if (lines[i].id != id && lines[i].side == side)
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
        setStatus(m_status, tr("Smart answer needs the language model: download it in Settings → AI → Audio Intelligence."));
        return;
    }
    QStringList lines;
    for (const LiveModel::Line &l : m_model.lines())
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
        out.append({double(l.startMs) / 1000.0, double(l.endMs) / 1000.0, l.text, l.side == 0 ? tr("You") : tr("Others"), false});
    return out;
}
