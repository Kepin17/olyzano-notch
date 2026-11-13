#include "AudioEngines.h"

#include "EngineParse.h"
#include "Hardware.h"
#include "ModelBundles.h"
#include "Summarize.h"
#include "SystemMemory.h"
#include "platform/ChildLife.h"

#include <QDir>
#include <cmath>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>
#include <QTimer>

namespace Audio {
namespace {
QString tail(QString text)
{
    text = text.trimmed();
    const QStringList lines = text.split(u'\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.mid(qMax(0, int(lines.size()) - 3)).join(QStringLiteral(" | ")).left(300);
}

QString memoryWarning(qint64 needed, const QString &what)
{
    const qint64 have = availableMemory();
    if (needed > 0 && have >= 0 && have < needed)
        return QObject::tr("Not enough free memory for %1: %2 are needed and %3 are free. Close other programs, or choose the standard quality.")
            .arg(what, QLocale().formattedDataSize(needed), QLocale().formattedDataSize(have));
    return {};
}
}

// ---- ffmpeg ----------------------------------------------------------------------------------

QString FfmpegPreprocessor::program() { return QStandardPaths::findExecutable(QStringLiteral("ffmpeg")); }

QString FfmpegPreprocessor::convert(const QString &source, const QString &wavOut, const std::function<void(double)> &progress, const std::atomic_bool *cancel)
{
    const QString ffmpeg = program();
    if (ffmpeg.isEmpty()) {
#ifdef Q_OS_WIN
        return QObject::tr("ffmpeg is needed to read this kind of file. Install it (winget install ffmpeg) and open Notch again, or use a WAV file.");
#else
        return QObject::tr("ffmpeg is needed to read this kind of file. Install it (sudo apt install ffmpeg), or use a WAV file.");
#endif
    }
    double total = -1;
    {
        QProcess probe;
        probe.setProcessChannelMode(QProcess::MergedChannels);
        probe.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-i"), source});
        if (probe.waitForFinished(20000))
            total = parseFfmpegDuration(QString::fromUtf8(probe.readAll()));
        else
            probe.kill();
    }
    QProcess p;
    p.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y"), QStringLiteral("-i"), source,
                     QStringLiteral("-map"), QStringLiteral("0:a:0"), QStringLiteral("-vn"), QStringLiteral("-ac"), QStringLiteral("1"), QStringLiteral("-ar"), QStringLiteral("16000"),
                     QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats"), wavOut});
    if (!p.waitForStarted(10000))
        return QObject::tr("ffmpeg could not be started");
    QByteArray pending;
    while (p.state() != QProcess::NotRunning) {
        if (cancel && cancel->load()) {
            p.kill();
            p.waitForFinished(3000);
            QFile::remove(wavOut);
            return QStringLiteral("cancelled");
        }
        if (p.waitForReadyRead(200)) {
            pending += p.readAllStandardOutput();
            int nl;
            while ((nl = pending.indexOf('\n')) >= 0) {
                const double t = parseFfmpegProgress(QString::fromUtf8(pending.left(nl)));
                pending.remove(0, nl + 1);
                if (t >= 0 && total > 0 && progress)
                    progress(qMin(1.0, t / total));
            }
        }
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString why = tail(QString::fromUtf8(p.readAllStandardError()));
        QFile::remove(wavOut);
        return why.contains(QLatin1String("matches no streams")) ? QObject::tr("This file has no audio")
                                                                  : QObject::tr("ffmpeg could not read this file: %1").arg(why);
    }
    return {};
}

// ---- whisper.cpp -------------------------------------------------------------------------------

QString WhisperAsr::modelFor(const QString &quality)
{
    return quality == QLatin1String("max") ? QStringLiteral("whisper-large-v3") : quality == QLatin1String("high") ? QStringLiteral("whisper-turbo") : QStringLiteral("whisper-small");
}

QString WhisperAsr::prepare(const QString &quality)
{
    if (!Whisper::compiled())
        return QObject::tr("This build of Notch has no speech recognition");
    const QString id = modelFor(quality);
    const Audio::HwPlan plan = Audio::Hardware::instance().plan(!m_store->path(LlamaSession::gpuHelperId()).isEmpty());
    const bool wantGpu = plan.whisperGpu && !Whisper::gpus().isEmpty(); // a build without GPU support has none to offer
    if (m_loaded == id && m_whisper.loaded() && m_gpu == wantGpu)
        return {};
    const QString path = m_store->path(id);
    if (path.isEmpty())
        return QObject::tr("The speech model (%1) is not installed. Download it in Settings → AI.").arg(m_store->item(id).title);
    if (const QString why = memoryWarning(m_store->item(id).memory, m_store->item(id).title); !why.isEmpty())
        return why;
    if (!m_whisper.load(path, wantGpu))
        return QObject::tr("The speech model could not be loaded: the file may be damaged. Delete it in Settings and download it again.");
    if (wantGpu && m_whisper.fellBack())
        Audio::Hardware::instance().markFailed(QStringLiteral("whisper"), QObject::tr("The speech model could not use the graphics card; it runs on the processor."));
    m_loaded = id;
    m_gpu = wantGpu && !m_whisper.fellBack();
    Audio::Hardware::instance().report(QStringLiteral("speech"), m_whisper.backend());
    return {};
}

QString WhisperAsr::transcribe(const QVector<float> &samples, const QString &language, const std::atomic_bool *cancel, QList<Segment> *segments, QString *detectedLanguage)
{
    QList<Whisper::Piece> pieces;
    const QString err = m_whisper.transcribeSegments(samples, Lang::known(language) ? Lang::whisperCode(language) : language, m_threads(), cancel, &pieces, detectedLanguage, m_languages ? m_languages() : QStringList(), m_fast);
    if (!err.isEmpty())
        return err;
    for (const Whisper::Piece &p : std::as_const(pieces))
        if (p.noSpeech < 0.85f && !p.text.isEmpty())
            segments->append({p.start, p.end, p.text, {}, false});
    return {};
}

// ---- sherpa-onnx diarization ---------------------------------------------------------------------

QString SherpaDiarizer::helperId()
{
#ifdef Q_OS_WIN
    return QStringLiteral("sherpa-windows");
#else
    return QStringLiteral("sherpa-linux");
#endif
}

bool SherpaDiarizer::available() const
{
    return !m_store->path(helperId()).isEmpty() && !m_store->path(QStringLiteral("diar-segmentation")).isEmpty() && !m_store->path(QStringLiteral("diar-embedding")).isEmpty();
}

QString SherpaDiarizer::diarize(const QString &wavPath, const std::atomic_bool *cancel, QList<Turn> *turns, int speakers)
{
    if (!available())
        return QObject::tr("The speaker model is not installed");
    const QString program = m_store->path(helperId()), folder = m_store->folder(helperId());
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString libs = QDir(folder).absoluteFilePath(QStringLiteral("../lib"));
#ifdef Q_OS_WIN
    env.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(folder) + u';' + QDir::toNativeSeparators(libs) + u';' + env.value(QStringLiteral("PATH")));
#else
    env.insert(QStringLiteral("LD_LIBRARY_PATH"), folder + u':' + libs + u':' + env.value(QStringLiteral("LD_LIBRARY_PATH")));
#endif
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(folder);
    ChildLife::dieWithParent(p);
    const QString threads = QString::number(m_threads());
    QStringList cluster = {QStringLiteral("--clustering.cluster-threshold=") + QString::number(m_threshold(), 'f', 2)};
    if (speakers >= 2) // the number of people is known: no guessing from a threshold
        cluster = {QStringLiteral("--clustering.num-clusters=%1").arg(speakers)};
    p.start(program, cluster + QStringList{
                      QStringLiteral("--segmentation.pyannote-model=") + m_store->path(QStringLiteral("diar-segmentation")),
                      QStringLiteral("--embedding.model=") + m_store->path(QStringLiteral("diar-embedding")),
                      QStringLiteral("--segmentation.num-threads=") + threads, QStringLiteral("--embedding.num-threads=") + threads,
                      QStringLiteral("--segmentation.pyannote-window-shift-ratio=0.1"), // the default, said here: less shift = more overlap = better, slower
                      QStringLiteral("--min-duration-on=0.25"), QStringLiteral("--min-duration-off=0.4"), wavPath});
    if (!p.waitForStarted(10000))
        return QObject::tr("The speaker program could not be started");
    QByteArray output;
    while (p.state() != QProcess::NotRunning) {
        if (cancel && cancel->load()) {
            p.kill();
            p.waitForFinished(3000);
            return QStringLiteral("cancelled");
        }
        if (p.waitForReadyRead(300))
            output += p.readAll();
    }
    output += p.readAll();
    const QString text = QString::fromUtf8(output);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return QObject::tr("The speaker program failed: %1").arg(tail(text));
    *turns = parseDiarization(text);
    return turns->isEmpty() ? QObject::tr("The speaker program found no speech") : QString();
}

// ---- llama.cpp server ----------------------------------------------------------------------------

QString getBlocking(const QUrl &url, const QByteArray &token, int timeoutMs, QByteArray *answer, int *status)
{
    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest req(url);
    if (!token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *r = nam.get(req);
    QEventLoop loop;
    QTimer::singleShot(timeoutMs, &loop, [&] { r->abort(); });
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    *status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    *answer = r->readAll();
    const QString err = r->error() == QNetworkReply::NoError ? QString() : r->errorString();
    delete r;
    return err;
}

QString postJsonBlocking(const QUrl &url, const QByteArray &token, const QByteArray &body, int timeoutMs, const std::atomic_bool *cancel, QByteArray *answer, int *status)
{
    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(0);
    if (!token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *r = nam.post(req, body);
    QEventLoop loop;
    QElapsedTimer clock;
    clock.start();
    QTimer poll;
    poll.setInterval(200);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if ((cancel && cancel->load()) || clock.elapsed() > timeoutMs)
            r->abort();
    });
    poll.start();
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    *status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    *answer = r->readAll();
    const bool aborted = cancel && cancel->load();
    const QString err = aborted ? QStringLiteral("cancelled") : r->error() == QNetworkReply::NoError ? QString() : r->errorString();
    delete r;
    return err;
}

QString LlamaSummarizer::helperId()
{
#ifdef Q_OS_WIN
    return QStringLiteral("llama-server-windows");
#else
    return QStringLiteral("llama-server-linux");
#endif
}

bool LlamaSummarizer::available() const { return !m_store->path(helperId()).isEmpty() && !m_store->path(QStringLiteral("qwen3-4b")).isEmpty(); }

bool LlamaSession::available(const ModelStore *store) { return !store->path(LlamaSummarizer::helperId()).isEmpty() && !store->path(textModelId(store)).isEmpty(); }

QString LlamaSession::textModelId(const ModelStore *store)
{
    Audio::Hardware &hw = Audio::Hardware::instance();
    const QString choice = hw.notesModel();
    if (choice == QLatin1String("standard") || !hw.detected())
        return QStringLiteral("qwen3-4b");
    const QString id = choice == QLatin1String("xl") ? QStringLiteral("qwen3-14b") : QStringLiteral("qwen3-8b");
    const Audio::Bundles::Feature f = Audio::Bundles::feature(choice == QLatin1String("xl") ? QStringLiteral("notes-xl") : QStringLiteral("notes-large"));
    if (store->path(id).isEmpty() || store->path(gpuHelperId()).isEmpty())
        return QStringLiteral("qwen3-4b");
    return Audio::largeModelSupport(hw.known(), true, Audio::hwSettingOf(hw.setting()), hw.bench(), f.gpuNeed).ok ? id : QStringLiteral("qwen3-4b");
}

void LlamaSession::stop()
{
    if (m_server.state() != QProcess::NotRunning) {
        m_server.terminate();
        if (!m_server.waitForFinished(4000)) {
            m_server.kill();
            m_server.waitForFinished(3000);
        }
    }
}

bool LlamaSession::embedAvailable(const ModelStore *store)
{
    return !store->path(LlamaSummarizer::helperId()).isEmpty() && !store->path(QStringLiteral("embed-qwen3")).isEmpty();
}

bool LlamaSession::visionAvailable(const ModelStore *store)
{
    return !store->path(LlamaSummarizer::helperId()).isEmpty() && !store->path(QStringLiteral("vision-qwen3vl")).isEmpty() && !store->path(QStringLiteral("vision-mmproj")).isEmpty();
}

QString LlamaSession::gpuHelperId()
{
#ifdef Q_OS_WIN
    return QStringLiteral("gpu-llama-windows");
#else
    return QStringLiteral("gpu-llama-linux");
#endif
}

// On a graphics card when the hardware plan says so (and the Vulkan program is installed); when that does not start, on the processor, and
// the failure is remembered so that the card is not tried again at every start.
QString LlamaSession::start(const std::atomic_bool *cancel)
{
    QString modelId = m_embed ? QStringLiteral("embed-qwen3") : m_vision ? QStringLiteral("vision-qwen3vl") : textModelId(m_store);
    if (m_embed ? !embedAvailable(m_store) : m_vision ? !visionAvailable(m_store) : !available(m_store))
        return m_embed ? QObject::tr("The search model is not installed") : m_vision ? QObject::tr("The picture model is not installed") : QObject::tr("The language model is not installed");
    const bool large = !m_embed && !m_vision && modelId != QLatin1String("qwen3-4b");
    if (large) { // a larger model runs wholly on the graphics card, or not at all: on the processor it would be too slow, so the standard one takes its place
        m_modelId = modelId;
        const Audio::HwPlan big = Audio::Hardware::instance().plan(!m_store->path(gpuHelperId()).isEmpty(), m_store->item(modelId).memory);
        if (big.llmVulkan && big.llmLayers >= 99) {
            const QString err = startWith(gpuHelperId(), 99, cancel);
            if (err.isEmpty()) {
                Audio::Hardware::instance().report(QStringLiteral("notes"), m_store->item(modelId).title + QStringLiteral(" · GPU: ") + big.device);
                return {};
            }
            if (err == QLatin1String("cancelled"))
                return err;
            stop();
            Audio::Hardware::instance().markFailed(QStringLiteral("llm"), QObject::tr("The larger language model could not use the graphics card (%1); the standard one runs instead.").arg(err.left(200)));
        } else {
            Audio::Hardware::instance().report(QStringLiteral("notes"), QObject::tr("the larger model needs the whole graphics card memory (%1); the standard one runs").arg(big.reason));
        }
        modelId = QStringLiteral("qwen3-4b");
        if (m_store->path(modelId).isEmpty())
            return QObject::tr("The larger language model could not run on the graphics card, and the standard one is not installed");
    }
    m_modelId = modelId;
    if (const QString why = memoryWarning(m_store->item(modelId).memory, m_store->item(modelId).title); !why.isEmpty())
        return why;
    const Audio::HwPlan plan = Audio::Hardware::instance().plan(!m_store->path(gpuHelperId()).isEmpty());
    if (plan.llmVulkan) {
        const QString err = startWith(gpuHelperId(), plan.llmLayers, cancel);
        if (err.isEmpty()) {
            Audio::Hardware::instance().report(m_embed ? QStringLiteral("search") : m_vision ? QStringLiteral("vision") : QStringLiteral("notes"), QStringLiteral("GPU: ") + plan.device + (plan.llmLayers >= 99 ? QString() : QObject::tr(" (part of the model)")));
            return {};
        }
        if (err == QLatin1String("cancelled"))
            return err;
        stop();
        Audio::Hardware::instance().markFailed(QStringLiteral("llm"), QObject::tr("The language model could not use the graphics card (%1); it runs on the processor.").arg(err.left(200)));
    }
    const QString err = startWith(LlamaSummarizer::helperId(), 0, cancel);
    if (err.isEmpty())
        Audio::Hardware::instance().report(m_embed ? QStringLiteral("search") : m_vision ? QStringLiteral("vision") : QStringLiteral("notes"), QStringLiteral("CPU"));
    return err;
}

QString LlamaSession::startWith(const QString &helper, int layers, const std::atomic_bool *cancel)
{
    const QString program = m_store->path(helper), folder = m_store->folder(helper), model = m_store->path(m_modelId);
    // a free port on this computer only, and a token that only this program and the server know
    int port = 0;
    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0))
            return QObject::tr("No free port on this computer for the language model");
        port = probe.serverPort();
    }
    m_token.clear();
    for (int i = 0; i < 32; ++i)
        m_token += QByteArray::number(QRandomGenerator::system()->generate(), 16);

    m_server.setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifndef Q_OS_WIN
    env.insert(QStringLiteral("LD_LIBRARY_PATH"), folder + u':' + env.value(QStringLiteral("LD_LIBRARY_PATH")));
#endif
    m_server.setProcessEnvironment(env);
    m_server.setWorkingDirectory(folder);
    ChildLife::dieWithParent(m_server);
    QStringList args{QStringLiteral("-m"), model, QStringLiteral("--host"), QStringLiteral("127.0.0.1"), QStringLiteral("--port"), QString::number(port), QStringLiteral("--api-key"),
                             QString::fromLatin1(m_token), QStringLiteral("-c"), QStringLiteral("8192"), QStringLiteral("-t"), QString::number(layers >= 99 ? qMin(m_threads(), 4) : m_threads()), QStringLiteral("--jinja"),
                             QStringLiteral("--no-webui"), QStringLiteral("-ngl"), QString::number(layers)};
    if (m_vision)
        args << QStringLiteral("--mmproj") << m_store->path(QStringLiteral("vision-mmproj"));
    if (m_embed) // vectors, not text: the last token of each text is its vector; a text is cut at 2048 tokens
        args << QStringLiteral("--embedding") << QStringLiteral("--pooling") << QStringLiteral("last") << QStringLiteral("-ub") << QStringLiteral("2048") << QStringLiteral("-b") << QStringLiteral("2048");
    m_server.start(program, args);
    if (!m_server.waitForStarted(15000))
        return QObject::tr("The language model program could not be started");
    m_base = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
    // loading 2.5 GB takes a while: wait for /health to say ok
    QElapsedTimer waited;
    waited.start();
    QString log;
    while (waited.elapsed() < 240000) {
        if (cancel && cancel->load()) {
            stop();
            return QStringLiteral("cancelled");
        }
        log += QString::fromUtf8(m_server.readAll());
        if (m_server.state() == QProcess::NotRunning)
            return QObject::tr("The language model program stopped: %1").arg(tail(log));
        QByteArray body;
        int status = 0;
        if (getBlocking(m_base.resolved(QUrl(QStringLiteral("/health"))), m_token, 3000, &body, &status).isEmpty() && status == 200)
            return {};
        QThread::msleep(500);
    }
    stop();
    return QObject::tr("The language model did not become ready in time: %1").arg(tail(log));
}

QList<QVector<float>> LlamaSession::embed(const QStringList &texts, const std::atomic_bool *cancel)
{
    QList<QVector<float>> out;
    if (!m_embed || texts.isEmpty())
        return out;
    QJsonArray input;
    for (const QString &t : texts)
        input.append(t.left(6000));
    const QJsonObject body{{QStringLiteral("model"), QStringLiteral("local")}, {QStringLiteral("input"), input}};
    QByteArray answer;
    int status = 0;
    const QString err = postJsonBlocking(m_base.resolved(QUrl(QStringLiteral("/v1/embeddings"))), m_token, QJsonDocument(body).toJson(QJsonDocument::Compact), 5 * 60 * 1000, cancel, &answer, &status);
    if (!err.isEmpty() || status != 200)
        return out;
    const QJsonArray data = QJsonDocument::fromJson(answer).object().value(QStringLiteral("data")).toArray();
    for (const QJsonValue &d : data) {
        const QJsonArray e = d.toObject().value(QStringLiteral("embedding")).toArray();
        QVector<float> v;
        v.reserve(512);
        double norm = 0;
        for (int i = 0; i < e.size() && i < 512; ++i) { // the first 512 numbers are a whole vector (the model is trained so)
            v.append(float(e[i].toDouble()));
            norm += double(v.last()) * v.last();
        }
        norm = std::sqrt(norm);
        if (norm > 0)
            for (float &x : v)
                x = float(x / norm);
        out.append(v);
    }
    return out.size() == texts.size() ? out : QList<QVector<float>>();
}

QString LlamaSession::chat(const QString &system, const QString &user, const QJsonObject &schema, const std::atomic_bool *cancel, int maxTokens)
{
    return complete(QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}},
                               QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("/no_think\n") + user}}},
                    schema, cancel, maxTokens);
}

QString LlamaSession::chatImage(const QString &system, const QString &user, const QByteArray &image, const QString &mime, const QJsonObject &schema, const std::atomic_bool *cancel, int maxTokens)
{
    const QString url = QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(image.toBase64()));
    return complete(QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}},
                               QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                           {QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")}, {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), url}}}},
                                                                                  QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), user}}}}}},
                    schema, cancel, maxTokens);
}

QString LlamaSession::complete(const QJsonArray &messages, const QJsonObject &schema, const std::atomic_bool *cancel, int maxTokens)
{
    const QJsonObject body{{QStringLiteral("model"), QStringLiteral("local")},
                           {QStringLiteral("messages"), messages},
                           {QStringLiteral("temperature"), 0.2},
                           {QStringLiteral("max_tokens"), maxTokens},
                           {QStringLiteral("stream"), false},
                           {QStringLiteral("chat_template_kwargs"), QJsonObject{{QStringLiteral("enable_thinking"), false}}},
                           {QStringLiteral("response_format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("json_schema")},
                                                                           {QStringLiteral("json_schema"), QJsonObject{{QStringLiteral("name"), QStringLiteral("notes")},
                                                                                                                       {QStringLiteral("schema"), schema}}}}}};
    QByteArray answer;
    int status = 0;
    const QString err = postJsonBlocking(m_base.resolved(QUrl(QStringLiteral("/v1/chat/completions"))), m_token, QJsonDocument(body).toJson(QJsonDocument::Compact), 20 * 60 * 1000,
                                         cancel, &answer, &status);
    if (!err.isEmpty() || status != 200)
        return {};
    const QJsonObject reply = QJsonDocument::fromJson(answer).object();
    return stripThinking(reply.value(QStringLiteral("choices")).toArray().at(0).toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString());
}

QString LlamaSession::chatStream(const QString &system, const QString &user, const QJsonObject &schema, const std::atomic_bool *cancel, int maxTokens, const std::function<void(const QString &)> &onText)
{
    return completeStream(QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}},
                                     QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("/no_think\n") + user}}},
                          schema, cancel, maxTokens, onText);
}

QString LlamaSession::completeStream(const QJsonArray &messages, const QJsonObject &schema, const std::atomic_bool *cancel, int maxTokens, const std::function<void(const QString &)> &onText)
{
    const QJsonObject body{{QStringLiteral("model"), QStringLiteral("local")},
                           {QStringLiteral("messages"), messages},
                           {QStringLiteral("temperature"), 0.2},
                           {QStringLiteral("max_tokens"), maxTokens},
                           {QStringLiteral("stream"), true},
                           {QStringLiteral("chat_template_kwargs"), QJsonObject{{QStringLiteral("enable_thinking"), false}}},
                           {QStringLiteral("response_format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("json_schema")},
                                                                           {QStringLiteral("json_schema"), QJsonObject{{QStringLiteral("name"), QStringLiteral("notes")},
                                                                                                                       {QStringLiteral("schema"), schema}}}}}};
    SseParser sse;
    QString written;
    int status = 0;
    const QString err = postJsonStreaming(m_base.resolved(QUrl(QStringLiteral("/v1/chat/completions"))), m_token, QJsonDocument(body).toJson(QJsonDocument::Compact), 20 * 60 * 1000, cancel, [&](const QByteArray &bytes) {
        const QStringList pieces = sse.feed(bytes);
        if (pieces.isEmpty())
            return;
        written += pieces.join(QString());
        if (onText)
            onText(written);
    }, &status);
    if (err == QLatin1String("cancelled"))
        return stripThinking(written);
    if (!err.isEmpty() || status != 200)
        return {};
    return stripThinking(written);
}

QString LlamaSummarizer::summarize(const QList<Segment> &segments, const QStringList &speakers, Template tmpl, const QString &language,
                                   const std::function<void(double)> &progress, const std::atomic_bool *cancel, Summary *out)
{
    LlamaSession session(m_store, m_threads);
    if (const QString err = session.start(cancel); !err.isEmpty())
        return err;
    SummarizeOptions opt;
    opt.tmpl = tmpl;
    opt.language = language;
    return summarizeTranscript(segments, speakers, opt, session.chatFn(cancel), progress, cancel, out);
}

} // namespace Audio
