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
    // language: "auto", "id", "en", ...; "" when nothing was recognised or the model is not loaded.
    QString transcribe(const QVector<float> &samples, const QString &language);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
