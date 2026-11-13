#pragma once

#include "audio/AskPrompt.h"
#include "audio/AudioController.h"
#include "services/Chunker.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>

#include <atomic>
#include <memory>

class NotchController;

// What the notch knows about your files, for searching and asking: an index of the PDFs, text files and recordings is kept up to date in the
// background (knowledge.db), a sentence finds files on the shelf (smart search), and a question is answered from the passages found with the
// quotes that support it. Local only. The language model and its program are the ones Audio downloads; the optional embedding model makes the
// search follow meaning as well as words.
class KnowledgeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool indexPdf READ indexPdf WRITE setIndexPdf NOTIFY changed)
    Q_PROPERTY(bool indexOcr READ indexOcr WRITE setIndexOcr NOTIFY changed)       // scanned PDFs, read with OCR (slow)
    Q_PROPERTY(bool indexImages READ indexImages WRITE setIndexImages NOTIFY changed) // pictures, described by the picture model (slow)
    Q_PROPERTY(bool indexing READ indexing NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)       // {docs, chunks, vectors, sizeText}
    Q_PROPERTY(bool modelReady READ modelReady NOTIFY changed)    // the notes model is installed: asking works
    Q_PROPERTY(bool meaningReady READ meaningReady NOTIFY changed) // the embedding model is installed
    Q_PROPERTY(bool smartBusy READ smartBusy NOTIFY changed)
    Q_PROPERTY(QVariantMap smart READ smart NOTIFY changed)       // {active, query, count, chips:[{label, kind}], note, error}
    Q_PROPERTY(bool asking READ asking NOTIFY changed)
    Q_PROPERTY(QString askStage READ askStage NOTIFY changed)     // understanding | searching | reading | ""
    Q_PROPERTY(QVariantMap answer READ answer NOTIFY changed)     // {question, text, notFound, sources:[...], scopeText}
    Q_PROPERTY(QString askError READ askError NOTIFY changed)
    Q_PROPERTY(bool indexForms READ indexForms WRITE setIndexForms NOTIFY changed) // the answers of forms are searched too
    Q_PROPERTY(QVariantList globalResults READ globalResults NOTIFY globalChanged) // what the search box found, in groups: [{group, kind, title, subtitle, snippet, ref, page, startSec}]
    Q_PROPERTY(int globalSerial READ globalSerial NOTIFY globalChanged)            // grows with every result list: an older one never replaces a newer
    Q_PROPERTY(int answerSerial READ answerSerial NOTIFY changed) // grows with each answer: a panel that keeps the conversation adds one message when it changes

public:
    KnowledgeController(NotchController *notch, AudioController *audio, QObject *parent = nullptr);
    ~KnowledgeController() override;

    bool enabled() const { return m_enabled; }
    bool indexPdf() const { return m_pdf; }
    bool indexOcr() const { return m_ocr; }
    bool indexImages() const { return m_images; }
    void setEnabled(bool on);
    void setIndexPdf(bool on);
    void setIndexOcr(bool on);
    void setIndexImages(bool on);
    bool indexForms() const { return m_forms; }
    void setIndexForms(bool on);
    QVariantList globalResults() const { return m_global; }
    int globalSerial() const { return m_globalSerial; }
    // One form's answers into the index (a document of kind "form": a passage for each answer, `page` its number), replacing what was there; or out of it.
    void indexForm(const QString &id, const QString &title, const QList<Knowledge::Chunk> &chunks, const QString &fingerprint);
    void removeForm(const QString &id);
    // What the search box shows for what was typed: passages of recordings (and their notes), files and forms, and documents by name. Answers by globalChanged.
    Q_INVOKABLE void globalSearch(const QString &query);
    bool indexing() const { return m_indexing; }
    QString status() const { return m_status; }
    QVariantMap stats() const { return m_stats; }
    bool modelReady() const;
    bool meaningReady() const;
    bool smartBusy() const { return m_smartBusy; }
    QVariantMap smart() const { return m_smart; }
    bool asking() const { return m_asking; }
    QString askStage() const { return m_stage; }
    QVariantMap answer() const { return m_answer; }
    QString askError() const { return m_askError; }
    int answerSerial() const { return m_answerSerial; }

    Q_INVOKABLE void rescan();                                    // look for what changed now
    Q_INVOKABLE void rebuild();                                   // forget the index and make it again
    Q_INVOKABLE void clearIndex();
    Q_INVOKABLE void smartSearch(const QString &sentence);        // finds files on the shelf for a sentence
    Q_INVOKABLE void smartDrop(int chip);                         // searches again without that part of the understanding
    Q_INVOKABLE void clearSmart();
    // scope: {kind: all | files | recordings | file | recording, ref}
    Q_INVOKABLE void ask(const QString &question, const QVariantMap &scope);
    Q_INVOKABLE void cancelAsk();
    Q_INVOKABLE void clearAnswer();

signals:
    void changed();
    void globalChanged();
    void formsToggled(bool on); // the answers of forms are in the index now, or not any more: the forms are told to send or to withdraw them

private:
    struct Job; // what a question or a search needs, made on the worker thread
    void scan();
    void index(const QVariantList &items, const QVariantList &meetings, bool pdf, bool ocr, bool images);
    void runSmart(const Audio::Ask::Query &query, const QString &sentence, bool plain, bool understood);
    void setStatus(const QString &s);
    QVariantList chips(const Audio::Ask::Query &q) const;
    Audio::ChatFn chatFn(int maxTokens); // on the worker thread: starts the model when it is first needed; empty when it cannot be
    void idleCheck();
    static QString dbPath();

    NotchController *m_notch;
    AudioController *m_audio;
    QThreadPool m_indexPool, m_pool, m_quickPool; // m_quickPool: the search box and the forms (small jobs, one at a time)
    std::atomic<int> m_globalWanted{0};
    QVariantList m_global;
    int m_globalSerial = 0;
    bool m_forms = true;
    std::atomic_bool m_cancelIndex{false}, m_cancelAsk{false}, m_pause{false};
    QTimer m_scanTimer, m_pauseTimer, m_idleTimer;
    bool m_enabled = true, m_pdf = true, m_ocr = false, m_images = false;
    bool m_indexing = false, m_smartBusy = false, m_asking = false;
    QString m_status, m_stage, m_askError;
    QVariantMap m_stats, m_smart, m_answer;
    int m_answerSerial = 0;
    Audio::Ask::Query m_smartQuery;
    QString m_smartSentence;
    bool m_smartPlain = false;

    // the language model, kept for a couple of minutes after use (starting it takes seconds): touched only by jobs of m_pool
    std::unique_ptr<Audio::LlamaSession> m_llm;
    QString m_llmError;
    bool m_llmTried = false;
    std::atomic<qint64> m_lastUse{0};
    std::atomic_bool m_jobRunning{false};
};
