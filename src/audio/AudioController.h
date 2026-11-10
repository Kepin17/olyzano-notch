#pragma once

#include "AudioEngines.h"
#include "AudioPlayback.h"
#include "AudioModels.h"
#include "AudioPipeline.h"
#include "AudioStore.h"
#include "InstallQueue.h"
#include "ModelBundles.h"
#include "LiveSession.h"
#include "services/Chunker.h"
#include "MeetingRecorder.h"
#include "ModelStore.h"

#include <QObject>
#include <memory>
#include <QSet>
#include <QSettings>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

// Audio Intelligence for the QML pages: the meetings, one open meeting (transcript, notes, speakers, audio), the job that is running, the
// recorder, and the models. The slow work (ffmpeg, whisper, diarization, the language model) runs on one worker thread, one job at a time;
// everything that touches the database happens here, on the GUI thread, handed over by the job's hooks.
class AudioController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *meetings READ meetings CONSTANT)
    Q_PROPERTY(QAbstractItemModel *transcript READ transcript CONSTANT)
    Q_PROPERTY(bool ready READ ready CONSTANT)                       // the database opened
    Q_PROPERTY(qlonglong current READ current NOTIFY currentChanged) // the open meeting, 0 for none
    Q_PROPERTY(QVariantMap info READ info NOTIFY currentChanged)     // title, status, language, duration, audio url, error...
    Q_PROPERTY(QVariantMap notes READ notes NOTIFY notesChanged)     // the summary of the open meeting
    Q_PROPERTY(QVariantList speakers READ speakers NOTIFY speakersChanged)
    Q_PROPERTY(QVariantList nameHints READ nameHints NOTIFY speakersChanged) // names the conversation suggests: [{speaker, name, time, timeText, quote, certain}]
    Q_PROPERTY(QVariantList waveform READ waveform NOTIFY waveformChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobChanged)
    Q_PROPERTY(int queued READ queued NOTIFY jobChanged)             // recordings waiting behind the one being worked on
    Q_PROPERTY(int batchTotal READ batchTotal NOTIFY jobChanged)     // recordings sent together, and how many of them are finished
    Q_PROPERTY(int batchDone READ batchDone NOTIFY jobChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged) // [{jobId, meetingId, title, state: done|stopped|failed|running, text, when, took}]
    Q_PROPERTY(qlonglong jobMeeting READ jobMeeting NOTIFY jobChanged)
    Q_PROPERTY(QVariantList stages READ stages NOTIFY jobChanged)    // [{id, label, state: done|active|pending, progress}]
    Q_PROPERTY(double jobProgress READ jobProgress NOTIFY jobChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY jobChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList aiCategories READ aiCategories CONSTANT)    // the categories of the AI models page, each with its features
    Q_PROPERTY(QVariantList aiFeatures READ aiFeatures NOTIFY modelsChanged)
    Q_PROPERTY(QVariantMap aiSummary READ aiSummary NOTIFY modelsChanged) // installed, on disk, what is left, and the install that is running
    Q_PROPERTY(QObject *playback READ playback CONSTANT)              // plays a recording on the default sound output (Linux); null elsewhere
    Q_PROPERTY(QString firstTab READ firstTab CONSTANT) // the tab a recording opens on (NOTCH_SHOT_TAB, for screenshots; else Summary)
    Q_PROPERTY(QStringList modelIds READ modelIds CONSTANT)           // the catalog's ids: a list that does not change while models download
    Q_PROPERTY(QVariantMap readiness READ readiness NOTIFY modelsChanged)
    Q_PROPERTY(QVariantMap hardware READ hardware NOTIFY hardwareChanged) // the graphics cards, the setting, and what runs where
    Q_PROPERTY(int threads READ threads WRITE setThreads NOTIFY settingsChanged)
    Q_PROPERTY(QString quality READ quality WRITE setQuality NOTIFY settingsChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged)
    Q_PROPERTY(QString tmpl READ tmpl WRITE setTmpl NOTIFY settingsChanged)
    Q_PROPERTY(bool detectSound READ detectSound WRITE setDetectSound NOTIFY settingsChanged) // tell music and other sounds from speech (when the sound model is installed)
    Q_PROPERTY(bool soundReady READ soundReady NOTIFY modelsChanged)
    Q_PROPERTY(QString cleanMode READ cleanMode WRITE setCleanMode NOTIFY settingsChanged) // off | denoise | vocals: done before the recognizer (when its model is installed)
    Q_PROPERTY(bool denoiseReady READ denoiseReady NOTIFY modelsChanged)
    Q_PROPERTY(bool vocalsReady READ vocalsReady NOTIFY modelsChanged)
    Q_PROPERTY(bool punctuate READ punctuate WRITE setPunctuate NOTIFY settingsChanged) // marks and capitals for English text that has none
    Q_PROPERTY(bool punctReady READ punctReady NOTIFY modelsChanged)
    Q_PROPERTY(QString speakerSplit READ speakerSplit WRITE setSpeakerSplit NOTIFY settingsChanged) // fewer | balanced | more
    Q_PROPERTY(int ramLimitMb READ ramLimitMb WRITE setRamLimitMb NOTIFY settingsChanged) // 0 = no limit
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(QObject *live READ live NOTIFY liveChanged) // the live transcript of the meeting being recorded (null otherwise)
    Q_PROPERTY(double recordSeconds READ recordSeconds NOTIFY recordingChanged)
    Q_PROPERTY(double recordLevel READ recordLevel NOTIFY recordingChanged)

public:
    explicit AudioController(QObject *parent = nullptr);
    ~AudioController() override;

    QAbstractItemModel *meetings() { return &m_list; }
    QAbstractItemModel *transcript() { return &m_transcript; }
    bool ready() const { return m_store.isOpen(); }
    qlonglong current() const { return m_current; }
    QVariantMap info() const;
    QVariantMap notes() const { return m_notes; }
    QVariantList speakers() const { return m_speakers; }
    QVariantList nameHints() const { return m_hints; }
    QVariantList waveform() const { return m_waveform; }
    bool busy() const { return m_busy != 0; }
    int queued() const { return int(m_queue.size()); }
    int batchTotal() const { return m_batchTotal; }
    int batchDone() const { return m_batchDone; }
    QVariantList history() const;
    qlonglong jobMeeting() const { return m_busy; }
    QVariantList stages() const;
    double jobProgress() const { return m_jobProgress; }
    QString lastError() const { return m_error; }
    QStringList warnings() const { return m_warnings; }
    QVariantList models() const;
    QVariantList aiCategories() const;
    QVariantList aiFeatures() const;
    QVariantMap aiSummary() const;
    // One place for every model, by feature and category: install some at once (the programs they share are downloaded once), stop, remove.
    Q_INVOKABLE QStringList aiFeatureIds(const QString &category) const { return Audio::Bundles::featureIds(category, true); } // fixed: a list that does not change keeps the rows of a page from being rebuilt
    Q_INVOKABLE QString dataSize(qlonglong bytes) const { return QLocale().formattedDataSize(bytes); }
    Q_INVOKABLE void installFeatures(const QStringList &featureIds);
    Q_INVOKABLE void installCategory(const QString &category, bool withExtras = false);
    Q_INVOKABLE void installRecommended() { installFeatures(Audio::Bundles::recommended()); }
    Q_INVOKABLE void installEverything() { installFeatures(Audio::Bundles::everything()); }
    Q_INVOKABLE void cancelInstall() { m_install.cancel(); }
    Q_INVOKABLE void removeFeature(const QString &featureId);
    Q_INVOKABLE void clearInstallMessage() { m_installMessage.clear(); emit modelsChanged(); }
    ModelStore *modelStore() { return &m_models; } // the downloaded models and helpers, shared with what reads documents
    QObject *playback() {
#ifdef Q_OS_LINUX
        return &m_playback;
#else
        return nullptr;
#endif
    }
    QString firstTab() const { return qEnvironmentVariable("NOTCH_SHOT_TAB", QStringLiteral("summary")); }
    QStringList modelIds() const { QStringList ids; for (const auto &i : m_models.items()) if (i.kind != Audio::Kind::Ocr && i.kind != Audio::Kind::Vision && i.kind != Audio::Kind::Embedder && i.kind != Audio::Kind::Matting && !i.id.startsWith(QLatin1String("sound-")) && !i.id.startsWith(QLatin1String("clean-")) && !i.id.startsWith(QLatin1String("punct-")) && !i.id.startsWith(QLatin1String("gpu-")) && !i.id.startsWith(QLatin1String("onnxruntime-"))) ids << i.id; return ids; } // the graphics card program has its own place (Hardware); the picture models have theirs
    Q_INVOKABLE QVariantMap modelInfo(const QString &id) const { for (const QVariant &v : models()) if (v.toMap().value(QStringLiteral("id")).toString() == id) return v.toMap(); return {}; }
    QVariantMap readiness() const;
    QVariantMap hardware() const;
    // For the knowledge index: the recordings that have a transcript ({id, title, created, fingerprint}) and the lines of one (main thread).
    QVariantList knowledgeMeetings() const;
    QList<Knowledge::Utterance> transcriptLines(qlonglong id) const;
    QList<Audio::ActionItem> actionItemsOf(qlonglong id) const; // the open tasks of a recording's notes
    QString meetingTitle(qlonglong id) const { return m_store.meeting(id).title; }
    bool processing() const { return m_busy != 0 || !m_queue.isEmpty() || m_recorder.recording(); } // the processor is busy with a recording
    int threads() const;
    void setThreads(int n);
    QString quality() const { return m_settings.value(QStringLiteral("quality"), QStringLiteral("standard")).toString(); }
    void setQuality(const QString &q);
    QString language() const { return m_settings.value(QStringLiteral("language"), QStringLiteral("auto")).toString(); }
    void setLanguage(const QString &l);
    QString speakerSplit() const { const QString v = m_settings.value(QStringLiteral("speakerSplit"), QStringLiteral("balanced")).toString(); return v == QLatin1String("fewer") || v == QLatin1String("more") ? v : QStringLiteral("balanced"); }
    bool detectSound() const { return m_settings.value(QStringLiteral("detectSound"), true).toBool(); }
    void setDetectSound(bool on) { m_settings.setValue(QStringLiteral("detectSound"), on); emit settingsChanged(); }
    bool soundReady() const { return m_tagger.available(); }
    QString cleanMode() const { const QString m = m_settings.value(QStringLiteral("cleanMode"), QStringLiteral("off")).toString(); return Audio::Clean::valid(m) ? m : Audio::Clean::Off; }
    void setCleanMode(const QString &m) { m_settings.setValue(QStringLiteral("cleanMode"), Audio::Clean::valid(m) ? m : Audio::Clean::Off); emit settingsChanged(); }
    bool denoiseReady() const { return m_cleaner.available(Audio::Clean::Denoise); }
    bool vocalsReady() const { return m_cleaner.available(Audio::Clean::Vocals); }
    bool punctuate() const { return m_settings.value(QStringLiteral("punctuate"), false).toBool(); }
    void setPunctuate(bool on) { m_settings.setValue(QStringLiteral("punctuate"), on); emit settingsChanged(); }
    bool punctReady() const { return m_punct.available(); }
    void setSpeakerSplit(const QString &v) { m_settings.setValue(QStringLiteral("speakerSplit"), v); emit settingsChanged(); }
    QString tmpl() const { return m_settings.value(QStringLiteral("template"), QStringLiteral("meeting")).toString(); }
    void setTmpl(const QString &t);
    int ramLimitMb() const { return m_settings.value(QStringLiteral("ramLimitMb"), 0).toInt(); }
    void setRamLimitMb(int mb);
    bool recording() const { return m_recorder.recording(); }
    QObject *live() { return m_live.get(); }
    double recordSeconds() const { return m_recorder.seconds(); }
    double recordLevel() const { return m_recorder.level(); }

    // What a file is, before it is imported: {ok, name, sizeText, durationText, seconds, needsFfmpeg, ffmpeg, memoryText, warning}
    Q_INVOKABLE QVariantMap inspect(const QString &pathOrUrl) const;
    // Adds a meeting for the file and (autoStart) starts it. Returns its id, 0 when it could not.
    Q_INVOKABLE qlonglong importFile(const QString &pathOrUrl, const QString &title, const QString &language, const QString &quality, const QString &tmpl, bool autoStart, int speakers = 0);
    Q_INVOKABLE void open(qlonglong id);
    Q_INVOKABLE void close();
    Q_INVOKABLE void process(qlonglong id); // (again): goes on from the last stage that finished
    Q_INVOKABLE void regenerateNotes(qlonglong id, const QString &tmpl);
    Q_INVOKABLE void cancel();
    // Several files at once: each is added and they are worked on one after the other. Returns how many were added.
    Q_INVOKABLE int importMany(const QStringList &paths, const QString &language, const QString &quality, const QString &tmpl, int speakers = 0);
    // Tell the speakers apart again from the transcript that is there (nothing is recognized again). `count`: how many people, 0 = find out.
    Q_INVOKABLE void redetectSpeakers(qlonglong id, int count);
    Q_INVOKABLE void applyNameHint(const QString &speakerLabel, const QString &name);
    Q_INVOKABLE void dismissNameHint(const QString &speakerLabel, const QString &name);
    Q_INVOKABLE void copyText(const QString &text); // to the clipboard
    Q_INVOKABLE void copyNotes(); // the notes of the open recording, as Markdown, to the clipboard
    Q_INVOKABLE QString notesMarkdown() const;
    // The open recording's transcript as a file: format "srt" | "vtt" | "txt", `sounds` keeps the lines for music and other sounds as [markers].
    // `target` is a path or a file:// address. "" on success, else why not.
    Q_INVOKABLE QString exportTranscript(const QString &format, bool sounds, const QString &target) const;
    Q_INVOKABLE QString suggestedFileName(const QString &format) const; // title + extension, for the save dialog
    // The chapters (the notes' topics that say when they begin): "youtube" | "list". "" when the notes give fewer than three.
    Q_INVOKABLE QString chaptersText(const QString &style) const;
    Q_INVOKABLE void copyChapters(const QString &style);
    Q_INVOKABLE QString saveChapters(const QString &style, const QString &target) const; // "" on success
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void remove(qlonglong id, bool deleteFiles);
    Q_INVOKABLE void rename(qlonglong id, const QString &title);
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void renameSpeaker(const QString &label, const QString &name);
    Q_INVOKABLE void editSegment(qlonglong segmentId, const QString &text);
    Q_INVOKABLE void deleteSegment(qlonglong segmentId);
    Q_INVOKABLE void setActionDone(qlonglong actionId, bool done);
    Q_INVOKABLE int segmentAt(double seconds) const { return m_transcript.rowAt(seconds); }
    Q_INVOKABLE void clearError() { setError({}); }

    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void setHardwareMode(const QString &mode); // auto | cpu | gpu
    Q_INVOKABLE void detectHardware();                      // look again (and forget what failed)
    Q_INVOKABLE void speedTest();                           // the speech model on the processor and on the card, for an integrated one
    Q_INVOKABLE void startLive(); // record, and transcribe (and translate) while it happens
    Q_INVOKABLE void stopRecording(bool process);
    Q_INVOKABLE void discardRecording();

    Q_INVOKABLE void downloadModel(const QString &id) { m_models.download(id); }
    Q_INVOKABLE void cancelDownload(const QString &id) { m_models.cancel(id); }
    Q_INVOKABLE void removeModel(const QString &id);

signals:
    void currentChanged();
    void notesChanged();
    void speakersChanged();
    void waveformChanged();
    void jobChanged();
    void historyChanged();
    void errorChanged();
    void modelsChanged();
    void settingsChanged();
    void recordingChanged();
    void liveChanged();
    void hardwareChanged();
    void listChanged();
    void finished(qlonglong id, bool ok); // a job ended (the pill and toasts listen)

private:
    struct JobOptions {
        QString language, quality;
        Audio::Template tmpl;
        bool stereo, diarize, summarize;
    };
    void reload();
    void setError(const QString &e);
    void start(qlonglong id, bool notesOnly, bool summarize = true);
    void next();
    void applyTranscript(qlonglong id, const QList<Audio::Segment> &segments, const QString &language, double duration);
    void applyNotes(qlonglong id, const Audio::Summary &s, const QString &tmpl);
    void finishJob(qlonglong id, qint64 jobId, const Audio::PipelineResult &r);
    void loadCurrent();
    void loadWaveform(qlonglong id);
    QString meetingDir(qlonglong id) const;
    QString readinessProblem(const QString &quality) const;
    double estimateNeedBytes(const QString &quality) const;

    QSettings m_settings;
    AudioStore m_store;
    ModelStore m_models;
    MeetingListModel m_list;
    TranscriptModel m_transcript;
    MeetingRecorder m_recorder;
    AudioPlayback m_playback;
    QThreadPool m_pool;
    Audio::FfmpegPreprocessor m_pre;
    Audio::WhisperAsr m_asr;
    Audio::SherpaDiarizer m_diar;
    Audio::SherpaTagger m_tagger;
    Audio::SherpaCleaner m_cleaner;
    Audio::SherpaPunctuator m_punct;
    Audio::LlamaSummarizer m_sum;
    InstallQueue m_install;
    QStringList m_installIds; // everything the running install was asked for (for its progress)
    QString m_installMessage;
    QStringList installedFeatures() const;
    std::atomic_bool m_cancel{false};
    int m_batchTotal = 0, m_batchDone = 0;
    QTimer m_tick;

    qlonglong m_current = 0, m_busy = 0, m_recordingMeeting = 0;
    QVariantMap m_notes;
    QVariantList m_hints;
    QSet<QString> m_dismissed; // "meeting|label|name" the person said no to (kept until Notch closes)
    void computeHints();
    QVariantList m_speakers, m_waveform;
    QString m_error, m_stage, m_search;
    QStringList m_warnings, m_doneStages;
    double m_jobProgress = 0, m_stageFraction = 0;
    QList<qlonglong> m_queue;
    QHash<qlonglong, bool> m_notesOnly;
    QString m_recordingDir;
    std::unique_ptr<LiveSession> m_live;
    std::atomic_bool m_testing{false};
    mutable std::atomic_bool m_probing{false}; // the graphics cards are being looked for
    QString m_testResult;
};
