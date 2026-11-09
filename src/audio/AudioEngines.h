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
    static QString modelFor(const QString &quality);

private:
    ModelStore *m_store;
    std::function<int()> m_threads;
    std::function<QStringList()> m_languages; // the Whisper codes of the languages in use, for "auto"
    Whisper m_whisper;
    QString m_loaded;
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

// llama.cpp's server as a program of our own on this computer only: started on a free port with a token that only it and we know, waited for until
// it says it is ready, asked through `chat`, and ended when this goes away. Made and used on one worker thread. Shared by what writes notes
// from a recording and what writes notes from a document.
class LlamaSession {
public:
    LlamaSession(ModelStore *store, std::function<int()> threads) : m_store(store), m_threads(std::move(threads)) {}
    ~LlamaSession() { stop(); }
    static bool available(const ModelStore *store);
    // "" when it is ready, else why not ("cancelled" when stopped meanwhile)
    QString start(const std::atomic_bool *cancel);
    // The model's answer (JSON that fits `schema`), or "" when it did not answer.
    QString chat(const QString &system, const QString &user, const QJsonObject &schema, const std::atomic_bool *cancel, int maxTokens = 2048);
    ChatFn chatFn(const std::atomic_bool *cancel) { return [this, cancel](const QString &s, const QString &u, const QJsonObject &sc) { return chat(s, u, sc, cancel); }; }
    void stop();

private:
    ModelStore *m_store;
    std::function<int()> m_threads;
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
