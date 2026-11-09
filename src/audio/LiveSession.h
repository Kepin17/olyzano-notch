#pragma once

#include "AudioEngines.h"
#include "LiveSegmenter.h"

#include <QAbstractListModel>
#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <QVariantList>

#include <atomic>

// The lines of a live meeting, in the order they were said (a line is added when the recognizer has finished it; its translation follows).
class LiveModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { SideRole = Qt::UserRole + 1, StartRole, TextRole, TranslationRole, LanguageRole };
    struct Line {
        qint64 id = 0, startMs = 0, endMs = 0;
        int side = 0;
        QString text, translation, language;
    };
    explicit LiveModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_lines.size()); }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{SideRole, "side"}, {StartRole, "startText"}, {TextRole, "text"}, {TranslationRole, "translation"}, {LanguageRole, "language"}};
    }
    QVariant data(const QModelIndex &index, int role) const override;
    qint64 add(const Line &line); // in time order; returns the id
    void setTranslation(qint64 id, const QString &t);
    const QList<Line> &lines() const { return m_lines; }
    void clear();

private:
    QList<Line> m_lines;
    qint64 m_next = 1;
};

// A meeting transcribed while it happens: the microphone ("You") and the sound of the computer ("Others") are cut into utterances, each is
// transcribed on one worker thread and, if asked, translated by the local language model on another. Smart answer: a few answers to say next.
// Everything stays on this computer. The recording itself is made by MeetingRecorder as usual.
class LiveSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *model READ model CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)        // loading | listening | behind | failed
    Q_PROPERTY(QString notice READ notice NOTIFY statusChanged)       // what to tell the person (why, or what is missing)
    Q_PROPERTY(QString mine READ mine WRITE setMine NOTIFY optionsChanged)         // language of "You": auto or a catalog code
    Q_PROPERTY(QString theirs READ theirs WRITE setTheirs NOTIFY optionsChanged)   // language of the others
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY optionsChanged)   // the language to translate into (and to answer in)
    Q_PROPERTY(bool translate READ translate WRITE setTranslate NOTIFY optionsChanged)
    Q_PROPERTY(bool translateMine READ translateMine WRITE setTranslateMine NOTIFY optionsChanged)
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY suggestionsChanged)
    Q_PROPERTY(bool suggesting READ suggesting NOTIFY suggestionsChanged)
    Q_PROPERTY(bool llmReady READ llmReady CONSTANT) // the language model is installed (translation and answers need it)
public:
    LiveSession(ModelStore *store, std::function<int()> threads, QObject *parent = nullptr);
    ~LiveSession() override;

    QObject *model() { return &m_model; }
    QString status() const { return m_status; }
    QString notice() const { return m_notice; }
    QString mine() const { return m_mine; }
    QString theirs() const { return m_theirs; }
    QString target() const { return m_target; }
    bool translate() const { return m_translate; }
    bool translateMine() const { return m_translateMine; }
    QVariantList suggestions() const { return m_suggestions; }
    bool suggesting() const { return m_suggesting; }
    bool llmReady() const { return m_llmReady; }
    void setMine(const QString &c);
    void setTheirs(const QString &c);
    void setTarget(const QString &c);
    void setTranslate(bool on);
    void setTranslateMine(bool on);

    // The chunks of the recorder (side 0: microphone, 1: computer sound); `endMs` is the recording clock.
    void audio(int side, const QByteArray &s16le, qint64 endMs);
    // The meeting is over: what is being said is closed, the recognizer is given a moment to finish what it has, then everything stops.
    void finish();
    QList<Audio::Segment> segments() const; // what was said, as a transcript ("You" and "Others")

    Q_INVOKABLE void suggestAnswer(const QString &note);

signals:
    void statusChanged();
    void optionsChanged();
    void suggestionsChanged();

private:
    void enqueue(const QList<Audio::Utterance> &list);
    void drain();
    void heard(const Audio::Utterance &u, const QString &text, const QString &detected);
    void translateLine(qint64 id, int side, const QString &text, const QString &detected);
    void setStatus(const QString &status, const QString &notice = {});
    QString languageOf(int side) const;

    ModelStore *m_store;
    std::function<int()> m_threads;
    LiveModel m_model;
    Audio::LiveSegmenter m_seg[2];
    Audio::WhisperAsr m_asr;
    Audio::LlamaSession m_llama;
    QThreadPool m_asrPool, m_llmPool;
    QMutex m_lock;
    QList<Audio::Utterance> m_queue;
    bool m_draining = false;
    std::atomic_bool m_cancelAsr{false}, m_cancelLlm{false};
    std::atomic_int m_llmWaiting{0};
    bool m_llmStarted = false, m_llmFailed = false, m_llmReady = false, m_finished = false;
    QString m_mine = QStringLiteral("auto"), m_theirs = QStringLiteral("auto"), m_target;
    bool m_translate = false, m_translateMine = false, m_suggesting = false;
    QString m_status = QStringLiteral("loading"), m_notice;
    QVariantList m_suggestions;
};
