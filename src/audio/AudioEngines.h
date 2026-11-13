#pragma once

#include "AudioPipeline.h"
#include "Summarize.h"
#include "ModelStore.h"
#include "platform/WhisperEngine.h"

#include <QJsonObject>
#include <QProcess>
#include <QUrl>
#include <functional>

// The real engines behind the pipeline's interfaces: ffmpeg, whisper.cpp, sherpa-onnx and llama.cpp's server. They run on the worker thread
// of a job and use blocking calls. `threads` says how many CPU threads each may use.
namespace Audio {

class FfmpegPreprocessor : public IPreprocessor {
public:
    QString convert(const QString &source, const QString &wavOut, const std::function<void(double)> &progress, const std::atomic_bool *cancel) override;
    static QString program(); // "" when ffmpeg is not installed
};

class WhisperAsr : public IAsr {
public:
    WhisperAsr(ModelStore *store, std::function<int()> threads, std::function<QStringList()> languages = {}) : m_store(store), m_threads(std::move(threads)), m_languages(std::move(languages)) {}
    QString prepare(const QString &quality) override;
    QString transcribe(const QVector<float> &samples, const QString &language, const std::atomic_bool *cancel, QList<Segment> *segments,
                       QString *detectedLanguage) override;
    void release() override { m_whisper.unload(); m_loaded.clear(); }
    void setFast(bool on) { m_fast = on; } // for live use: greedy decoding
    static QString modelFor(const QString &quality);

private:
    ModelStore *m_store;
    std::function<int()> m_threads;
    std::function<QStringList()> m_languages; // the Whisper codes of the languages in use, for "auto"
    Whisper m_whisper;
    QString m_loaded;
    bool m_fast = false, m_gpu = false; // m_gpu: what the loaded model runs on
};

class SherpaDiarizer : public IDiarizer {
public:
    // `threshold`: how different two voices must be to count as two people (smaller = more speakers, larger = fewer)
    SherpaDiarizer(ModelStore *store, std::function<int()> threads, std::function<double()> threshold = [] { return 0.55; })
        : m_store(store), m_threads(std::move(threads)), m_threshold(std::move(threshold)) {}
    bool available() const override;
    QString diarize(const QString &wavPath, const std::atomic_bool *cancel, QList<Turn> *turns, int speakers = 0) override;
    static QString helperId();

private:
    ModelStore *m_store;
    std::function<int()> m_threads;
    std::function<double()> m_threshold;
};

// Music and other sounds, with the sound-tagging model (CED, a download) run by the audio-tagging program that comes with the sherpa-onnx
// helper: the recording is cut into clips of ten seconds, each is tagged by a program of its own, a few at a time.
class SherpaTagger : public ISoundTagger {
public:
    SherpaTagger(ModelStore *store, std::function<int()> threads) : m_store(store), m_threads(std::move(threads)) {}
    bool available() const override;
    QString tag(const QString &wavPath, const std::atomic_bool *cancel, const std::function<void(double)> &progress, QList<Sound::Clip> *clips) override;
    static QString modelId() { return QStringLiteral("sound-ced-tiny"); }

private:
    ModelStore *m_store;
    std::function<int()> m_threads;
};

// Noise out of a recording (a small speech-enhancement model, GTCRN) or the voice out of the music (Spleeter), with the programs that come with
// the sherpa-onnx helper. What comes out is 16 kHz mono again, as long as what went in.
class SherpaCleaner : public ICleaner {
public:
    SherpaCleaner(ModelStore *store, std::function<int()> threads) : m_store(store), m_threads(std::move(threads)) {}
    bool available(const QString &mode) const override;
    QString clean(const QString &wavIn, const QString &wavOut, const QString &mode, const std::atomic_bool *cancel, const std::function<void(double)> &progress) override;
    static QString denoiseId() { return QStringLiteral("clean-gtcrn"); }
    static QString vocalsId() { return QStringLiteral("clean-spleeter"); }

private:
    bool helperReady() const;
    ModelStore *m_store;
    std::function<int()> m_threads;
};

// Marks and capitals for English text (the CNN-BiLSTM model of sherpa-onnx's online-punctuation program).
class SherpaPunctuator : public IPunctuator {
public:
    SherpaPunctuator(ModelStore *store, std::function<int()> threads) : m_store(store), m_threads(std::move(threads)) {}
    bool available() const override;
    QString punctuate(const QStringList &texts, const std::atomic_bool *cancel, QStringList *out) override;
    static QString modelId() { return QStringLiteral("punct-en"); }

private:
    ModelStore *m_store;
    std::function<int()> m_threads;
};

// llama.cpp's server as a program of our own on this computer only: started on a free port with a token that only it and we know, waited for until
// it says it is ready, asked through `chat`, and ended when this goes away. Made and used on one worker thread. Shared by what writes notes
// from a recording and what writes notes from a document.
class LlamaSession {
public:
    // `vision`: the model that reads pictures (Qwen3-VL with its mmproj) instead of the one that writes notes
    enum class Mode { Text, Vision, Embed };
    LlamaSession(ModelStore *store, std::function<int()> threads, bool vision = false) : m_store(store), m_threads(std::move(threads)), m_vision(vision) {}
    // `Mode::Embed`: the model that turns text into vectors of meaning (for finding what a sentence is about)
    LlamaSession(ModelStore *store, std::function<int()> threads, Mode mode) : m_store(store), m_threads(std::move(threads)), m_vision(mode == Mode::Vision), m_embed(mode == Mode::Embed) {}
    ~LlamaSession() { stop(); }
    static bool available(const ModelStore *store);
    // The catalog id of the model that writes notes and answers: the one chosen (Qwen3 8B or 14B) when it is installed and this computer can run it on a
    // graphics card, else the standard Qwen3 4B. Never waits for the card to be looked for: until it is known, the standard one.
    static QString textModelId(const ModelStore *store);
    static bool embedAvailable(const ModelStore *store);  // the embedding model and the program are downloaded
    static bool visionAvailable(const ModelStore *store); // the picture model, its mmproj and the program are all downloaded
    static QString gpuHelperId(); // the Vulkan build of the program (for graphics cards)
    // "" when it is ready, else why not ("cancelled" when stopped meanwhile)
    QString start(const std::atomic_bool *cancel);
    // The model's answer (JSON that fits `schema`), or "" when it did not answer.
    QString chat(const QString &system, const QString &user, const QJsonObject &schema, const std::atomic_bool *cancel, int maxTokens = 2048);
    // Vectors for texts (normalised, 512 numbers each), in order; empty when the model did not answer. Only on a session made with Mode::Embed.
    QList<QVector<float>> embed(const QStringList &texts, const std::atomic_bool *cancel);
    // The same with one picture (PNG or JPEG bytes) in the question: only on a session made with `vision`.
    QString chatImage(const QString &system, const QString &user, const QByteArray &image, const QString &mime, const QJsonObject &schema, const std::atomic_bool *cancel, int maxTokens = 1536);
    ChatFn chatFn(const std::atomic_bool *cancel) { return [this, cancel](const QString &s, const QString &u, const QJsonObject &sc) { return chat(s, u, sc, cancel); }; }
    void stop();

private:
    QString startWith(const QString &helper, int layers, const std::atomic_bool *cancel);
    QString m_modelId; // the language model this session runs (set when it starts) // layers: how many go to the graphics card (0: none)
    QString complete(const QJsonArray &messages, const QJsonObject &schema, const std::atomic_bool *cancel, int maxTokens);
    ModelStore *m_store;
    std::function<int()> m_threads;
    bool m_vision = false, m_embed = false;
    QProcess m_server;
    QUrl m_base;
    QByteArray m_token;
};

class LlamaSummarizer : public ISummarizer {
public:
    LlamaSummarizer(ModelStore *store, std::function<int()> threads) : m_store(store), m_threads(std::move(threads)) {}
    bool available() const override;
    QString summarize(const QList<Segment> &segments, const QStringList &speakers, Template tmpl, const QString &language,
                      const std::function<void(double)> &progress, const std::atomic_bool *cancel, Summary *out) override;
    static QString helperId();

private:
    ModelStore *m_store;
    std::function<int()> m_threads;
};

// a blocking POST of JSON to the local server (own event loop, so it works on a worker thread); 0 on success, else a message
QString postJsonBlocking(const QUrl &url, const QByteArray &token, const QByteArray &body, int timeoutMs, const std::atomic_bool *cancel, QByteArray *answer, int *status);
QString getBlocking(const QUrl &url, const QByteArray &token, int timeoutMs, QByteArray *answer, int *status);

} // namespace Audio
