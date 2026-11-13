#pragma once

#include "audio/AudioController.h"

#include <QObject>
#include <QThreadPool>

#include <atomic>

class NotchController;

// Translation with the local language model: the transcript of a recording (kept, so that it can be saved as subtitles in several forms without
// translating again) and the pages of a PDF (a new PDF and a Markdown file on the shelf). One job at a time, on a worker thread, with progress
// and a stop button. The model is the one the notes use (AudioController's ModelStore).
class TranslateController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)       // what is happening, or what was made
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)

public:
    TranslateController(NotchController *notch, AudioController *audio, QObject *parent = nullptr);
    ~TranslateController() override;

    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString lastError() const { return m_error; }
    QStringList warnings() const { return m_warnings; }

    // The transcript of recording `id` into the language `code` (kept); `hasTranslation` tells whether a kept one still fits the transcript.
    Q_INVOKABLE void translateMeeting(qlonglong id, const QString &code);
    Q_INVOKABLE bool hasTranslation(qlonglong id, const QString &code) const;
    // format: bilingual-srt | bilingual-vtt | srt | vtt | txt; "" or why not
    Q_INVOKABLE QString exportMeeting(qlonglong id, const QString &code, const QString &format, const QString &target) const;
    Q_INVOKABLE QString suggestedName(qlonglong id, const QString &code, const QString &format) const;
    // A PDF of the shelf into `code`: Name (code).pdf and Name (code).md on the shelf.
    Q_INVOKABLE void translatePdf(const QString &itemId, const QString &code);
    Q_INVOKABLE void translatePdfPath(const QString &pathOrUrl, const QString &code); // a PDF from anywhere (a path or file:// URL)
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();

signals:
    void changed();
    void finished(bool ok);

private:
    QString cachePath(qlonglong id, const QString &code) const;
    QStringList cached(qlonglong id, const QString &code) const; // the kept translation, or {} when there is none or the transcript changed
    void beginPdf(const QString &file, const QString &name, const QString &code);
    void end(const QString &error, const QStringList &warnings, const QString &status);

    NotchController *m_notch;
    AudioController *m_audio;
    QThreadPool m_pool;
    std::atomic_bool m_cancel{false};
    bool m_busy = false;
    double m_progress = 0;
    QString m_status, m_error;
    QStringList m_warnings;
};
