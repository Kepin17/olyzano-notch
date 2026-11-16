#include "AndroidMirror.h"

#include "app/NotchController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QColor>
#include <QMutexLocker>
#include <QPainter>
#include <QStandardPaths>

namespace {
constexpr int Fps = 15;

QString findAdb()
{
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("adb"));
    if (!onPath.isEmpty())
        return onPath;
    for (const QString &c : Android::adbCandidates(QDir::homePath(), qEnvironmentVariable("ANDROID_HOME", qEnvironmentVariable("ANDROID_SDK_ROOT")), qEnvironmentVariable("LOCALAPPDATA")))
        if (QFile::exists(c))
            return c;
    return {};
}
} // namespace

QImage PhoneFrameProvider::requestImage(const QString &, QSize *size, const QSize &)
{
    QMutexLocker lock(&m_state->lock);
    if (size)
        *size = m_state->image.size();
    return m_state->image;
}

AndroidMirror::AndroidMirror(NotchController *notch, QObject *parent) : QObject(parent), m_notch(notch)
{
    m_restart.setSingleShot(true);
    m_restart.setInterval(200);
    connect(&m_restart, &QTimer::timeout, this, [this] { if (m_state != QLatin1String("idle") && !m_stopping) startPipeline(); });
}

AndroidMirror::~AndroidMirror()
{
    m_stopping = true;
    for (QProcess *p : {m_adbProc, m_ffmpeg, m_shell, m_recorder})
        if (p) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(500);
        }
}

void AndroidMirror::setState(const QString &state, const QString &status)
{
    m_state = state;
    m_status = status;
    emit changed();
}

void AndroidMirror::fail(const QString &why)
{
    stopPipeline();
    m_error = why;
    setState(QStringLiteral("idle"));
}

void AndroidMirror::clearError()
{
    m_error.clear();
    emit changed();
}

// One adb call, not waiting: `done` gets whether it went well, what it printed and a short message when it did not. Ends by itself after `timeoutMs`.
void AndroidMirror::adb(const QStringList &args, int timeoutMs, std::function<void(bool, const QByteArray &, const QString &)> done, const QString &serial)
{
    auto *p = new QProcess(this);
    QStringList all;
    if (!serial.isEmpty())
        all << QStringLiteral("-s") << serial;
    all << args;
    auto *timer = new QTimer(p);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, p, [p] { p->kill(); });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            done(false, {}, tr("adb could not be started."));
            p->deleteLater();
        }
    });
    connect(p, &QProcess::finished, this, [p, done](int code, QProcess::ExitStatus status) {
        const QByteArray out = p->readAllStandardOutput();
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        done(status == QProcess::NormalExit && code == 0, out, err.isEmpty() ? QString::fromUtf8(out).trimmed().section(u'\n', 0, 0) : err.section(u'\n', 0, 0));
        p->deleteLater();
    });
    p->start(m_adb, all);
    timer->start(timeoutMs);
}

void AndroidMirror::refresh()
{
    if (m_debug) { // a made-up state for a picture stays as it is
        emit changed();
        return;
    }
    m_adb = findAdb();
    m_ffmpegFound = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    m_problem.clear();
    if (m_adb.isEmpty())
        m_problem = tr("adb was not found. Install the Android platform tools (Debian and Ubuntu: sudo apt install adb; Windows: platform-tools from developer.android.com), or set ANDROID_HOME, then press Look again.");
    else if (!m_ffmpegFound)
        m_problem = tr("ffmpeg was not found: it turns the phone's picture into something Notch can show.");
    if (!available()) {
        m_deviceList.clear();
        emit changed();
        return;
    }
    ++m_busy;
    emit changed();
    adb({QStringLiteral("devices"), QStringLiteral("-l")}, 8000, [this](bool ok, const QByteArray &out, const QString &message) {
        --m_busy;
        m_devices = ok ? Android::parseDevices(out) : QList<Android::Device>();
        if (!ok)
            m_error = tr("adb did not answer: %1").arg(message);
        m_deviceList.clear();
        for (const Android::Device &d : std::as_const(m_devices))
            m_deviceList << QVariantMap{{QStringLiteral("serial"), d.serial}, {QStringLiteral("name"), d.model.isEmpty() ? d.serial : d.model}, {QStringLiteral("state"), d.state},
                                        {QStringLiteral("wifi"), d.wifi}, {QStringLiteral("usable"), d.usable()}};
        emit changed();
    });
}

void AndroidMirror::pair(const QString &endpoint, const QString &code)
{
    static const QRegularExpression digits(QStringLiteral("^\\d{6}$"));
    if (!Android::validEndpoint(endpoint.trimmed()) || !digits.match(code.trimmed()).hasMatch()) {
        m_error = tr("Type the address and port the phone shows under Pair device with pairing code (like 192.168.1.20:37099), and the 6 digits.");
        emit changed();
        return;
    }
    if (!available())
        return;
    ++m_busy;
    m_error.clear();
    emit changed();
    adb({QStringLiteral("pair"), endpoint.trimmed(), code.trimmed()}, 20000, [this](bool, const QByteArray &out, const QString &message) {
        --m_busy;
        const QString text = QString::fromUtf8(out) + message;
        m_error = Android::connectedOrPaired(text) ? QString() : tr("The phone did not accept the pairing: %1").arg(message.isEmpty() ? tr("no answer") : message);
        m_status = m_error.isEmpty() ? tr("Paired. Now type the address and port from the main Wireless debugging screen and press Connect.") : QString();
        emit changed();
    });
}

void AndroidMirror::connectTo(const QString &endpoint)
{
    if (!Android::validEndpoint(endpoint.trimmed())) {
        m_error = tr("Type the address and port shown on the Wireless debugging screen of the phone (like 192.168.1.20:5555).");
        emit changed();
        return;
    }
    if (!available())
        return;
    ++m_busy;
    m_error.clear();
    emit changed();
    adb({QStringLiteral("connect"), endpoint.trimmed()}, 15000, [this](bool, const QByteArray &out, const QString &message) {
        --m_busy;
        const QString text = QString::fromUtf8(out) + message;
        m_error = Android::connectedOrPaired(text) ? QString() : tr("Could not connect: %1").arg(message.isEmpty() ? tr("no answer") : message);
        emit changed();
        refresh();
    });
}

void AndroidMirror::mirror(const QString &serial)
{
    if (!available())
        return;
    const Android::Device *found = nullptr;
    for (const Android::Device &d : std::as_const(m_devices))
        if (d.serial == serial)
            found = &d;
    if (!found || !found->usable()) {
        m_error = found && found->state == QLatin1String("unauthorized") ? tr("The phone has not allowed this computer yet: look at its screen and tap Allow USB debugging.")
                                                                         : tr("That phone is not ready. Check the cable or the wireless connection and press Look again.");
        emit changed();
        return;
    }
    stopPipeline();
    m_serial = serial;
    m_name = found->model.isEmpty() ? serial : found->model;
    m_error.clear();
    m_windowOpen = true;
    m_quickFailures = 0;
    m_size = QSize();
    {
        QMutexLocker lock(&m_frames->lock);
        m_frames->image = QImage();
    }
    setState(QStringLiteral("starting"), tr("Asking the phone for its screen…"));
    adb({QStringLiteral("shell"), QStringLiteral("wm"), QStringLiteral("size")}, 8000, [this, serial](bool ok, const QByteArray &out, const QString &message) {
        if (serial != m_serial || m_state != QLatin1String("starting"))
            return;
        m_screen = ok ? Android::parseWmSize(QString::fromUtf8(out)) : QSize();
        if (m_screen.isEmpty()) {
            fail(tr("The phone did not tell its screen size: %1").arg(message));
            return;
        }
        m_streamSize = Android::streamSize(m_screen);
        startPipeline();
    }, serial);
}

void AndroidMirror::startPipeline()
{
    stopPipeline();
    m_stopping = false;
    m_splitter.reset();
    m_ffmpegErr.clear();
    m_pipelineStartedAt = QDateTime::currentMSecsSinceEpoch();
    m_adbProc = new QProcess(this);
    m_ffmpeg = new QProcess(this);
    // the phone's H.264 goes straight into ffmpeg, which writes a steady stream of JPEG pictures
    m_adbProc->setStandardOutputProcess(m_ffmpeg);
    m_adbProc->setReadChannel(QProcess::StandardError);
    connect(m_ffmpeg, &QProcess::readyReadStandardOutput, this, &AndroidMirror::onFrames);
    connect(m_ffmpeg, &QProcess::readyReadStandardError, this, [this] { m_ffmpegErr = (m_ffmpegErr + QString::fromUtf8(m_ffmpeg->readAllStandardError())).right(400); });
    QPointer<QProcess> mine = m_adbProc;
    connect(m_adbProc, &QProcess::finished, this, [this, mine](int, QProcess::ExitStatus) {
        if (m_stopping || mine != m_adbProc)
            return;
        const QString why = QString::fromUtf8(mine->readAllStandardError()).trimmed().section(u'\n', 0, 0);
        const bool quick = QDateTime::currentMSecsSinceEpoch() - m_pipelineStartedAt < 4000;
        m_quickFailures = quick ? m_quickFailures + 1 : 0;
        if (m_quickFailures >= 3) { // screenrecord ends by itself every 3 minutes: that is normal; ending at once, three times, is not
            fail(tr("The phone stopped sending its screen%1").arg(why.isEmpty() ? QString() : QStringLiteral(": ") + why));
            return;
        }
        m_restart.start();
    });
    connect(m_adbProc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) { if (e == QProcess::FailedToStart) fail(tr("adb could not be started.")); });
    connect(m_ffmpeg, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) { if (e == QProcess::FailedToStart) fail(tr("ffmpeg could not be started.")); });
    const QStringList ffmpegArgs = {QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-flags"), QStringLiteral("low_delay"),
                                    QStringLiteral("-probesize"), QStringLiteral("32768"), QStringLiteral("-analyzeduration"), QStringLiteral("0"), QStringLiteral("-f"), QStringLiteral("h264"),
                                    QStringLiteral("-i"), QStringLiteral("pipe:0"), QStringLiteral("-vf"), QStringLiteral("fps=%1,format=yuvj420p").arg(Fps), QStringLiteral("-f"), QStringLiteral("image2pipe"),
                                    QStringLiteral("-c:v"), QStringLiteral("mjpeg"), QStringLiteral("-q:v"), QStringLiteral("5"), QStringLiteral("pipe:1")};
    m_ffmpeg->start(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")), ffmpegArgs);
    m_adbProc->start(m_adb, {QStringLiteral("-s"), m_serial, QStringLiteral("exec-out"), QStringLiteral("screenrecord"), QStringLiteral("--output-format=h264"),
                             QStringLiteral("--size"), QStringLiteral("%1x%2").arg(m_streamSize.width()).arg(m_streamSize.height()), QStringLiteral("--bit-rate"), QStringLiteral("4000000"),
                             QStringLiteral("--time-limit"), QStringLiteral("180"), QStringLiteral("-")});
}

void AndroidMirror::stopPipeline()
{
    m_stopping = true;
    m_restart.stop();
    finishRecording();
    for (QProcess **p : {&m_adbProc, &m_ffmpeg, &m_shell}) {
        if (!*p)
            continue;
        (*p)->disconnect(this);
        (*p)->kill();
        (*p)->waitForFinished(300);
        (*p)->deleteLater();
        *p = nullptr;
    }
    m_stopping = false;
}

void AndroidMirror::onFrames()
{
    if (!m_ffmpeg)
        return;
    const QList<QByteArray> pictures = m_splitter.feed(m_ffmpeg->readAllStandardOutput());
    if (pictures.isEmpty())
        return;
    if (recording())
        for (const QByteArray &p : pictures)
            m_recorder->write(p);
    const QImage image = QImage::fromData(pictures.last(), "JPEG"); // only the newest is shown: a slow window never falls behind
    if (image.isNull())
        return;
    {
        QMutexLocker lock(&m_frames->lock);
        m_frames->image = image;
    }
    m_size = image.size();
    ++m_revision;
    m_quickFailures = 0;
    if (m_state != QLatin1String("mirroring"))
        setState(QStringLiteral("mirroring"));
    emit frame();
}

void AndroidMirror::close()
{
    stopPipeline();
    m_windowOpen = false;
    setState(QStringLiteral("idle"));
}

void AndroidMirror::send(const QString &command)
{
    if (m_state != QLatin1String("mirroring") || m_serial.isEmpty())
        return;
    if (!m_shell) { // one shell for all touches: starting adb again for each one would be slow
        m_shell = new QProcess(this);
        connect(m_shell, &QProcess::finished, this, [this](int, QProcess::ExitStatus) { if (m_shell) { m_shell->deleteLater(); m_shell = nullptr; } });
        m_shell->start(m_adb, {QStringLiteral("-s"), m_serial, QStringLiteral("shell")});
    }
    m_shell->write(command.toUtf8() + '\n');
}

void AndroidMirror::tap(double nx, double ny) { send(Android::tapCommand(Android::toDevice(nx, ny, m_size, m_screen))); }

void AndroidMirror::swipe(double nx1, double ny1, double nx2, double ny2, int ms)
{
    send(Android::swipeCommand(Android::toDevice(nx1, ny1, m_size, m_screen), Android::toDevice(nx2, ny2, m_size, m_screen), ms));
}

void AndroidMirror::key(const QString &name)
{
    if (const QString c = Android::keyCommand(name); !c.isEmpty())
        send(c);
}

void AndroidMirror::typeText(const QString &text)
{
    QString why;
    const QString c = Android::textCommand(text, &why);
    if (c.isEmpty()) {
        m_error = why;
        emit changed();
        return;
    }
    send(c);
}

void AndroidMirror::screenshot()
{
    if (m_serial.isEmpty() || !available())
        return;
    ++m_busy;
    emit changed();
    adb({QStringLiteral("exec-out"), QStringLiteral("screencap"), QStringLiteral("-p")}, 15000, [this](bool ok, const QByteArray &out, const QString &message) {
        --m_busy;
        const QString name = tr("Phone screenshot %1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss")));
        if (!ok || out.size() < 100 || !out.startsWith("\x89PNG")) {
            m_error = tr("The screenshot failed: %1").arg(message.isEmpty() ? tr("the phone sent nothing") : message);
        } else if (m_notch->addDocument(name, QStringLiteral("image/png"), out, tr("Screenshot of %1 with Notch").arg(m_name))) {
            m_notch->announce(tr("Screenshot saved"), name, QStringLiteral("image-x-generic"));
        } else {
            m_error = tr("The screenshot could not be put on the shelf.");
        }
        emit changed();
    }, m_serial);
}

void AndroidMirror::startRecording()
{
    if (recording() || m_state != QLatin1String("mirroring"))
        return;
    if (m_size.isEmpty())
        return;
    m_recordFile = m_notch->scratchPath(false) + QStringLiteral("/phone-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".mp4");
    QDir().mkpath(QFileInfo(m_recordFile).absolutePath());
    m_recorder = new QProcess(this);
    // the pictures come at a steady rate, so each one is one step of the video
    m_recorder->start(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")),
                      {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("image2pipe"), QStringLiteral("-framerate"), QString::number(Fps),
                       QStringLiteral("-c:v"), QStringLiteral("mjpeg"), QStringLiteral("-i"), QStringLiteral("pipe:0"), QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"),
                       QStringLiteral("veryfast"), QStringLiteral("-crf"), QStringLiteral("24"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-movflags"), QStringLiteral("+faststart"),
                       m_recordFile});
    emit changed();
}

void AndroidMirror::stopRecording() { finishRecording(); }

void AndroidMirror::finishRecording()
{
    if (!m_recorder)
        return;
    QProcess *r = m_recorder;
    m_recorder = nullptr;
    const QString file = m_recordFile, name = m_name;
    emit changed();
    r->closeWriteChannel();
    connect(r, &QProcess::finished, this, [this, r, file, name](int code, QProcess::ExitStatus) {
        const QString err = QString::fromUtf8(r->readAllStandardError()).trimmed().right(200);
        r->deleteLater();
        const QString shelf = tr("Phone recording %1.mp4").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss")));
        if (code == 0 && QFileInfo(file).size() > 0 && !m_notch->adoptLocalFile(file, shelf, tr("Recorded from %1 with Notch").arg(name)).isEmpty()) {
            m_notch->announce(tr("Phone recording saved"), shelf, QStringLiteral("video-x-generic"));
            QFile::remove(file);
        } else {
            m_error = tr("The phone recording could not be put together%1. The file is %2").arg(err.isEmpty() ? QString() : QStringLiteral(" (") + err + QStringLiteral(")"), file);
        }
        emit changed();
    });
    QTimer::singleShot(60000, r, [r] { if (r->state() != QProcess::NotRunning) r->kill(); });
}

// for pictures: `notch --shot-page x.png phone-demo` makes a frame and a state without a phone
void AndroidMirror::debugShow(const QString &what)
{
    m_debug = true;
    m_ffmpegFound = true;
    m_adb = QStringLiteral("/usr/bin/adb");
    m_problem.clear();
    m_devices = {{QStringLiteral("R58M123ABC"), QStringLiteral("device"), QStringLiteral("Pixel 7"), false}, {QStringLiteral("192.168.1.20:5555"), QStringLiteral("device"), QStringLiteral("Galaxy S23"), true},
                 {QStringLiteral("ZX1G22"), QStringLiteral("unauthorized"), QString(), false}};
    m_deviceList.clear();
    for (const Android::Device &d : std::as_const(m_devices))
        m_deviceList << QVariantMap{{QStringLiteral("serial"), d.serial}, {QStringLiteral("name"), d.model.isEmpty() ? d.serial : d.model}, {QStringLiteral("state"), d.state}, {QStringLiteral("wifi"), d.wifi}, {QStringLiteral("usable"), d.usable()}};
    if (what == QLatin1String("none")) {
        m_adb.clear();
        m_problem = tr("adb was not found. Install the Android platform tools (Debian and Ubuntu: sudo apt install adb; Windows: platform-tools from developer.android.com), or set ANDROID_HOME, then press Look again.");
        m_deviceList.clear();
    }
    if (what == QLatin1String("window")) {
        QImage img(576, 1280, QImage::Format_RGB32);
        img.fill(QColor(24, 34, 52));
        QPainter p(&img);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 90, 160));
        p.drawRoundedRect(QRect(40, 120, 496, 120), 24, 24);
        for (int i = 0; i < 4; ++i) {
            p.setBrush(QColor(40 + i * 25, 60 + i * 20, 100));
            p.drawRoundedRect(QRect(40, 300 + i * 150, 496, 120), 24, 24);
        }
        p.end();
        {
            QMutexLocker lock(&m_frames->lock);
            m_frames->image = img;
        }
        m_size = img.size();
        m_screen = QSize(1080, 2400);
        m_serial = QStringLiteral("R58M123ABC");
        m_name = QStringLiteral("Pixel 7");
        m_windowOpen = true;
        ++m_revision;
        m_state = QStringLiteral("mirroring");
        emit frame();
    }
    emit changed();
}
