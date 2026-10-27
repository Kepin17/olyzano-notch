#pragma once

#include <QString>
#include <QVector>
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
    bool load(const QString &modelPath); // false when the file is no model
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

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
