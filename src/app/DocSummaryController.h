#pragma once

#include "audio/AudioController.h"

#include <QObject>
#include <QThreadPool>
#include <QVariantMap>

#include <atomic>

class NotchController;

// "Summarize as study notes": a PDF of the shelf is read (its text, and its pictures by OCR), the local language model writes the notes, and a
// PDF and a Markdown file with them go onto the shelf. One job at a time, on a worker thread, with progress and a stop button. The language
// model and its program are the ones Audio downloads (AudioController's ModelStore).
class DocSummaryController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString stage READ stage NOTIFY changed)       // pages | ocr | read | write | ""
    Q_PROPERTY(QString stageText READ stageText NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)  // 0..1 of the whole job
    Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)
    Q_PROPERTY(QString lastResult READ lastResult NOTIFY changed) // what was made last, for the toast
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)  // what was made, newest first: [{title, when, pages, style, language, pdfId, mdId, pdfThere, mdThere}]

public:
    DocSummaryController(NotchController *notch, AudioController *audio, QObject *parent = nullptr);
    ~DocSummaryController() override;

    bool busy() const { return m_busy; }
    QString stage() const { return m_stage; }
    QString stageText() const;
    double progress() const { return m_progress; }
    QString fileName() const { return m_fileName; }
    QString lastError() const { return m_error; }
    QStringList warnings() const { return m_warnings; }
    QString lastResult() const { return m_result; }
    QVariantList history() const;

    // {ok, name, pages, pictureShare (of a sample of pages), ocrLanguages, ocrInstalled, estimateMinutes, warning}
    Q_INVOKABLE QVariantMap inspect(const QString &itemId);
    Q_INVOKABLE QVariantMap inspectPath(const QString &pathOrUrl); // a PDF that is not on the shelf: read where it is, not copied
    // options: {style, length, language, ocr, firstPage, lastPage}
    Q_INVOKABLE void start(const QString &itemId, const QVariantMap &options);
    Q_INVOKABLE void startPath(const QString &pathOrUrl, const QVariantMap &options);
    Q_INVOKABLE void removeHistory(int index); // forgets the entry; the files stay on the shelf
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();

signals:
    void changed();
    void finished(bool ok);

private:
    QVariantMap describe(const QString &path, const QString &name);
    void begin(const QString &path, const QString &name, const QString &base, const QVariantMap &options);
    void saveHistory() const;
    QVariantList m_history;
    void done(const QString &error, const QStringList &warnings, const QString &base, const QString &mdName, const QByteArray &md, const QString &pdfName, const QString &html, const QVariantMap &meta);

    NotchController *m_notch;
    AudioController *m_audio;
    QThreadPool m_pool;
    std::atomic_bool m_cancel{false};
    bool m_busy = false;
    QString m_stage, m_fileName, m_error, m_result;
    QStringList m_warnings;
    double m_progress = 0;
};
