#pragma once

#include "AndroidAdb.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQuickImageProvider>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <memory>

class NotchController;

// The latest picture of the phone, shared between the mirror (which writes it) and the image provider the window reads it from (image://phone/<n>).
// Shared on purpose: the engine owns and deletes the provider, and this object may outlive it.
struct PhoneFrameState {
    QMutex lock;
    QImage image;
};

class PhoneFrameProvider : public QQuickImageProvider
{
public:
    explicit PhoneFrameProvider(std::shared_ptr<PhoneFrameState> state) : QQuickImageProvider(Image), m_state(std::move(state)) {}
    QImage requestImage(const QString &, QSize *size, const QSize &) override;

private:
    std::shared_ptr<PhoneFrameState> m_state;
};

// An Android phone shown in a window of its own, and touched from there. Everything goes through `adb` (the phone needs USB debugging, or wireless
// debugging on Android 11 and up): the picture comes from `screenrecord` (H.264), ffmpeg turns it into pictures, a click becomes `input tap` / `swipe`.
// Nothing is installed on the phone and nothing is downloaded: without adb the window explains how to get it.
class AndroidMirror : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)             // adb (and ffmpeg) were found
    Q_PROPERTY(QString problem READ problem NOTIFY changed)              // why not, for the person
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)         // [{serial, name, state, wifi, usable}]
    Q_PROPERTY(QString serial READ serial NOTIFY changed)                // the phone on show
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)                  // idle | starting | mirroring
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY frame)                  // grows with every new picture
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY frame)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY frame)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)                       // a search, a pairing or a screenshot is going on
    Q_PROPERTY(bool windowOpen READ windowOpen NOTIFY changed)

public:
    AndroidMirror(NotchController *notch, QObject *parent = nullptr);
    ~AndroidMirror() override;

    PhoneFrameProvider *makeProvider() { return new PhoneFrameProvider(m_frames); }
    bool available() const { return !m_adb.isEmpty() && m_ffmpegFound; }
    QString problem() const { return m_problem; }
    QVariantList devices() const { return m_deviceList; }
    QString serial() const { return m_serial; }
    QString deviceName() const { return m_name; }
    QString state() const { return m_state; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    int revision() const { return m_revision; }
    int frameWidth() const { return m_size.width(); }
    int frameHeight() const { return m_size.height(); }
    bool recording() const { return m_recorder && m_recorder->state() != QProcess::NotRunning; }
    bool busy() const { return m_busy > 0; }
    bool windowOpen() const { return m_windowOpen; }

    Q_INVOKABLE void refresh();                                  // look for adb and phones
    Q_INVOKABLE void pair(const QString &endpoint, const QString &code); // Android 11+: "ip:port" and the 6 digits the phone shows
    Q_INVOKABLE void connectTo(const QString &endpoint);          // "ip:port" of wireless debugging
    Q_INVOKABLE void mirror(const QString &serial);               // opens the window and starts the picture
    Q_INVOKABLE void close();                                     // closes the window and stops everything
    // Touches, with the point as 0..1 across and down the picture.
    Q_INVOKABLE void tap(double nx, double ny);
    Q_INVOKABLE void swipe(double nx1, double ny1, double nx2, double ny2, int ms);
    Q_INVOKABLE void key(const QString &name);                    // back | home | recents | power | volume-up | volume-down | enter | backspace | wake | menu
    Q_INVOKABLE void typeText(const QString &text);
    Q_INVOKABLE void screenshot();                                // to the shelf
    Q_INVOKABLE void startRecording();                            // a video of the picture, to the shelf when it stops
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void debugShow(const QString &what);              // `--shot-page`: a made-up state for a picture

signals:
    void changed();
    void frame();

private:
    void adb(const QStringList &args, int timeoutMs, std::function<void(bool ok, const QByteArray &out, const QString &message)> done, const QString &serial = {});
    void startPipeline();
    void stopPipeline();
    void onFrames();
    void send(const QString &command);
    void fail(const QString &why);
    void setState(const QString &state, const QString &status = {});
    void finishRecording();

    NotchController *m_notch;
    std::shared_ptr<PhoneFrameState> m_frames = std::make_shared<PhoneFrameState>();
    QString m_adb, m_problem, m_serial, m_name, m_state = QStringLiteral("idle"), m_status, m_error, m_ffmpegErr;
    bool m_ffmpegFound = false, m_windowOpen = false, m_stopping = false, m_debug = false;
    QVariantList m_deviceList;
    QList<Android::Device> m_devices;
    QSize m_screen, m_size, m_streamSize;
    int m_revision = 0, m_busy = 0, m_quickFailures = 0;
    qint64 m_pipelineStartedAt = 0;
    QProcess *m_adbProc = nullptr, *m_ffmpeg = nullptr, *m_shell = nullptr, *m_recorder = nullptr;
    QString m_recordFile;
    Android::JpegSplitter m_splitter;
    QTimer m_restart;
};
