#pragma once

#include "audio/AudioController.h"

#include <QObject>
#include <QThreadPool>

#include <atomic>

class NotchController;

// Taking the background off a picture, with a model on this computer (IS-Net, an optional download) run by ONNX Runtime (a downloaded helper).
// `cutout` makes a PNG with transparency in the scratch folder (for a preview, or for a stamp); `removeBackground` does it for a picture of the
// shelf or a file and puts the result on the shelf. One picture at a time, on a worker thread, with a stop button.
class MattingController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool supported READ supported CONSTANT)           // this build can run models at all
    Q_PROPERTY(bool ready READ ready NOTIFY changed)             // runtime and a model are there
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString model READ model NOTIFY changed)          // the model that will be used ("" when none is downloaded)

public:
    MattingController(NotchController *notch, AudioController *audio, QObject *parent = nullptr);
    ~MattingController() override;

    bool busy() const { return m_busy; }
    bool supported() const;
    bool ready() const;
    QString status() const { return m_status; }
    QString lastError() const { return m_error; }
    QString model() const;

    // `ref`: a picture of the shelf (its id) or a file (path or file:// URL). The result file is a PNG; `cutoutReady` says where, or what went wrong.
    Q_INVOKABLE void cutout(const QString &ref, int featherPx = 1);
    Q_INVOKABLE void removeBackground(const QString &ref, int featherPx = 1); // … and onto the shelf
    Q_INVOKABLE bool addToShelf(const QString &cutoutFileUrl, const QString &baseName);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();

signals:
    void changed();
    void cutoutReady(const QString &fileUrl, const QString &error); // fileUrl "" on failure

private:
    QString libraryPath() const;
    void start(const QString &ref, int featherPx, bool toShelf);

    NotchController *m_notch;
    AudioController *m_audio;
    QThreadPool m_pool;
    std::atomic_bool m_cancel{false};
    bool m_busy = false;
    int m_serial = 0;
    QString m_status, m_error;
};
