#pragma once

#include <QList>
#include <QStringList>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>

// whisper.cpp behind a small door: load a model once, turn 16 kHz mono samples into text. Blocking; call it from a
// worker thread. Without NOTCH_HAVE_WHISPER (no network when building) there is nothing to load and `compiled()` is false.
class Whisper
{
public:
    Whisper();
    ~Whisper();
    Whisper(const Whisper &) = delete;
    Whisper &operator=(const Whisper &) = delete;

    static bool compiled();
    // The graphics processors this build can use (empty when it was built without GPU support): `index` is what load() takes.
    struct GpuDevice {
        int index = 0;
        QString name;
        bool integrated = false;
        qint64 freeBytes = 0, totalBytes = 0;
    };
    static QList<GpuDevice> gpus();
    static bool gpusBuilt(); // this build has graphics support (Vulkan), whether or not a card is found
    // `gpu`: run on the best graphics processor (a card of its own before an integrated one); when that cannot be done the model is loaded
    // on the processor instead and `fellBack()` says so. false when the file is no model.
    bool load(const QString &modelPath, bool gpu = false);
    QString backend() const;  // "CPU", or the graphics processor it runs on
    bool fellBack() const;    // the last load was asked for the GPU and had to use the processor
    bool loaded() const;
    void unload();
    // What was heard, and how sure the model was: the mean probability of the words (0..1), and the pieces it doubted.
    struct Transcript {
        QString text;
        double confidence = 0;
        QStringList doubtful;
        QString language;
    };
    // language: "auto" (Indonesian or English), "id", "en"; `vocabulary`: names that may come up (apps, sites), which settle the spelling.
    // An empty text: nothing was recognised or the model is not loaded.
    Transcript hear(const QVector<float> &samples, const QString &language, const QString &vocabulary = QString());
    QString transcribe(const QVector<float> &samples, const QString &language) { return hear(samples, language).text; }

    // A stretch of speech (up to 30 s) with the times of what was said, for transcripts of recordings (Audio Intelligence). Times are in seconds
    // from the start of `samples`. `language`: auto | id | en; auto decides between Indonesian and English (Malay counts as Indonesian) and only
    // when neither is likely takes what the model finds. `cancel` is looked at while it works. "" on success, "cancelled", or why not.
    struct Piece {
        double start = 0, end = 0;
        QString text;
        float noSpeech = 0; // the model's own doubt that this was speech (0..1)
    };
    // `fast`: greedy decoding without retries, for live use (a little less exact, about three times quicker).
    // `candidates`: the Whisper codes of the languages in use: "auto" then picks the likeliest of those (none given: Indonesian or English as before).
    QString transcribeSegments(const QVector<float> &samples, const QString &language, int threads, const std::atomic_bool *cancel, QList<Piece> *out,
                               QString *detectedLanguage, const QStringList &candidates = {}, bool fast = false);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
