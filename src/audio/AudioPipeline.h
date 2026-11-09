#pragma once

#include "Aligner.h"
#include "SummaryModel.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

// Audio in, notes out, one stage at a time (so only one model is in memory): preprocess -> vad -> asr -> diarize -> align -> summarize.
// Each stage leaves a file in the meeting's folder, so a job that failed or was stopped goes on from the last stage that finished, and a
// stage's failure never deletes what the stages before it made. The models themselves are behind the interfaces below: the real ones are
// ffmpeg, whisper.cpp, sherpa-onnx and llama.cpp; the tests use fakes. Blocking; run it on a worker thread, and never touch the database
// from it (the hooks hand results to the thread that owns it).
namespace Audio {

class IPreprocessor {
public:
    virtual ~IPreprocessor() = default;
    // `source` (any audio ffmpeg reads) -> 16 kHz mono 16-bit WAV at `wavOut`. "" on success.
    virtual QString convert(const QString &source, const QString &wavOut, const std::function<void(double)> &progress, const std::atomic_bool *cancel) = 0;
};

class IAsr {
public:
    virtual ~IAsr() = default;
    // Loads the model for `quality` ("standard" | "high"); "" or why it cannot (not installed, not enough memory).
    virtual QString prepare(const QString &quality) = 0;
    // One window of 16 kHz mono samples -> segments with times relative to the window. `language`: auto | id | en.
    virtual QString transcribe(const QVector<float> &samples, const QString &language, const std::atomic_bool *cancel, QList<Segment> *segments,
                               QString *detectedLanguage) = 0;
    virtual void release() = 0; // free the model
};

class IDiarizer {
public:
    virtual ~IDiarizer() = default;
    virtual bool available() const = 0;
    // A 16 kHz mono WAV -> who spoke when. "" or why not. `speakers`: how many people there are when that is known, else 0 (found from the voices).
    virtual QString diarize(const QString &wavPath, const std::atomic_bool *cancel, QList<Turn> *turns, int speakers = 0) = 0;
};

class ISummarizer {
public:
    virtual ~ISummarizer() = default;
    virtual bool available() const = 0;
    virtual QString summarize(const QList<Segment> &segments, const QStringList &speakers, Template tmpl, const QString &language,
                              const std::function<void(double)> &progress, const std::atomic_bool *cancel, Summary *out) = 0;
};

struct PipelineOptions {
    QString language = QStringLiteral("auto");
    QString quality = QStringLiteral("standard");
    Template tmpl = Template::Meeting;
    bool diarize = true;
    bool summarize = true;
    bool stereo = false;   // a recording from Notch: microphone left, the computer's sound right
    QString youLabel = QStringLiteral("Anda");
    QString remoteLabel = QStringLiteral("Lawan bicara");
    QString localLabel = QStringLiteral("Ruangan"); // other people at the microphone: "Ruangan 2", "Ruangan 3"
    int speakers = 0;      // how many people, when known (0 = find out from the voices); for recordings that are not stereo
    double maxSeconds = 3600; // the longest recording taken
    int windowSeconds = 25;
};

struct PipelineHooks {
    std::function<void(const QString &stage, double fraction)> report;                        // progress inside the stage, 0..1
    std::function<void(const QList<Segment> &, const QString &language, double duration)> transcript; // the transcript is ready (before the summary)
    std::function<void(const Summary &)> summary;
};

struct PipelineResult {
    bool ok = false, cancelled = false;
    QString error, failedStage;
    QStringList warnings;
    QString language;
    double duration = 0;
    QList<Segment> segments;
    Summary summary;
    bool hasSummary = false;
    QStringList doneStages;
};

class AudioPipeline {
public:
    AudioPipeline(IPreprocessor *pre, IAsr *asr, IDiarizer *diarizer, ISummarizer *summarizer)
        : m_pre(pre), m_asr(asr), m_diarizer(diarizer), m_summarizer(summarizer) {}

    static QStringList stageIds();
    static QString stageLabel(const QString &id);
    // How far a whole job is when `stage` is at `fraction` (the stages are not equally long).
    static double overall(const QString &stage, double fraction);

    // Only the notes, from this transcript (what the person has edited and named, not what was recognized): used to write them again with
    // another template, or to finish a job whose notes failed.
    PipelineResult notes(const QList<Segment> &segments, const QString &workDir, const PipelineOptions &options, const std::atomic_bool &cancel,
                         const PipelineHooks &hooks);
    // `workDir` is the meeting's folder. Stages whose files are there and valid are not done again.
    PipelineResult run(const QString &source, const QString &workDir, const PipelineOptions &options, const std::atomic_bool &cancel, const PipelineHooks &hooks);

    // Text the recognizer makes out of silence and noise is removed: empty lines, [BLANK_AUDIO] and (music) marks, and a phrase repeated
    // three or more times in a row (kept once).
    static QList<Segment> clean(const QList<Segment> &segments);

private:
    bool summarizeStage(PipelineResult &r, const QString &workDir, const PipelineOptions &options, const std::atomic_bool &cancel, const PipelineHooks &hooks);
    IPreprocessor *m_pre;
    IAsr *m_asr;
    IDiarizer *m_diarizer;
    ISummarizer *m_summarizer;
};

} // namespace Audio
