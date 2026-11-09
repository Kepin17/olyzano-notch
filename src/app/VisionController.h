#pragma once

#include "audio/AudioController.h"

#include <QObject>
#include <QThreadPool>

#include <atomic>

class NotchController;

// "Image (AI)" in the Tools menu of a picture on the shelf: the picture model (Qwen3-VL, an optional download) looks at it and says what is in it
// or reads the writing in it; the answer is copied or becomes a note. One picture at a time, on a worker thread, with a stop button. The model
// and its program are the ones AudioController's ModelStore downloads.
class VisionController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)         // the picture model is downloaded
    Q_PROPERTY(QString status READ status NOTIFY changed)    // what is going on, for the person ("Reading the picture…")
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

public:
    VisionController(NotchController *notch, AudioController *audio, QObject *parent = nullptr);
    ~VisionController() override;

    bool busy() const { return m_busy; }
    bool ready() const;
    QString status() const { return m_status; }
    QString lastError() const { return m_error; }

    // kind: describe-copy | describe-note | read-copy | read-note
    Q_INVOKABLE void run(const QString &kind, const QString &itemId);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();

signals:
    void changed();

private:
    NotchController *m_notch;
    AudioController *m_audio;
    QThreadPool m_pool;
    std::atomic_bool m_cancel{false};
    bool m_busy = false;
    QString m_status, m_error;
};
