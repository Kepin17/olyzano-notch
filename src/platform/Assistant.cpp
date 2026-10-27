#include "Assistant.h"

#include "app/NotchController.h"
#include "models/FileItem.h"
#include "platform/Activities.h"
#include "platform/Launcher.h"
#include "platform/MediaPlayers.h"
#include "platform/QuickSettings.h"
#include "platform/Tasks.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

namespace {

struct ModelInfo {
    const char *id, *file, *sha256;
    qint64 size;
};
// ggml-org/whisper.cpp on Hugging Face; the size and the SHA-256 are what the download must come out as.
const ModelInfo Models[] = {
    {"tiny", "ggml-tiny.bin", "be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21", 77691713},
    {"base", "ggml-base.bin", "60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe", 147951465},
};

const ModelInfo &info(const QString &id)
{
    return id == QLatin1String("tiny") ? Models[0] : Models[1];
}

QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/assistant.conf");
}

QString duration(int seconds)
{
    if (seconds % 3600 == 0)
        return Assistant::tr("%n hour(s)", nullptr, seconds / 3600);
    if (seconds >= 3600)
        return Assistant::tr("%1 h %2 min").arg(seconds / 3600).arg(seconds % 3600 / 60);
    if (seconds % 60 == 0)
        return Assistant::tr("%n min", nullptr, seconds / 60);
    return Assistant::tr("%n sec", nullptr, seconds);
}

}

const QStringList &Assistant::hints()
{
    static const QStringList h{tr("open YouTube"), tr("search cats on YouTube"), tr("search channel Nadia"), tr("open the browser"), tr("volume 40"),
                               tr("timer 10 minutes"), tr("pause the music"), tr("add task buy milk")};
    return h;
}

Assistant::Assistant(Launcher *launcher, Tasks *tasks, QuickSettings *quick, MediaPlayers *media, Activities *activities,
                     NotchController *notch, QObject *parent)
    : QObject(parent), m_launcher(launcher), m_tasks(tasks), m_quick(quick), m_media(media), m_activities(activities), m_notch(notch),
      m_settings(configPath(), QSettings::IniFormat)
{
    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(3500);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] { if (m_state == QLatin1String("done") || m_state == QLatin1String("error")) setState(QStringLiteral("idle")); });
    m_hintTimer.setInterval(2600);
    connect(&m_hintTimer, &QTimer::timeout, this, [this] { ++m_hint; emit hintChanged(); });
    connect(&m_mic, &MicCapture::chunk, this, &Assistant::onChunk);
    connect(&m_mic, &MicCapture::failed, this, [this](const QString &why) {
        if (m_state == QLatin1String("listening"))
            finish(QStringLiteral("error"), QString(), why);
    });
    connect(&m_watcher, &QFutureWatcher<Whisper::Transcript>::finished, this, [this] {
        const Whisper::Transcript t = m_watcher.result();
        if (m_state != QLatin1String("thinking"))
            return; // cancelled meanwhile
        if (t.text.isEmpty())
            finish(QStringLiteral("error"), QString(), tr("I could not make out the words."));
        else
            act(t.text, t.confidence);
    });
    checkModel();
}

Assistant::~Assistant()
{
    m_mic.stop();
    if (m_reply)
        m_reply->abort();
    m_watcher.waitForFinished();
}

void Assistant::store(const QString &key, const QVariant &v)
{
    if (m_settings.value(key) == v)
        return;
    m_settings.setValue(key, v);
    emit settingsChanged();
}

void Assistant::setModel(const QString &m)
{
    store(QStringLiteral("model"), m == QLatin1String("tiny") ? QStringLiteral("tiny") : QStringLiteral("base"));
    checkModel();
}

QString Assistant::language() const
{
    const QString l = m_settings.value(QStringLiteral("language")).toString();
    return l == QLatin1String("id") || l == QLatin1String("en") ? l : QStringLiteral("auto");
}

QString Assistant::searchEngine() const
{
    const QString e = m_settings.value(QStringLiteral("searchEngine")).toString();
    return e == QLatin1String("google") || e == QLatin1String("bing") || e == QLatin1String("youtube") ? e : QStringLiteral("duckduckgo");
}

QString Assistant::modelPath(const QString &model)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/notch/models/")
           + QString::fromLatin1(info(model).file);
}

QString Assistant::modelSizeText() const
{
    return tr("%1 MB").arg(info(model()).size / 1000000);
}

QString Assistant::modelState() const
{
    return m_reply ? QStringLiteral("downloading") : m_modelReady ? QStringLiteral("ready") : QStringLiteral("missing");
}

void Assistant::checkModel()
{
    const QFileInfo f(modelPath(model()));
    const bool ready = f.exists() && f.size() == info(model()).size;
    if (ready != m_modelReady) {
        m_modelReady = ready;
        if (!ready) { // the file is gone: the loaded copy goes with it
            std::lock_guard<std::mutex> lock(m_whisperMutex);
            m_whisper.unload();
            m_loadedModel.clear();
        }
    }
    emit modelChanged();
}

void Assistant::setState(const QString &s)
{
    if (s == m_state)
        return;
    m_state = s;
    if (s != QLatin1String("listening") && m_level != 0) {
        m_level = 0;
        emit levelChanged();
    }
    if (s == QLatin1String("listening"))
        m_hintTimer.start();
    else
        m_hintTimer.stop();
    if (s == QLatin1String("done") || s == QLatin1String("error"))
        m_idleTimer.start();
    else
        m_idleTimer.stop();
    emit stateChanged();
}

void Assistant::finish(const QString &state, const QString &heard, const QString &result)
{
    m_mic.stop();
    m_heard = heard;
    m_result = result;
    emit resultChanged();
    setState(state);
}

void Assistant::start()
{
    if (m_state == QLatin1String("listening") || m_state == QLatin1String("thinking"))
        return cancel(); // pressing the mic again is "never mind"
    if (!available())
        return finish(QStringLiteral("error"), QString(), tr("This build has no offline voice assistant."));
    if (!m_modelReady)
        return finish(QStringLiteral("error"), QString(), tr("Download the voice model in Settings first."));
    m_pcm.clear();
    m_clock.start();
    m_tracker.sensitivity = m_activities ? m_activities->micSensitivity() : 1;
    m_tracker.reset(0);
    m_heard.clear();
    m_result = tr("Listening…");
    emit resultChanged();
    setState(QStringLiteral("listening"));
    if (!m_mic.start())
        finish(QStringLiteral("error"), QString(), tr("The microphone could not be opened."));
}

void Assistant::cancel()
{
    m_mic.stop();
    m_heard.clear();
    m_result.clear();
    emit resultChanged();
    setState(QStringLiteral("idle"));
}

void Assistant::onChunk(const QByteArray &s16le)
{
    if (m_state != QLatin1String("listening"))
        return;
    const double rms = VoiceModel::rms(s16le);
    const qreal level = qMin<qreal>(1.0, rms / 6000.0);
    if (!qFuzzyCompare(1 + level, 1 + m_level)) {
        m_level = level;
        emit levelChanged();
    }
    const int n = s16le.size() / 2;
    const auto *p = reinterpret_cast<const qint16 *>(s16le.constData());
    for (int i = 0; i < n; ++i)
        m_pcm.append(p[i] / 32768.0f);
    const auto before = m_tracker.state();
    m_tracker.update(rms, m_clock.elapsed());
    if (m_tracker.state() == UtteranceModel::State::Waiting && before == UtteranceModel::State::Waiting) {
        constexpr int PreRoll = 16000 * 6 / 10; // before a voice, only the last 0.6 s is kept
        if (m_pcm.size() > PreRoll)
            m_pcm.remove(0, m_pcm.size() - PreRoll);
    }
    if (!m_tracker.done())
        return;
    m_mic.stop();
    if (m_tracker.state() == UtteranceModel::State::NoSpeech)
        return finish(QStringLiteral("error"), QString(), tr("I did not hear anything."));
    setState(QStringLiteral("thinking"));
    m_result = tr("Thinking…");
    emit resultChanged();
    const QString model = this->model(), lang = language(), path = modelPath(model), vocab = vocabulary();
    QVector<float> pcm = std::move(m_pcm);
    m_pcm.clear();
    m_watcher.setFuture(QtConcurrent::run([this, pcm, model, lang, path, vocab]() -> Whisper::Transcript {
        std::lock_guard<std::mutex> lock(m_whisperMutex);
        if (m_loadedModel != model || !m_whisper.loaded()) {
            if (!m_whisper.load(path))
                return {};
            m_loadedModel = model;
        }
        return m_whisper.hear(pcm, lang, vocab);
    }));
}

QString Assistant::transcribeFile(const QString &wavPath)
{
    QFile f(wavPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray all = f.readAll();
    const int at = all.indexOf("data");
    if (at < 0 || all.size() < at + 8)
        return {};
    const QByteArray pcm = all.mid(at + 8);
    QVector<float> samples;
    samples.reserve(pcm.size() / 2);
    const auto *p = reinterpret_cast<const qint16 *>(pcm.constData());
    for (int i = 0; i < pcm.size() / 2; ++i)
        samples.append(p[i] / 32768.0f);
    std::lock_guard<std::mutex> lock(m_whisperMutex);
    if (!m_whisper.loaded() && !m_whisper.load(modelPath(model())))
        return {};
    m_loadedModel = model();
    const Whisper::Transcript t = m_whisper.hear(samples, language(), vocabulary());
    m_confidence = t.confidence;
    return t.text;
}

void Assistant::debugShow(const QString &state, qreal level, const QString &heard, const QString &result)
{
    m_heard = heard;
    m_result = result;
    m_level = level;
    emit resultChanged();
    emit levelChanged();
    m_state.clear();
    m_idleTimer.stop();
    setState(state);
    m_idleTimer.stop(); // for a picture it stays
}

void Assistant::say(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    act(text);
}

// The names the recognizer should expect: the apps (the ones used most first) and the best-known sites, so a name is spelled the
// way it is written on the screen.
QString Assistant::vocabulary() const
{
    QStringList names;
    if (m_launcher) {
        struct Named { QString name; int uses; };
        QList<Named> apps;
        for (const QVariant &a : m_launcher->search(QString(), QString())) {
            const QVariantMap m = a.toMap();
            const QString n = m.value(QStringLiteral("name")).toString();
            if (!n.isEmpty() && n.size() <= 28)
                apps << Named{n, m_launcher->uses(m.value(QStringLiteral("id")).toString())};
        }
        std::stable_sort(apps.begin(), apps.end(), [](const Named &a, const Named &b) { return a.uses > b.uses; });
        for (int i = 0; i < apps.size() && i < 24; ++i)
            names << apps.at(i).name;
    }
    names << QStringLiteral("YouTube") << QStringLiteral("Google") << QStringLiteral("GitHub") << QStringLiteral("Gmail");
    return names.join(QStringLiteral(", "));
}

void Assistant::act(const QString &text, double confidence)
{
    m_confidence = confidence;
    const AssistantModel::Intent in = AssistantModel::parse(text);
    if (in.kind == AssistantModel::Kind::Unknown)
        return finish(QStringLiteral("error"), text, tr("Not understood. Try: \"%1\"").arg(hints().at(m_hint % hints().size())));
    finish(QStringLiteral("done"), text, run(in));
}

QString Assistant::describe(const AssistantModel::Intent &in) const
{
    using K = AssistantModel::Kind;
    switch (in.kind) {
    case K::OpenApp: return tr("open the app \"%1\"").arg(in.text);
    case K::OpenBrowser: return tr("open the browser");
    case K::OpenSite: return tr("open %1").arg(in.text);
    case K::Search: return tr("search for \"%1\"%2%3").arg(in.text, in.engine.isEmpty() ? QString() : tr(" on %1").arg(in.engine), in.filter.isEmpty() ? QString() : tr(" (%1)").arg(in.filter));
    case K::Volume: return tr("volume (%1)").arg(in.number);
    case K::Media: return tr("music control");
    case K::Timer: return tr("set a timer for %1").arg(duration(in.number));
    case K::Task: return tr("add the task \"%1\"").arg(in.text);
    case K::Note: return tr("save the note \"%1\"").arg(in.text);
    default: return tr("nothing");
    }
}

QString Assistant::run(const AssistantModel::Intent &in)
{
    using namespace AssistantModel;
    switch (in.kind) {
    case Kind::OpenApp: {
        const QVariantList list = m_launcher ? m_launcher->search(QString(), QString()) : QVariantList();
        QList<AppInfo> apps;
        for (const QVariant &a : list) {
            const QVariantMap m = a.toMap();
            apps << AppInfo{m.value(QStringLiteral("name")).toString(), m.value(QStringLiteral("keywords")).toString(),
                            m_launcher->uses(m.value(QStringLiteral("id")).toString())};
        }
        const AppMatch m = matchApp(in.text, apps);
        if (m.index < 0 || m.score < 0.6)
            return tr("I could not find an app called \"%1\".").arg(in.text);
        const QString name = apps.at(m.index).name;
        if (!m.sure && m.score < 0.85)
            return tr("Not sure which app: %1?").arg(name);
        m_launcher->launch(list.at(m.index).toMap().value(QStringLiteral("id")).toString());
        return tr("Opening %1").arg(name);
    }
    case Kind::OpenBrowser: {
        QUrl home = searchUrl(searchEngine(), QString());
        home.setQuery(QString());
        QDesktopServices::openUrl(home); // the browser the system has chosen, never one picked here
        return tr("Opening the browser");
    }
    case Kind::OpenSite:
        QDesktopServices::openUrl(QUrl(in.text)); // in the browser the system has chosen
        return tr("Opening %1").arg(QUrl(in.text).host().remove(QStringLiteral("www.")));
    case Kind::Search: {
        const QString engine = in.engine.isEmpty() ? searchEngine() : in.engine;
        QDesktopServices::openUrl(searchUrl(engine, in.text, in.filter));
        return in.engine.isEmpty() ? tr("Searching for \"%1\"").arg(in.text) : tr("Searching %1 for \"%2\"").arg(engine, in.text);
    }
    case Kind::Volume: {
        if (!m_quick || !m_quick->audioAvailable())
            return tr("Volume control is not available here.");
        switch (in.volume) {
        case VolumeAction::Mute:
            if (!m_quick->muted())
                m_quick->toggleMute();
            return tr("Muted");
        case VolumeAction::Unmute:
            if (m_quick->muted())
                m_quick->toggleMute();
            return tr("Sound on");
        case VolumeAction::Up:
            m_quick->setVolume(m_quick->volume() + in.number);
            return tr("Volume up");
        case VolumeAction::Down:
            m_quick->setVolume(m_quick->volume() - in.number);
            return tr("Volume down");
        default:
            m_quick->setVolume(in.number);
            return tr("Volume %1%").arg(qBound(0, in.number, 100));
        }
    }
    case Kind::Media:
        if (!m_media || !m_media->active())
            return tr("Nothing is playing.");
        switch (in.media) {
        case MediaAction::Next: m_media->next(); return tr("Next song");
        case MediaAction::Previous: m_media->previous(); return tr("Previous song");
        case MediaAction::Pause:
            if (m_media->playing())
                m_media->playPause();
            return tr("Paused");
        default:
            if (!m_media->playing())
                m_media->playPause();
            return tr("Playing");
        }
    case Kind::Timer:
        m_activities->startTimer(in.number, in.text);
        return in.text.isEmpty() ? tr("Timer: %1").arg(duration(in.number)) : tr("Timer: %1 (%2)").arg(duration(in.number), in.text);
    case Kind::Task:
        return m_tasks->addQuick(in.text) ? tr("Task added: %1").arg(in.text) : tr("I did not catch the task.");
    case Kind::Note:
        m_notch->addTextNote(in.text);
        return tr("Note saved");
    default:
        return tr("I did not understand that.");
    }
}

// ---- the model file ----
void Assistant::downloadModel()
{
    if (m_reply || m_modelReady)
        return;
    const ModelInfo &mi = info(model());
    QDir().mkpath(QFileInfo(modelPath(model())).absolutePath());
    m_part = std::make_unique<QFile>(modelPath(model()) + QStringLiteral(".part"));
    if (!m_part->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_modelError = tr("The model cannot be saved here.");
        return emit modelChanged();
    }
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_modelError.clear();
    m_progress = 0;
    QNetworkRequest req(QUrl(QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/") + QString::fromLatin1(mi.file)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OlyzanoNotch/") + QStringLiteral(NOTCH_VERSION));
    m_reply = m_net.get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &Assistant::downloadChunk);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        const qint64 want = total > 0 ? total : info(model()).size;
        m_progress = qBound<qreal>(0, qreal(got) / want, 1);
        emit modelChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, &Assistant::downloadDone);
    emit modelChanged();
}

void Assistant::downloadChunk()
{
    if (!m_reply || !m_part)
        return;
    const QByteArray data = m_reply->readAll();
    m_hash->addData(data);
    m_part->write(data);
}

void Assistant::downloadDone()
{
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    if (!r)
        return;
    const QByteArray rest = r->readAll();
    m_hash->addData(rest);
    m_part->write(rest);
    m_part->close();
    const bool ok = r->error() == QNetworkReply::NoError;
    const QString err = r->errorString();
    r->deleteLater();
    const QString path = modelPath(model());
    const ModelInfo &mi = info(model());
    if (!ok) {
        QFile::remove(path + QStringLiteral(".part"));
        m_modelError = r->error() == QNetworkReply::OperationCanceledError ? QString() : tr("The download failed: %1").arg(err);
    } else if (QFileInfo(path + QStringLiteral(".part")).size() != mi.size
               || QString::fromLatin1(m_hash->result().toHex()) != QLatin1String(mi.sha256)) {
        QFile::remove(path + QStringLiteral(".part"));
        m_modelError = tr("The downloaded file is not the model (the checksum does not match).");
    } else {
        QFile::remove(path);
        QFile::rename(path + QStringLiteral(".part"), path);
        m_modelError.clear();
    }
    m_part.reset();
    checkModel();
}

void Assistant::cancelDownload()
{
    if (m_reply)
        m_reply->abort();
}

void Assistant::removeModel()
{
    if (m_reply)
        return;
    QFile::remove(modelPath(model()));
    checkModel();
}
