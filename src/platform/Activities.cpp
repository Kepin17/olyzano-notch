#include "platform/Activities.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

static QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/activities.conf");
}

// What browsers name a file while they're still writing it.
static bool isPartial(const QString &name)
{
    return name.endsWith(QLatin1String(".part")) || name.endsWith(QLatin1String(".crdownload"))
           || name.endsWith(QLatin1String(".opdownload"));
}

Activities::Activities(QObject *parent)
    : QObject(parent), m_settings(configPath(), QSettings::IniFormat)
{
    m_end.setSingleShot(true);
    connect(&m_end, &QTimer::timeout, this, &Activities::onCountdownEnd);

    m_downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (QFileInfo(m_downloadDir).isDir())
        m_watcher.addPath(m_downloadDir);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Activities::scanDownloads);
    m_downloadPoll.setInterval(1000);
    connect(&m_downloadPoll, &QTimer::timeout, this, &Activities::scanDownloads);
    scanDownloads();

    m_privacyPoll.setInterval(2000);
    connect(&m_privacyPoll, &QTimer::timeout, this, &Activities::pollPrivacy);
    connect(&m_pactl, &QProcess::readyReadStandardOutput, this, [this] { m_pactlOut += m_pactl.readAllStandardOutput(); });
    connect(&m_pactl, &QProcess::finished, this, &Activities::readMicOutputs);
#ifdef Q_OS_LINUX
    if (privacyIndicators())
        m_privacyPoll.start();
#endif
}

void Activities::store(const QString &key, const QVariant &value)
{
    if (m_settings.value(key) == value)
        return;
    m_settings.setValue(key, value);
    emit settingsChanged();
}

void Activities::setShowDownloads(bool on)
{
    store(QStringLiteral("downloads"), on);
    scanDownloads();
}

void Activities::setPrivacyIndicators(bool on)
{
    store(QStringLiteral("privacy"), on);
#ifdef Q_OS_LINUX
    if (on) {
        m_privacyPoll.start();
        pollPrivacy();
        return;
    }
#endif
    m_privacyPoll.stop();
    if (!m_mic.isEmpty() || !m_camera.isEmpty()) {
        m_mic.clear();
        m_camera.clear();
        emit privacyChanged();
    }
}

void Activities::setPhoneAlerts(bool on)
{
    store(QStringLiteral("phone"), on);
}

void Activities::setFocusMinutes(int minutes)
{
    store(QStringLiteral("focus"), qBound(1, minutes, 180));
}

void Activities::setBreakMinutes(int minutes)
{
    store(QStringLiteral("break"), qBound(1, minutes, 60));
}

// ---- Timer ----

qint64 Activities::timerMs() const
{
    if (m_kind.isEmpty())
        return 0;
    if (!m_running)
        return m_baseMs;
    if (m_kind == QLatin1String("stopwatch"))
        return m_baseMs + m_since.elapsed();
    return qMax<qint64>(0, m_baseMs - m_since.elapsed());
}

void Activities::startCountdown(qint64 ms, const QString &label)
{
    m_totalMs = ms;
    m_baseMs = ms;
    m_label = label;
    m_running = true;
    m_since.start();
    m_end.start(int(ms));
    emit timerChanged();
}

void Activities::startTimer(int seconds, const QString &label)
{
    if (seconds <= 0)
        return;
    m_kind = QStringLiteral("timer");
    m_round = 0;
    startCountdown(qint64(seconds) * 1000, label.isEmpty() ? QStringLiteral("Timer") : label);
}

void Activities::startStopwatch()
{
    m_end.stop();
    m_kind = QStringLiteral("stopwatch");
    m_label = QStringLiteral("Stopwatch");
    m_totalMs = 0;
    m_baseMs = 0;
    m_round = 0;
    m_running = true;
    m_since.start();
    emit timerChanged();
}

void Activities::startPomodoro()
{
    m_kind = QStringLiteral("pomodoro");
    m_breakPhase = false;
    m_round = 1;
    startCountdown(qint64(focusMinutes()) * 60000, QStringLiteral("Focus"));
}

void Activities::pause()
{
    if (m_kind.isEmpty() || !m_running)
        return;
    m_baseMs = timerMs();
    m_running = false;
    m_end.stop();
    emit timerChanged();
}

void Activities::resume()
{
    if (m_kind.isEmpty() || m_running)
        return;
    m_running = true;
    m_since.start();
    if (m_kind != QLatin1String("stopwatch"))
        m_end.start(int(m_baseMs));
    emit timerChanged();
}

void Activities::stop()
{
    m_end.stop();
    m_kind.clear();
    m_label.clear();
    m_running = false;
    m_totalMs = m_baseMs = 0;
    m_round = 0;
    emit timerChanged();
}

void Activities::addMinute()
{
    if (m_kind.isEmpty() || m_kind == QLatin1String("stopwatch"))
        return;
    m_baseMs = timerMs() + 60000;
    m_totalMs += 60000;
    if (m_running) {
        m_since.start();
        m_end.start(int(m_baseMs));
    }
    emit timerChanged();
}

void Activities::onCountdownEnd()
{
    if (m_kind == QLatin1String("pomodoro")) {
        // Focus and break follow each other until stopped.
        if (!m_breakPhase) {
            m_breakPhase = true;
            emit timerFinished(QStringLiteral("Time for a break"),
                               QStringLiteral("Round %1 done · %2 min break").arg(m_round).arg(breakMinutes()));
            startCountdown(qint64(breakMinutes()) * 60000, QStringLiteral("Break"));
        } else {
            m_breakPhase = false;
            ++m_round;
            emit timerFinished(QStringLiteral("Back to focus"), QStringLiteral("Round %1").arg(m_round));
            startCountdown(qint64(focusMinutes()) * 60000, QStringLiteral("Focus"));
        }
        return;
    }
    const QString label = m_label;
    const qint64 total = m_totalMs;
    stop();
    emit timerFinished(QStringLiteral("Time's up"), label == QLatin1String("Timer")
                                                        ? QStringLiteral("%1 min timer").arg(qMax<qint64>(1, total / 60000))
                                                        : label);
}

// ---- Downloads ----

void Activities::scanDownloads()
{
    QVariantList list;
    QHash<QString, Download> seen;
    if (showDownloads() && !m_downloadDir.isEmpty()) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QFileInfoList files = QDir(m_downloadDir).entryInfoList(QDir::Files | QDir::Hidden, QDir::Time);
        for (const QFileInfo &f : files) {
            if (!isPartial(f.fileName()))
                continue;
            // Leftovers of a cancelled or crashed download stop growing: leave them out.
            if (now - f.lastModified().toMSecsSinceEpoch() > 30000)
                continue;
            Download d = m_downloads.value(f.absoluteFilePath());
            if (d.at > 0 && now > d.at) {
                const double instant = double(f.size() - d.bytes) * 1000.0 / double(now - d.at);
                d.speed = d.speed > 0 ? d.speed * 0.6 + instant * 0.4 : instant; // smoothed
            }
            d.bytes = f.size();
            d.at = now;
            seen.insert(f.absoluteFilePath(), d);
            QString name = f.fileName();
            name.chop(name.size() - name.lastIndexOf(u'.')); // drop .part / .crdownload
            list << QVariantMap{{QStringLiteral("name"), name},
                                {QStringLiteral("bytes"), d.bytes},
                                {QStringLiteral("speed"), qMax(0.0, d.speed)}};
        }
    }
    m_downloads = seen;
    // The folder only reports new and removed files; growing ones are polled.
    if (seen.isEmpty())
        m_downloadPoll.stop();
    else if (!m_downloadPoll.isActive())
        m_downloadPoll.start();
    if (list != m_downloadList) {
        m_downloadList = list;
        emit downloadsChanged();
    }
}

// ---- Microphone and camera ----

void Activities::pollPrivacy()
{
#ifdef Q_OS_LINUX
    // Camera: a process holding /dev/video* open. Only looked for when there is a camera device.
    QStringList camera;
    if (!QDir(QStringLiteral("/dev")).entryList({QStringLiteral("video*")}, QDir::System).isEmpty()) {
        const QString self = QString::number(QCoreApplication::applicationPid());
        const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &pid : pids) {
            if (pid == self || !pid.at(0).isDigit())
                continue;
            const QString fdDir = QStringLiteral("/proc/") + pid + QStringLiteral("/fd");
            const QStringList fds = QDir(fdDir).entryList(QDir::System | QDir::NoDotAndDotDot);
            for (const QString &fd : fds) {
                if (!QFileInfo(fdDir + u'/' + fd).symLinkTarget().startsWith(QLatin1String("/dev/video")))
                    continue;
                QFile comm(QStringLiteral("/proc/") + pid + QStringLiteral("/comm"));
                const QString name = comm.open(QIODevice::ReadOnly) ? QString::fromUtf8(comm.readAll()).trimmed() : pid;
                if (!camera.contains(name))
                    camera << name;
                break;
            }
        }
    }
    if (camera != m_camera) {
        m_camera = camera;
        emit privacyChanged();
    }

    // Microphone: recording streams, minus those recording a monitor (what's playing, e.g. Notch's equalizer).
    if (m_pactl.state() == QProcess::NotRunning) {
        m_pactlOut.clear();
        m_pactl.start(QStringLiteral("sh"), {QStringLiteral("-c"),
                                             QStringLiteral("pactl -f json list sources && echo && pactl -f json list source-outputs")});
    }
#endif
}

void Activities::readMicOutputs()
{
    const QList<QByteArray> docs = m_pactlOut.trimmed().split('\n');
    if (docs.size() < 2)
        return; // no pactl, or one without JSON output
    QHash<int, bool> monitor;
    for (const QJsonValue &v : QJsonDocument::fromJson(docs.first()).array()) {
        const QJsonObject source = v.toObject();
        const QString name = source.value(QLatin1String("name")).toString();
        monitor.insert(source.value(QLatin1String("index")).toInt(), name.endsWith(QLatin1String(".monitor")));
    }
    QStringList mic;
    for (const QJsonValue &v : QJsonDocument::fromJson(docs.last()).array()) {
        const QJsonObject stream = v.toObject();
        if (monitor.value(stream.value(QLatin1String("source")).toInt(), false))
            continue;
        const QJsonObject props = stream.value(QLatin1String("properties")).toObject();
        QString name = props.value(QLatin1String("application.name")).toString();
        if (name.isEmpty())
            name = props.value(QLatin1String("application.process.binary")).toString();
        if (!name.isEmpty() && !mic.contains(name))
            mic << name;
    }
    if (mic != m_mic) {
        m_mic = mic;
        emit privacyChanged();
    }
}
