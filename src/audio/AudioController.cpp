#include "AudioController.h"

#include "EngineParse.h"
#include "Hardware.h"
#include "NameHints.h"
#include "services/LanguageSettings.h"
#include "WavFile.h"
#include "platform/NotchPaths.h"

#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <cmath>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>

namespace {
constexpr double MaxSeconds = 3600;
const QStringList Extensions{QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("m4a"), QStringLiteral("flac"), QStringLiteral("ogg"),
                             QStringLiteral("webm"), QStringLiteral("opus"), QStringLiteral("aac"), QStringLiteral("mp4"), QStringLiteral("mka")};

QString kindName(Audio::Kind k)
{
    switch (k) {
    case Audio::Kind::Whisper: return QStringLiteral("speech");
    case Audio::Kind::Llm: return QStringLiteral("language");
    case Audio::Kind::Segmentation: return QStringLiteral("speakers");
    case Audio::Kind::Embedding: return QStringLiteral("speakers");
    case Audio::Kind::Helper: return QStringLiteral("program");
    case Audio::Kind::Ocr: return QStringLiteral("ocr");
    case Audio::Kind::Vision: return QStringLiteral("vision");
    case Audio::Kind::Embedder: return QStringLiteral("embedder");
    }
    return {};
}

QString localPath(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}
}

AudioController::AudioController(QObject *parent)
    : QObject(parent)
    , m_settings(NotchPaths::conf(QStringLiteral("audio")), QSettings::IniFormat)
    , m_models(NotchPaths::dataDir(QStringLiteral("audio-models")))
    , m_recorder(this)
    , m_asr(&m_models, [this] { return threads(); }, [] { return LanguageSettings::whisperCodes(LanguageSettings::enabled()); })
    , m_diar(&m_models, [this] { return threads(); }, [this] { const QString v = speakerSplit(); return v == QLatin1String("fewer") ? 0.75 : v == QLatin1String("more") ? 0.4 : 0.55; })
    , m_sum(&m_models, [this] { return threads(); })
{
    m_pool.setMaxThreadCount(1);
    QDir().mkpath(NotchPaths::dataDir(QStringLiteral("audio/meetings")));
    const QString err = m_store.open(NotchPaths::dataDir(QStringLiteral("audio")) + QStringLiteral("/audio.db"));
    if (!err.isEmpty())
        m_error = err;
    else
        m_store.markInterrupted();
    reload();
    connect(&m_models, &ModelStore::changed, this, [this](const QString &) { emit modelsChanged(); });
    connect(&m_recorder, &MeetingRecorder::levelChanged, this, &AudioController::recordingChanged);
    connect(&m_recorder, &MeetingRecorder::failed, this, [this](const QString &why) { setError(why); });
    connect(&m_recorder, &MeetingRecorder::limitReached, this, [this] { stopRecording(false); });
    (void)QtConcurrent::run([this] { // what graphics cards there are, found without making the window wait
        Audio::Hardware::instance().info();
        QMetaObject::invokeMethod(this, &AudioController::hardwareChanged, Qt::QueuedConnection);
    });
    m_tick.setInterval(500);
    connect(&m_tick, &QTimer::timeout, this, &AudioController::recordingChanged);
}

AudioController::~AudioController()
{
    m_cancel = true;
    m_pool.waitForDone(5000);
}

// ---- settings ---------------------------------------------------------------------------------------------

int AudioController::threads() const
{
    return qBound(1, m_settings.value(QStringLiteral("threads"), qBound(2, QThread::idealThreadCount() - 2, 8)).toInt(), qMax(1, QThread::idealThreadCount()));
}

void AudioController::setThreads(int n)
{
    m_settings.setValue(QStringLiteral("threads"), qBound(1, n, qMax(1, QThread::idealThreadCount())));
    emit settingsChanged();
}

void AudioController::setQuality(const QString &q)
{
    m_settings.setValue(QStringLiteral("quality"), q == QLatin1String("high") ? q : QStringLiteral("standard"));
    emit settingsChanged();
    emit modelsChanged();
}

void AudioController::setLanguage(const QString &l)
{
    m_settings.setValue(QStringLiteral("language"), LanguageSettings::valid(l));
    emit settingsChanged();
}

void AudioController::setTmpl(const QString &t)
{
    m_settings.setValue(QStringLiteral("template"), Audio::templateId(Audio::templateFrom(t)));
    emit settingsChanged();
}

void AudioController::setRamLimitMb(int mb)
{
    m_settings.setValue(QStringLiteral("ramLimitMb"), qMax(0, mb));
    emit settingsChanged();
    emit modelsChanged();
}

void AudioController::setError(const QString &e)
{
    if (e == m_error)
        return;
    m_error = e;
    emit errorChanged();
}

// ---- what the pages show ----------------------------------------------------------------------------------

QString AudioController::meetingDir(qlonglong id) const { return NotchPaths::dataDir(QStringLiteral("audio/meetings/%1").arg(id)); }

QVariantMap AudioController::info() const
{
    if (!m_current)
        return {};
    const AudioStore::Meeting m = m_store.meeting(m_current);
    if (!m.id)
        return {};
    const AudioStore::Job job = m_store.lastJob(m.id);
    return {{QStringLiteral("id"), m.id}, {QStringLiteral("title"), m.title}, {QStringLiteral("status"), m.status}, {QStringLiteral("language"), m.language},
            {QStringLiteral("duration"), m.duration}, {QStringLiteral("durationText"), m.duration > 0 ? Audio::clock(m.duration) : QString()},
            {QStringLiteral("stereo"), m.stereo}, {QStringLiteral("template"), m.tmpl}, {QStringLiteral("quality"), m.quality}, {QStringLiteral("source"), m.source},
            {QStringLiteral("audioUrl"), QUrl::fromLocalFile(m.audioPath)}, {QStringLiteral("created"), QLocale().toString(m.created.toLocalTime(), QLocale::LongFormat)},
            {QStringLiteral("error"), m.status == QLatin1String("failed") ? job.error : QString()}, {QStringLiteral("hasTranscript"), m.segments > 0 || !m_transcript.rows().isEmpty()},
            {QStringLiteral("hasNotes"), m_notes.value(QStringLiteral("hasNotes")).toBool()}, {QStringLiteral("speakersHint"), m.speakersHint},
            {QStringLiteral("segments"), int(m_transcript.rows().size())}, {QStringLiteral("actionCount"), int(m_notes.value(QStringLiteral("actions")).toList().size())},
            {QStringLiteral("speakerCount"), int(m_speakers.size())},
            {QStringLiteral("oneSpeaker"), m_speakers.size() <= 1 && m.duration > 60 && !m_transcript.rows().isEmpty()}};
}

QVariantList AudioController::stages() const
{
    QVariantList out;
    const QStringList ids = Audio::AudioPipeline::stageIds();
    const int active = ids.indexOf(m_stage);
    for (int i = 0; i < ids.size(); ++i) {
        QString state = QStringLiteral("pending");
        if (m_busy && (m_doneStages.contains(ids[i]) || (active >= 0 && i < active)))
            state = QStringLiteral("done");
        else if (m_busy && i == active)
            state = QStringLiteral("active");
        out.append(QVariantMap{{QStringLiteral("id"), ids[i]}, {QStringLiteral("label"), Audio::AudioPipeline::stageLabel(ids[i])}, {QStringLiteral("state"), state},
                               {QStringLiteral("progress"), state == QLatin1String("active") ? m_stageFraction : state == QLatin1String("done") ? 1.0 : 0.0}});
    }
    return out;
}

static QString etaText(qint64 seconds)
{
    if (seconds < 0)
        return {};
    if (seconds >= 3600)
        return QObject::tr("%1 h %2 min left").arg(seconds / 3600).arg(seconds % 3600 / 60);
    if (seconds >= 60)
        return QObject::tr("%1 min %2 s left").arg(seconds / 60).arg(seconds % 60);
    return QObject::tr("%1 s left").arg(seconds);
}

QVariantList AudioController::models() const
{
    QVariantList out;
    for (const Audio::CatalogItem &i : m_models.items()) {
        const QString st = m_models.state(i.id);
        out.append(QVariantMap{{QStringLiteral("id"), i.id}, {QStringLiteral("kind"), kindName(i.kind)}, {QStringLiteral("title"), i.title}, {QStringLiteral("note"), i.note},
                               {QStringLiteral("state"), st}, {QStringLiteral("ready"), st == QLatin1String("ready")}, {QStringLiteral("progress"), m_models.progress(i.id)},
                               {QStringLiteral("error"), m_models.error(i.id)}, {QStringLiteral("sizeText"), QLocale().formattedDataSize(i.size)},
                               {QStringLiteral("diskText"), QLocale().formattedDataSize(m_models.diskBytes(i.id))}, {QStringLiteral("partial"), m_models.partialBytes(i.id) > 0},
                               {QStringLiteral("speedText"), st == QLatin1String("downloading") && m_models.speed(i.id) > 0 ? QLocale().formattedDataSize(qint64(m_models.speed(i.id))) + QStringLiteral("/s") : QString()},
                               {QStringLiteral("receivedText"), st == QLatin1String("downloading") ? QLocale().formattedDataSize(m_models.receivedBytes(i.id)) + QStringLiteral(" / ") + QLocale().formattedDataSize(i.size) : QString()},
                               {QStringLiteral("etaText"), st == QLatin1String("downloading") && m_models.speed(i.id) > 1 ? etaText(qint64((i.size - m_models.receivedBytes(i.id)) / m_models.speed(i.id))) : QString()}});
    }
    return out;
}

QVariantMap AudioController::readiness() const
{
    QStringList messages;
    const bool asr = !m_models.path(Audio::WhisperAsr::modelFor(QStringLiteral("standard"))).isEmpty();
    const bool asrHigh = !m_models.path(Audio::WhisperAsr::modelFor(QStringLiteral("high"))).isEmpty();
    const bool ffmpeg = !Audio::FfmpegPreprocessor::program().isEmpty();
    if (!Whisper::compiled())
        messages << tr("This build of Notch has no speech recognition.");
    else if (!(quality() == QLatin1String("high") ? asrHigh : asr))
        messages << tr("Download the speech model to turn recordings into text.");
    if (!m_diar.available())
        messages << tr("Optional: download the speaker model to tell the speakers apart.");
    if (!m_sum.available())
        messages << tr("Optional: download the language model to get notes, decisions and tasks.");
    QStringList missingForNotes; // what the notes need and is not installed: the language model and the program that runs it
    for (const Audio::CatalogItem &i : m_models.items())
        if ((i.id == QLatin1String("qwen3-4b") || i.id.startsWith(QLatin1String("llama-server"))) && m_models.state(i.id) != QLatin1String("ready"))
            missingForNotes << i.id;
    return {{QStringLiteral("summaryMissing"), missingForNotes}, {QStringLiteral("compiled"), Whisper::compiled()}, {QStringLiteral("asr"), asr}, {QStringLiteral("asrHigh"), asrHigh}, {QStringLiteral("diarize"), m_diar.available()},
            {QStringLiteral("summary"), m_sum.available()}, {QStringLiteral("ffmpeg"), ffmpeg}, {QStringLiteral("messages"), messages}};
}

QVariantList AudioController::knowledgeMeetings() const
{
    QVariantList out;
    for (const AudioStore::Meeting &m : m_store.meetings())
        if (m.segments > 0)
            out.append(QVariantMap{{QStringLiteral("id"), m.id}, {QStringLiteral("title"), m.title}, {QStringLiteral("created"), m.created.toSecsSinceEpoch()},
                                   {QStringLiteral("fingerprint"), QStringLiteral("%1|%2|%3").arg(m.segments).arg(m.updated.toSecsSinceEpoch()).arg(m.title)}});
    return out;
}

QList<Knowledge::Utterance> AudioController::transcriptLines(qlonglong id) const
{
    QList<Knowledge::Utterance> out;
    for (const AudioStore::SegmentRow &s : m_store.segments(id))
        out.append({s.start, s.who(), s.text()});
    return out;
}

QList<Audio::ActionItem> AudioController::actionItemsOf(qlonglong id) const
{
    QList<Audio::ActionItem> out;
    for (const AudioStore::ActionRow &r : m_store.actions(id))
        out.append(r.item);
    return out;
}

QVariantMap AudioController::hardware() const
{
    Audio::Hardware &hw = Audio::Hardware::instance();
    QVariantList devices;
    const Gpu::Info info = hw.known();
    for (const Gpu::Device &d : info.devices)
        devices.append(QVariantMap{{QStringLiteral("name"), d.name}, {QStringLiteral("vendor"), d.vendor}, {QStringLiteral("integrated"), d.integrated},
                                   {QStringLiteral("memory"), QLocale().formattedDataSize(d.vramTotal)}});
    const Audio::HwPlan plan = hw.detected() ? hw.plan(!m_models.path(Audio::LlamaSession::gpuHelperId()).isEmpty()) : Audio::HwPlan{};
    const Audio::HwBench bench = hw.detected() ? hw.bench() : Audio::HwBench{};
    return {{QStringLiteral("detected"), hw.detected()}, {QStringLiteral("setting"), hw.setting()}, {QStringLiteral("devices"), devices}, {QStringLiteral("vulkan"), info.vulkan},
            {QStringLiteral("built"), !Whisper::gpus().isEmpty() || Whisper::gpusBuilt()}, {QStringLiteral("reason"), plan.reason}, {QStringLiteral("speech"), hw.reported(QStringLiteral("speech"))},
            {QStringLiteral("notes"), hw.reported(QStringLiteral("notes"))}, {QStringLiteral("failure"), hw.lastFailure()}, {QStringLiteral("benchKnown"), bench.known},
            {QStringLiteral("speedup"), bench.speedup}, {QStringLiteral("testing"), m_testing.load()}, {QStringLiteral("testResult"), m_testResult},
            {QStringLiteral("helperId"), Audio::LlamaSession::gpuHelperId()}, {QStringLiteral("helperReady"), !m_models.path(Audio::LlamaSession::gpuHelperId()).isEmpty()}};
}

void AudioController::setHardwareMode(const QString &mode)
{
    Audio::Hardware::instance().setSetting(mode);
    emit hardwareChanged();
}

void AudioController::detectHardware()
{
    (void)QtConcurrent::run([this] {
        Audio::Hardware::instance().refresh();
        QMetaObject::invokeMethod(this, &AudioController::hardwareChanged, Qt::QueuedConnection);
    });
}

// The speech model on the processor and on the best card, on the same 10 s of sound: how many times quicker the card was. It decides "Auto" for an
// integrated card, which shares the computer's memory and is not always quicker. The words do not matter, the work of the model does.
void AudioController::speedTest()
{
    if (m_testing.exchange(true))
        return;
    m_testResult.clear();
    emit hardwareChanged();
    (void)QtConcurrent::run([this] {
        QString result;
        const QString path = m_models.path(Audio::WhisperAsr::modelFor(QStringLiteral("standard")));
        if (!Whisper::compiled()) {
            result = tr("This build of Notch has no speech recognition.");
        } else if (Whisper::gpus().isEmpty()) {
            result = tr("This build or this computer has no graphics card the speech model can use.");
        } else if (path.isEmpty()) {
            result = tr("Download the speech model first.");
        } else {
            QVector<float> sound(16000 * 10);
            for (int i = 0; i < sound.size(); ++i) { // something like a voice: a low tone that swells and fades
                const double t = i / 16000.0;
                sound[i] = float(0.25 * std::sin(2 * M_PI * 180.0 * t) * (0.5 + 0.5 * std::sin(2 * M_PI * 2.5 * t)) + 0.08 * std::sin(2 * M_PI * 1200.0 * t));
            }
            const auto timeIt = [&](bool gpu, double *ms, QString *backend) {
                Whisper w;
                if (!w.load(path, gpu))
                    return false;
                if (gpu && w.fellBack())
                    return false;
                QList<Whisper::Piece> pieces;
                QString detected;
                std::atomic_bool never{false};
                w.transcribeSegments(sound.mid(0, 16000 * 2), QStringLiteral("en"), qMax(2, threads()), &never, &pieces, &detected, {}, true); // warm up
                QElapsedTimer t;
                t.start();
                w.transcribeSegments(sound, QStringLiteral("en"), qMax(2, threads()), &never, &pieces, &detected, {}, true);
                *ms = double(t.elapsed());
                *backend = w.backend();
                return true;
            };
            double cpu = 0, gpu = 0;
            QString b1, b2;
            if (!timeIt(false, &cpu, &b1))
                result = tr("The speech model could not be loaded.");
            else if (!timeIt(true, &gpu, &b2))
                result = tr("The speech model could not use the graphics card.");
            else if (cpu <= 0 || gpu <= 0)
                result = tr("The test was too quick to measure.");
            else {
                const double speedup = cpu / gpu;
                Audio::Hardware::instance().setBench(speedup);
                result = tr("%1 took %2 s, the processor %3 s: %4 times %5").arg(b2).arg(gpu / 1000.0, 0, 'f', 1).arg(cpu / 1000.0, 0, 'f', 1).arg(speedup, 0, 'f', 1).arg(speedup >= 1 ? tr("quicker") : tr("slower"));
            }
        }
        QMetaObject::invokeMethod(this, [this, result] {
            m_testResult = result;
            m_testing = false;
            emit hardwareChanged();
        }, Qt::QueuedConnection);
    });
}

QString AudioController::readinessProblem(const QString &quality) const
{
    if (!Whisper::compiled())
        return tr("This build of Notch has no speech recognition.");
    const QString id = Audio::WhisperAsr::modelFor(quality);
    if (m_models.path(id).isEmpty())
        return tr("The speech model (%1) is not installed. Download it in Settings → AI.").arg(m_models.item(id).title);
    const qint64 limit = qint64(ramLimitMb()) << 20;
    if (limit > 0 && m_models.item(id).memory > limit)
        return tr("%1 needs about %2 of memory, more than the limit you set (%3).")
            .arg(m_models.item(id).title, QLocale().formattedDataSize(m_models.item(id).memory), QLocale().formattedDataSize(limit));
    return {};
}

// ---- the meetings -------------------------------------------------------------------------------------------

void AudioController::reload()
{
    if (!m_store.isOpen())
        return;
    m_list.setAll(m_store.meetings(m_search));
    emit listChanged();
}

void AudioController::search(const QString &text)
{
    m_search = text;
    reload();
}

QVariantMap AudioController::inspect(const QString &pathOrUrl) const
{
    const QString path = localPath(pathOrUrl);
    const QFileInfo fi(path);
    QVariantMap out{{QStringLiteral("ok"), false}, {QStringLiteral("name"), fi.fileName()}, {QStringLiteral("path"), path}};
    if (!fi.isFile()) {
        out.insert(QStringLiteral("warning"), tr("This file does not exist."));
        return out;
    }
    if (!Extensions.contains(fi.suffix().toLower())) {
        out.insert(QStringLiteral("warning"), tr("This kind of file is not supported. Use MP3, WAV, M4A, FLAC, OGG or WebM."));
        return out;
    }
    out.insert(QStringLiteral("sizeText"), QLocale().formattedDataSize(fi.size()));
    double seconds = -1;
    const Wav::Info wi = Wav::readInfo(path);
    const bool nativeWav = wi.valid() && wi.rate == 16000 && wi.channels == 1 && wi.bits == 16;
    if (wi.valid()) {
        seconds = wi.seconds();
    } else if (const QString ffmpeg = Audio::FfmpegPreprocessor::program(); !ffmpeg.isEmpty()) {
        QProcess probe;
        probe.setProcessChannelMode(QProcess::MergedChannels);
        probe.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-i"), path});
        if (probe.waitForFinished(5000))
            seconds = Audio::parseFfmpegDuration(QString::fromUtf8(probe.readAll()));
        else
            probe.kill();
    }
    out.insert(QStringLiteral("seconds"), seconds);
    out.insert(QStringLiteral("durationText"), seconds > 0 ? Audio::clock(seconds) : tr("unknown"));
    out.insert(QStringLiteral("ffmpeg"), !Audio::FfmpegPreprocessor::program().isEmpty());
    out.insert(QStringLiteral("needsFfmpeg"), !nativeWav);
    const Audio::CatalogItem asr = m_models.item(Audio::WhisperAsr::modelFor(quality()));
    out.insert(QStringLiteral("memoryText"), tr("about %1 of memory while it is transcribed (%2)").arg(QLocale().formattedDataSize(asr.memory), asr.title));
    QString warning;
    if (seconds > MaxSeconds)
        warning = tr("This recording is longer than 60 minutes, which is the most Notch takes for now.");
    else if (!nativeWav && Audio::FfmpegPreprocessor::program().isEmpty())
        warning = tr("ffmpeg is needed to read this kind of file. Install it, or use a 16 kHz mono WAV.");
    out.insert(QStringLiteral("warning"), warning);
    out.insert(QStringLiteral("ok"), warning.isEmpty());
    return out;
}

qlonglong AudioController::importFile(const QString &pathOrUrl, const QString &title, const QString &language, const QString &quality, const QString &tmplId, bool autoStart, int speakers)
{
    if (!m_store.isOpen()) {
        setError(m_error.isEmpty() ? tr("The audio database is not available") : m_error);
        return 0;
    }
    const QVariantMap check = inspect(pathOrUrl);
    if (!check.value(QStringLiteral("ok")).toBool()) {
        setError(check.value(QStringLiteral("warning")).toString());
        return 0;
    }
    const QFileInfo fi(check.value(QStringLiteral("path")).toString());
    AudioStore::Meeting m;
    m.title = title.trimmed().isEmpty() ? fi.completeBaseName() : title.trimmed().left(120);
    m.originalFilename = fi.fileName();
    m.audioPath = fi.absoluteFilePath();
    m.duration = qMax(0.0, check.value(QStringLiteral("seconds")).toDouble());
    m.language = LanguageSettings::valid(language);
    m.quality = quality == QLatin1String("high") ? quality : QStringLiteral("standard");
    m.tmpl = Audio::templateId(Audio::templateFrom(tmplId));
    m.source = QStringLiteral("import");
    m.speakersHint = qBound(0, speakers, 8);
    const qint64 id = m_store.addMeeting(m);
    if (!id) {
        setError(tr("The meeting could not be saved"));
        return 0;
    }
    reload();
    if (autoStart)
        process(id);
    return id;
}

void AudioController::open(qlonglong id)
{
    m_current = id;
    m_waveform.clear();
    loadCurrent();
    loadWaveform(id);
    emit waveformChanged();
    emit currentChanged();
}

void AudioController::close()
{
    m_current = 0;
    m_transcript.setAll({});
    m_notes.clear();
    m_speakers.clear();
    m_waveform.clear();
    emit currentChanged();
    emit notesChanged();
    emit speakersChanged();
    emit waveformChanged();
}

void AudioController::loadCurrent()
{
    if (!m_current)
        return;
    m_transcript.setAll(m_store.segments(m_current));
    m_speakers.clear();
    for (const QString &label : m_store.speakerLabels(m_current)) {
        QString name;
        int count = 0;
        for (const AudioStore::SegmentRow &r : m_transcript.rows())
            if (r.speakerLabel == label) {
                name = r.speakerName;
                ++count;
            }
        m_speakers.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("name"), name}, {QStringLiteral("count"), count},
                                      {QStringLiteral("index"), m_speakers.size()}});
    }
    m_notes.clear();
    if (m_store.hasSummary(m_current)) {
        QList<qint64> ids;
        const Audio::Summary s = m_store.summary(m_current, &ids);
        QVariantList decisions, actions;
        for (const Audio::Decision &d : s.decisions)
            decisions.append(QVariantMap{{QStringLiteral("description"), d.description}, {QStringLiteral("time"), d.timestamp}, {QStringLiteral("timeText"), d.timestamp >= 0 ? Audio::clock(d.timestamp) : QString()},
                                         {QStringLiteral("quote"), d.quote}, {QStringLiteral("review"), d.needsReview}, {QStringLiteral("reason"), d.rationale}, {QStringLiteral("owner"), d.owner}});
        const auto topicList = [](const QList<Audio::Topic> &list) {
            QVariantList out;
            for (const Audio::Topic &t : list)
                out.append(QVariantMap{{QStringLiteral("title"), t.title}, {QStringLiteral("points"), t.points}, {QStringLiteral("time"), t.timestamp},
                                       {QStringLiteral("timeText"), t.timestamp >= 0 ? Audio::clock(t.timestamp) : QString()}});
            return out;
        };
        for (int i = 0; i < s.actions.size(); ++i) {
            const Audio::ActionItem &a = s.actions[i];
            actions.append(QVariantMap{{QStringLiteral("id"), ids.value(i)}, {QStringLiteral("task"), a.task}, {QStringLiteral("assignee"), a.assignee}, {QStringLiteral("deadline"), a.deadline},
                                       {QStringLiteral("priority"), a.priority}, {QStringLiteral("done"), a.done}, {QStringLiteral("time"), a.timestamp},
                                       {QStringLiteral("timeText"), a.timestamp >= 0 ? Audio::clock(a.timestamp) : QString()}, {QStringLiteral("quote"), a.quote},
                                       {QStringLiteral("review"), a.needsReview}});
        }
        m_notes = {{QStringLiteral("hasNotes"), true}, {QStringLiteral("executive"), s.executive}, {QStringLiteral("keyPoints"), s.keyPoints}, {QStringLiteral("openQuestions"), s.openQuestions},
                   {QStringLiteral("notes"), s.notes}, {QStringLiteral("decisions"), decisions}, {QStringLiteral("actions"), actions},
                   {QStringLiteral("participants"), s.participants}, {QStringLiteral("topics"), topicList(s.topics)}, {QStringLiteral("concepts"), topicList(s.concepts)},
                   {QStringLiteral("qa"), topicList(s.qa)}, {QStringLiteral("quotes"), s.quotes}, {QStringLiteral("risks"), s.risks}, {QStringLiteral("nextSteps"), s.nextSteps}};
    }
    computeHints();
    emit notesChanged();
    emit speakersChanged();
}

// Names that the conversation gives (see NameHints.h), minus the ones already used or said no to.
void AudioController::computeHints()
{
    m_hints.clear();
    QList<Audio::Segment> lines;
    QStringList used, named{tr("You"), QStringLiteral("Anda")}; // "You" is whoever is using Notch: not renamed from what is said
    for (const AudioStore::SegmentRow &r : m_transcript.rows())
        lines.append({r.start, r.end, r.text(), r.speakerLabel, r.approx});
    for (const QVariant &v : std::as_const(m_speakers)) {
        const QVariantMap sp = v.toMap();
        if (const QString n = sp.value(QStringLiteral("name")).toString(); !n.isEmpty()) {
            used << n;
            named << sp.value(QStringLiteral("label")).toString();
        }
    }
    for (const Audio::NameHint &h : Audio::nameHints(lines, used, named)) {
        if (m_dismissed.contains(QStringLiteral("%1|%2|%3").arg(m_current).arg(h.speaker, h.name.toLower())))
            continue;
        m_hints.append(QVariantMap{{QStringLiteral("speaker"), h.speaker}, {QStringLiteral("name"), h.name}, {QStringLiteral("time"), h.time},
                                   {QStringLiteral("timeText"), Audio::clock(h.time)}, {QStringLiteral("quote"), h.quote}, {QStringLiteral("certain"), h.certain}});
    }
}

void AudioController::applyNameHint(const QString &speakerLabel, const QString &name)
{
    renameSpeaker(speakerLabel, name);
    computeHints();
    emit speakersChanged();
}

void AudioController::dismissNameHint(const QString &speakerLabel, const QString &name)
{
    m_dismissed.insert(QStringLiteral("%1|%2|%3").arg(m_current).arg(speakerLabel, name.toLower()));
    computeHints();
    emit speakersChanged();
}

QString AudioController::notesMarkdown() const
{
    if (!m_current || !m_store.hasSummary(m_current))
        return {};
    const AudioStore::Meeting m = m_store.meeting(m_current);
    const QStringList meta{QLocale().toString(m.created.toLocalTime(), QLocale::LongFormat), m.duration > 0 ? Audio::clock(m.duration) : QString()};
    QStringList parts;
    for (const QString &p : meta)
        if (!p.isEmpty())
            parts << p;
    return Audio::toMarkdown(m_store.summary(m_current), m.title, parts.join(QStringLiteral(" · ")));
}

void AudioController::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void AudioController::copyNotes()
{
    if (const QString md = notesMarkdown(); !md.isEmpty())
        QGuiApplication::clipboard()->setText(md);
}

void AudioController::redetectSpeakers(qlonglong id, int count)
{
    AudioStore::Meeting m = m_store.meeting(id);
    if (!m.id || m_store.segments(id).isEmpty())
        return;
    if (m_busy) {
        setError(tr("Another recording is being processed. Try again when it is done."));
        return;
    }
    m.speakersHint = qBound(0, count, 8);
    m_store.updateMeeting(m);
    QFile::remove(meetingDir(id) + QStringLiteral("/diarization.json")); // the speakers are found again; the recognized text is kept
    setError({});
    start(id, false, false);
}

void AudioController::loadWaveform(qlonglong id)
{
    const QString file = meetingDir(id) + QStringLiteral("/work.wav");
    if (!QFileInfo::exists(file))
        return;
    auto *w = new QFutureWatcher<QVariantList>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, id] {
        const QVariantList peaks = w->result();
        w->deleteLater();
        if (id == m_current) {
            m_waveform = peaks;
            emit waveformChanged();
        }
    });
    w->setFuture(QtConcurrent::run([file] {
        QVariantList out;
        for (float v : Wav::peaks(Wav::readFrames(file, 0), 240))
            out.append(double(v));
        return out;
    }));
}

void AudioController::rename(qlonglong id, const QString &title)
{
    AudioStore::Meeting m = m_store.meeting(id);
    const QString clean = title.simplified().left(120);
    if (!m.id || clean.isEmpty() || clean == m.title)
        return;
    m.title = clean;
    m_store.updateMeeting(m);
    reload();
    if (id == m_current)
        emit currentChanged();
}

void AudioController::remove(qlonglong id, bool deleteFiles)
{
    if (id == m_busy) {
        setError(tr("Stop the job first."));
        return;
    }
    m_queue.removeAll(id);
    if (!m_store.deleteMeeting(id)) {
        setError(tr("The meeting could not be deleted"));
        return;
    }
    if (deleteFiles)
        QDir(meetingDir(id)).removeRecursively();
    if (id == m_current)
        close();
    reload();
    emit historyChanged();
}

void AudioController::removeModel(const QString &id)
{
    if (!m_models.remove(id))
        setError(tr("This could not be deleted right now."));
}

// ---- editing the transcript ---------------------------------------------------------------------------------

void AudioController::renameSpeaker(const QString &label, const QString &name)
{
    if (!m_current || !m_store.renameSpeaker(m_current, label, name))
        return;
    const QString clean = name.simplified().left(60);
    m_transcript.refreshSpeaker(label, clean);
    for (QVariant &v : m_speakers) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("label")).toString() == label) {
            m.insert(QStringLiteral("name"), clean);
            v = m;
        }
    }
    computeHints();
    emit speakersChanged();
}

void AudioController::editSegment(qlonglong segmentId, const QString &text)
{
    const int row = m_transcript.rowOf(segmentId);
    if (row < 0 || !m_store.editSegment(segmentId, text.trimmed()))
        return;
    for (const AudioStore::SegmentRow &r : m_store.segments(m_current))
        if (r.id == segmentId)
            m_transcript.replaceRow(row, r);
}

void AudioController::deleteSegment(qlonglong segmentId)
{
    const int row = m_transcript.rowOf(segmentId);
    if (row >= 0 && m_store.deleteSegment(segmentId))
        m_transcript.removeRow(row);
}

void AudioController::setActionDone(qlonglong actionId, bool done)
{
    if (m_store.setActionDone(actionId, done))
        loadCurrent();
}

// ---- jobs ---------------------------------------------------------------------------------------------------

void AudioController::process(qlonglong id)
{
    const AudioStore::Meeting m = m_store.meeting(id);
    if (!m.id)
        return;
    if (m_busy && m_busy != id) {
        if (!m_queue.contains(id)) {
            m_queue << id;
            m_store.setStatus(id, QStringLiteral("queued"));
            reload();
        }
        return;
    }
    if (m_busy == id)
        return;
    setError({});
    // Notes that failed, or a transcript that is already there (maybe edited): only the notes are written again, from that transcript.
    start(id, m.segments > 0 || !m_store.segments(id).isEmpty());
}

void AudioController::regenerateNotes(qlonglong id, const QString &tmplId)
{
    AudioStore::Meeting m = m_store.meeting(id);
    if (!m.id || m_store.segments(id).isEmpty())
        return;
    m.tmpl = Audio::templateId(Audio::templateFrom(tmplId));
    m_store.updateMeeting(m);
    if (!m_sum.available()) {
        emit currentChanged();
        setError(tr("The notes need the language model and its program: download both under Summary, or in Settings → AI."));
        return;
    }
    if (m_busy && m_busy != id) {
        setError(tr("Another recording is being processed. Try again when it is done."));
        return;
    }
    if (m_busy == id)
        return;
    setError({});
    start(id, true);
}

void AudioController::start(qlonglong id, bool notesOnly, bool summarize)
{
    AudioStore::Meeting m = m_store.meeting(id);
    if (!notesOnly) {
        if (const QString problem = readinessProblem(m.quality); !problem.isEmpty()) {
            setError(problem);
            m_store.setStatus(id, QStringLiteral("ready")); // it may have been waiting in a batch
            reload();
            return;
        }
    }
    m_busy = id;
    m_cancel = false;
    m_stage.clear();
    m_stageFraction = 0;
    m_jobProgress = 0;
    m_doneStages.clear();
    m_warnings.clear();
    const qint64 jobId = m_store.startJob(id);
    m_store.setStatus(id, QStringLiteral("processing"));
    reload();
    emit historyChanged();
    m_list.setProgress(id, tr("Starting"), 0);
    emit jobChanged();
    if (id == m_current)
        emit currentChanged();

    Audio::PipelineOptions opt;
    opt.language = m.language.isEmpty() ? QStringLiteral("auto") : m.language;
    opt.quality = m.quality;
    opt.tmpl = Audio::templateFrom(m.tmpl);
    opt.stereo = m.stereo;
    opt.youLabel = tr("You");
    opt.remoteLabel = tr("Others");
    opt.localLabel = tr("Room");
    opt.speakers = m.speakersHint;
    opt.summarize = summarize;
    QList<Audio::Segment> existing;
    if (notesOnly)
        for (const AudioStore::SegmentRow &r : m_store.segments(id))
            existing.append({r.start, r.end, r.text(), r.who(), r.approx}); // what the person sees: edited text, the names they gave
    const QString dir = meetingDir(id);
    const QString audioPath = m.audioPath;
    Audio::PipelineHooks hooks;
    hooks.report = [this, id](const QString &stage, double f) {
        QMetaObject::invokeMethod(this, [this, id, stage, f] {
            if (m_busy != id)
                return;
            m_stage = stage;
            m_stageFraction = f;
            m_jobProgress = Audio::AudioPipeline::overall(stage, f);
            m_list.setProgress(id, Audio::AudioPipeline::stageLabel(stage), m_jobProgress);
            emit jobChanged();
        }, Qt::QueuedConnection);
    };
    if (!notesOnly) {
        hooks.transcript = [this, id](const QList<Audio::Segment> &segments, const QString &language, double duration) {
            QMetaObject::invokeMethod(this, [this, id, segments, language, duration] { applyTranscript(id, segments, language, duration); }, Qt::QueuedConnection);
        };
    }
    hooks.summary = [this, id, tmpl = m.tmpl](const Audio::Summary &s) {
        QMetaObject::invokeMethod(this, [this, id, s, tmpl] { applyNotes(id, s, tmpl); }, Qt::QueuedConnection);
    };
    m_pool.start([this, id, jobId, opt, existing, notesOnly, dir, audioPath, hooks] {
        Audio::AudioPipeline pipe(&m_pre, &m_asr, &m_diar, &m_sum);
        Audio::PipelineResult r;
        if (notesOnly) {
            r = pipe.notes(existing, dir, opt, m_cancel, hooks);
        } else {
            // the audio is kept in the meeting's own folder, so it survives the original being moved or deleted
            QString source = audioPath;
            const QString copy = dir + QStringLiteral("/source.") + QFileInfo(audioPath).suffix().toLower();
            QDir().mkpath(dir);
            if (QFileInfo(source).absoluteFilePath() != QFileInfo(copy).absoluteFilePath()) {
                if (!QFileInfo::exists(copy) || QFileInfo(copy).size() != QFileInfo(source).size()) {
                    QFile::remove(copy);
                    if (!QFile::copy(source, copy)) {
                        r.failedStage = QStringLiteral("preprocess");
                        r.error = QObject::tr("The audio file could not be copied (is the disk full, or was the file moved?)");
                    }
                }
                if (r.error.isEmpty()) {
                    source = copy;
                    QMetaObject::invokeMethod(this, [this, id, copy] {
                        AudioStore::Meeting mm = m_store.meeting(id);
                        mm.audioPath = copy;
                        m_store.updateMeeting(mm);
                    }, Qt::QueuedConnection);
                }
            }
            if (r.error.isEmpty())
                r = pipe.run(source, dir, opt, m_cancel, hooks);
        }
        QMetaObject::invokeMethod(this, [this, id, jobId, r] { finishJob(id, jobId, r); }, Qt::QueuedConnection);
    });
}

void AudioController::applyTranscript(qlonglong id, const QList<Audio::Segment> &segments, const QString &language, double duration)
{
    if (!m_store.replaceTranscript(id, segments)) {
        setError(tr("The transcript could not be saved"));
        return;
    }
    AudioStore::Meeting m = m_store.meeting(id);
    if (duration > 0)
        m.duration = duration;
    if (!language.isEmpty() && (m.language.isEmpty() || m.language == QLatin1String("auto")))
        m.language = language;
    m_store.updateMeeting(m);
    reload();
    if (id == m_current) {
        loadCurrent();
        emit currentChanged();
    }
}

void AudioController::applyNotes(qlonglong id, const Audio::Summary &s, const QString &tmplId)
{
    if (!m_store.saveSummary(id, s, tmplId)) {
        setError(tr("The notes could not be saved"));
        return;
    }
    reload();
    if (id == m_current) {
        loadCurrent();
        emit currentChanged();
    }
}

void AudioController::finishJob(qlonglong id, qint64 jobId, const Audio::PipelineResult &r)
{
    QString message;
    if (!r.ok && !r.cancelled)
        message = Audio::AudioPipeline::stageLabel(r.failedStage) + QStringLiteral(": ") + r.error;
    m_store.finishJob(jobId, r.ok ? QString() : (r.cancelled ? tr("Stopped") : message));
    m_store.setStatus(id, r.ok ? QStringLiteral("completed") : QStringLiteral("failed"));
    m_warnings = r.warnings;
    m_busy = 0;
    if (m_batchTotal > 0 && m_batchDone < m_batchTotal)
        ++m_batchDone;
    m_stage.clear();
    m_list.clearProgress();
    reload();
    if (id == m_current) {
        loadCurrent();
        loadWaveform(id);
        emit currentChanged();
    }
    if (!message.isEmpty())
        setError(message);
    emit jobChanged();
    emit historyChanged();
    emit finished(id, r.ok);
    next();
}

void AudioController::next()
{
    while (!m_queue.isEmpty() && !m_busy) {
        const qlonglong id = m_queue.takeFirst();
        if (m_store.meeting(id).id)
            process(id);
    }
}

void AudioController::cancel()
{
    m_cancel = true;
    for (const qlonglong id : std::as_const(m_queue)) // the ones still waiting go back to ready
        m_store.setStatus(id, QStringLiteral("ready"));
    m_batchTotal = qMax(0, m_batchTotal - int(m_queue.size()));
    m_queue.clear();
    reload();
    emit jobChanged();
}

int AudioController::importMany(const QStringList &paths, const QString &language, const QString &quality, const QString &tmplId, int speakers)
{
    if (!m_busy && m_queue.isEmpty())
        m_batchTotal = m_batchDone = 0; // a new batch
    QList<qlonglong> ids;
    for (const QString &path : paths)
        if (const qlonglong id = importFile(path, {}, language, quality, tmplId, false, speakers)) // the title comes from the file name
            ids << id;
    m_batchTotal += int(ids.size());
    for (const qlonglong id : std::as_const(ids)) // in the order given: the first starts (if nothing runs), the rest wait
        process(id);
    emit jobChanged();
    return int(ids.size());
}

QVariantList AudioController::history() const
{
    QVariantList out;
    for (const AudioStore::HistoryRow &r : m_store.history()) {
        const bool running = !r.finished.isValid();
        const QString state = running ? QStringLiteral("running") : r.error.isEmpty() ? QStringLiteral("done")
                              : r.error == tr("Stopped") ? QStringLiteral("stopped") : QStringLiteral("failed");
        const qint64 secs = running ? 0 : qMax<qint64>(0, r.started.secsTo(r.finished));
        out.append(QVariantMap{{QStringLiteral("jobId"), r.jobId}, {QStringLiteral("meetingId"), r.meetingId}, {QStringLiteral("title"), r.title},
                               {QStringLiteral("state"), state}, {QStringLiteral("text"), r.error},
                               {QStringLiteral("when"), QLocale().toString(r.started.toLocalTime(), QLocale::ShortFormat)},
                               {QStringLiteral("took"), running ? QString() : secs >= 60 ? tr("%1 min %2 s").arg(secs / 60).arg(secs % 60) : tr("%1 s").arg(secs)}});
    }
    return out;
}

void AudioController::clearHistory()
{
    m_store.clearHistory();
    emit historyChanged();
}

// ---- recording ----------------------------------------------------------------------------------------------

void AudioController::startRecording()
{
    if (m_recorder.recording())
        return;
    setError({});
    m_recordingDir = NotchPaths::dataDir(QStringLiteral("audio/recording"));
    const QString err = m_recorder.start(m_recordingDir);
    if (!err.isEmpty()) {
        setError(err);
        return;
    }
    m_tick.start();
    emit recordingChanged();
}

void AudioController::startLive()
{
    if (m_recorder.recording())
        return;
    if (m_busy != 0 || !m_queue.isEmpty()) {
        setError(tr("A recording is being processed. Live transcription needs the processor: wait for it to finish."));
        return;
    }
    m_live = std::make_unique<LiveSession>(&m_models, [this] { return threads(); });
    connect(&m_recorder, &MeetingRecorder::micAudio, m_live.get(), [this](const QByteArray &c, qint64 ms) { m_live->audio(0, c, ms); });
    connect(&m_recorder, &MeetingRecorder::sysAudio, m_live.get(), [this](const QByteArray &c, qint64 ms) { m_live->audio(1, c, ms); });
    startRecording();
    if (!m_recorder.recording())
        m_live.reset();
    emit liveChanged();
}

void AudioController::stopRecording(bool processIt)
{
    if (!m_recorder.recording())
        return;
    m_tick.stop();
    QString wav;
    const double seconds = m_recorder.seconds();
    const QString err = m_recorder.stop(&wav);
    emit recordingChanged();
    std::unique_ptr<LiveSession> live = std::move(m_live); // the live transcript becomes the first transcript of the recording
    if (live)
        live->finish();
    emit liveChanged();
    if (!err.isEmpty()) {
        setError(err);
        return;
    }
    AudioStore::Meeting m;
    m.title = tr("Recording %1").arg(QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat));
    m.audioPath = wav;
    m.duration = seconds;
    m.language = language();
    m.quality = quality();
    m.tmpl = tmpl();
    m.source = QStringLiteral("recording");
    m.stereo = true;
    const qint64 id = m_store.addMeeting(m);
    if (!id) {
        setError(tr("The recording could not be saved"));
        return;
    }
    const QString dir = meetingDir(id), target = dir + QStringLiteral("/source.wav");
    QDir().mkpath(dir);
    if (QFile::rename(wav, target) || QFile::copy(wav, target)) {
        QFile::remove(wav);
        m.id = id;
        m.audioPath = target;
        m.duration = Wav::readInfo(target).seconds();
        m_store.updateMeeting(m);
    }
    if (live && !live->segments().isEmpty())
        m_store.replaceTranscript(id, live->segments());
    reload();
    if (processIt)
        process(id);
}

void AudioController::discardRecording()
{
    m_tick.stop();
    m_live.reset();
    emit liveChanged();
    m_recorder.discard();
    emit recordingChanged();
}
