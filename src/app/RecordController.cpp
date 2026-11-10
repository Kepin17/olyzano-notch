#include "RecordController.h"

#include "NotchController.h"
#include "platform/ChildLife.h"
#include "platform/NotchPaths.h"
#include "platform/ProcessRunner.h"
#include "services/RecordMix.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QQmlComponent>
#include <QRegularExpression>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QtConcurrent>

namespace {
bool have(const char *program) { return !QStandardPaths::findExecutable(QString::fromLatin1(program)).isEmpty(); }

// a file name inside the session folder, nothing else
QString inside(const QString &session, const QString &name)
{
    if (name.isEmpty() || name != QFileInfo(name).fileName())
        return {};
    return QDir(session).filePath(name);
}
}

RecordController::RecordController(NotchController *notch, QObject *parent)
    : QObject(parent), m_notch(notch), m_prefs(NotchPaths::conf(QStringLiteral("camera")), QSettings::IniFormat)
{
    m_pool.setMaxThreadCount(1);
}

void RecordController::setPref(const QString &key, const QVariant &value)
{
    m_prefs.setValue(key, value);
    emit prefsChanged();
}

void RecordController::publishState(const QString &phase, int elapsed, bool previewing, const QString &target, bool held)
{
    if (phase == m_phase && elapsed == m_elapsed && previewing == m_previewing && target == m_target && held == m_held)
        return;
    m_held = held;
    m_target = target;
    m_phase = phase;
    m_elapsed = elapsed;
    m_previewing = previewing;
    emit stateChanged();
}

QObject *RecordController::newScreenCapture()
{
    if (!m_engine)
        return nullptr;
    QQmlComponent component(m_engine);
    component.setData("import QtMultimedia\nScreenCapture {}", QUrl());
    QObject *capture = component.create();
    if (!capture) {
        qWarning() << "ScreenCapture could not be made:" << component.errorString();
        return nullptr;
    }
    QQmlEngine::setObjectOwnership(capture, QQmlEngine::CppOwnership); // no parent, no owner, not deleted: see the header
    return capture;
}

RecordController::~RecordController()
{
    stopSystemSound();
    m_cancel = true;
    m_pool.waitForDone(5000);
}

bool RecordController::canRecordSystem() const { return have("parec"); }
bool RecordController::canMix() const { return have("ffmpeg") && have("ffprobe"); }

QString RecordController::newSession()
{
    const QString dir = m_notch->scratchPath(false) + QStringLiteral("/record-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    return QDir().mkpath(dir) ? dir : QString();
}

QVariantList RecordController::soundSources() const
{
    QVariantList out;
    if (!have("pactl"))
        return out;
    const auto r = ProcessRunner::run(QStringLiteral("pactl"), {QStringLiteral("--format=json"), QStringLiteral("list"), QStringLiteral("sources")}, 5000);
    const QJsonArray all = QJsonDocument::fromJson(r.out).array();
    for (const QJsonValue &v : all) {
        const QJsonObject s = v.toObject();
        const QString name = s.value(QStringLiteral("name")).toString();
        if (!name.endsWith(QLatin1String(".monitor")))
            continue; // the microphones are chosen in the picture's own list
        out.append(QVariantMap{{QStringLiteral("id"), name}, {QStringLiteral("name"), s.value(QStringLiteral("description")).toString()}});
    }
    return out;
}

bool RecordController::startSystemSound(const QString &session, const QString &sourceId)
{
    stopSystemSound();
    if (!canRecordSystem()) {
        m_error = tr("The sound of the computer needs parec (package pulseaudio-utils).");
        emit changed();
        return false;
    }
    const QString file = inside(session, QStringLiteral("system.wav"));
    if (file.isEmpty() || !QDir(session).exists())
        return false;
    QStringList args;
    if (!sourceId.isEmpty())
        args << QStringLiteral("--device=") + sourceId;
    else
        args << QStringLiteral("--device=@DEFAULT_MONITOR@");
    args << QStringLiteral("--file-format=wav") << QStringLiteral("--rate=48000") << QStringLiteral("--channels=2") << QStringLiteral("--client-name=OlyzanoRecorder") << file;
    ChildLife::dieWithParent(m_parec);
    m_parec.start(QStringLiteral("parec"), args);
    return m_parec.waitForStarted(3000);
}

void RecordController::stopSystemSound()
{
    if (m_parec.state() == QProcess::NotRunning)
        return;
    m_parec.terminate(); // SIGTERM: parec closes the WAV properly
    if (!m_parec.waitForFinished(2000)) {
        m_parec.kill();
        m_parec.waitForFinished(500);
    }
}

void RecordController::cancel() { m_cancel = true; }
void RecordController::clearError() { m_error.clear(); emit changed(); }

void RecordController::discard(const QString &session)
{
    if (QFileInfo(session).fileName().startsWith(QLatin1String("record-")) && QFileInfo(session).absolutePath() == QFileInfo(m_notch->scratchPath(false) + QStringLiteral("/x")).absolutePath())
        QDir(session).removeRecursively();
}

void RecordController::mix(const QVariantMap &v)
{
    if (m_mixing)
        return;
    const auto fail = [this](const QString &why) { m_error = why; emit changed(); emit finished(why); };
    if (!canMix()) {
        fail(tr("Putting the recording together needs ffmpeg (package ffmpeg)."));
        return;
    }
    stopSystemSound();
    const QString session = v.value(QStringLiteral("session")).toString();
    RecordMix::Options o;
    o.screen = inside(session, v.value(QStringLiteral("screen")).toString());
    const QString cam = v.value(QStringLiteral("camera")).toString(), sys = v.value(QStringLiteral("system")).toString();
    o.camera = cam.isEmpty() ? QString() : inside(session, cam);
    o.system = sys.isEmpty() ? QString() : inside(session, sys);
    o.screenHasSound = v.value(QStringLiteral("screenHasSound")).toBool();
    o.cameraOffset = v.value(QStringLiteral("cameraOffset")).toDouble();
    o.systemOffset = v.value(QStringLiteral("systemOffset")).toDouble();
    o.x = v.value(QStringLiteral("x"), 1.0).toDouble();
    o.y = v.value(QStringLiteral("y"), 1.0).toDouble();
    o.size = v.value(QStringLiteral("size"), 0.22).toDouble();
    o.mirror = v.value(QStringLiteral("mirror"), true).toBool();
    o.brightness = v.value(QStringLiteral("brightness"), 0.0).toDouble();
    o.contrast = v.value(QStringLiteral("contrast"), 1.0).toDouble();
    o.micVolume = v.value(QStringLiteral("micVolume"), 1.0).toDouble();
    o.systemVolume = v.value(QStringLiteral("systemVolume"), 1.0).toDouble();
    const QString shape = v.value(QStringLiteral("shape")).toString();
    o.shape = shape == QLatin1String("circle") ? RecordMix::Shape::Circle : shape == QLatin1String("square") ? RecordMix::Shape::Square : RecordMix::Shape::Rounded;
    if (!o.system.isEmpty() && (!QFileInfo(o.system).isFile() || QFileInfo(o.system).size() < 64))
        o.system.clear(); // nothing was heard
    if (o.screen.isEmpty() || !QFileInfo(o.screen).isFile() || QFileInfo(o.screen).size() == 0) {
        fail(tr("Nothing was recorded."));
        return;
    }
    QString app = v.value(QStringLiteral("app")).toString(); // the application that was recorded, when it is known: part of the name
    app.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    app = app.simplified().left(40);
    const QString when = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm"));
    const QString name = app.isEmpty() ? tr("Screen recording %1.mp4").arg(when) : tr("Screen recording %1 %2.mp4").arg(app, when);
    o.output = QDir(session).filePath(QStringLiteral("out-%1.mp4").arg(++m_serial));

    m_mixing = true;
    m_cancel = false;
    m_error.clear();
    m_status = tr("Putting the recording together…");
    emit changed();
    (void)QtConcurrent::run(&m_pool, [this, o, name, session]() mutable {
        QString error;
        // the size and length of the main picture
        const auto probe = ProcessRunner::run(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"), QStringLiteral("v:0"),
            QStringLiteral("-show_entries"), QStringLiteral("stream=width,height:format=duration"), QStringLiteral("-of"), QStringLiteral("json"), o.screen}, 30000);
        const QJsonObject j = QJsonDocument::fromJson(probe.out).object();
        const QJsonObject st = j.value(QStringLiteral("streams")).toArray().isEmpty() ? QJsonObject() : j.value(QStringLiteral("streams")).toArray().first().toObject();
        o.screenWidth = st.value(QStringLiteral("width")).toInt();
        o.screenHeight = st.value(QStringLiteral("height")).toInt();
        o.seconds = j.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble();
        error = RecordMix::validate(o);
        if (error.isEmpty() && o.screenWidth < 16)
            error = tr("The recording cannot be read.");
        if (error.isEmpty()) {
            const auto r = ProcessRunner::run(QStringLiteral("ffmpeg"), RecordMix::args(o), 4 * 3600 * 1000);
            if (m_cancel)
                error = QStringLiteral("cancelled");
            else if (!r.ok)
                error = tr("ffmpeg could not put the recording together: %1").arg(QString::fromUtf8(r.err).trimmed().right(300));
        }
        QMetaObject::invokeMethod(this, [this, error, o, name, session] {
            m_mixing = false;
            m_status.clear();
            QString shown = error;
            if (error == QLatin1String("cancelled")) {
                shown.clear();
            } else if (error.isEmpty()) {
                if (!m_notch->adoptLocalFile(o.output, name, tr("Recorded with Notch"), QString()).isEmpty()) {
                    m_notch->announce(tr("Recording saved"), name, QStringLiteral("video-x-generic"));
                    discard(session);
                } else {
                    shown = tr("The recording cannot be put on the shelf. The files are in %1").arg(session);
                }
            } else {
                shown += tr(" The recorded files are kept in %1").arg(session);
            }
            if (!shown.isEmpty())
                m_error = shown;
            emit changed();
            emit finished(shown);
        }, Qt::QueuedConnection);
    });
}
