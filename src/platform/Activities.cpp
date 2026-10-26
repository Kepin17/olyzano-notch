#include "platform/Activities.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include "platform/WinPrivacy.h"
#endif

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

    m_privacyPoll.setInterval(5000); // a mute toggled from the notch re-reads at once (toggleMicMute)
    connect(&m_privacyPoll, &QTimer::timeout, this, &Activities::pollPrivacy);
    connect(&m_pactl, &QProcess::readyReadStandardOutput, this, [this] { m_pactlOut += m_pactl.readAllStandardOutput(); });
    connect(&m_pactl, &QProcess::finished, this, &Activities::readMicOutputs);
    connect(&m_micTap, &QProcess::readyReadStandardOutput, this, &Activities::onMicTap);
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    m_privacyPoll.start(); // the microphone's mute state is also shown by the idle clock
#endif
#ifdef Q_OS_WIN
    m_discordPoll.setInterval(3000);
    connect(&m_discordPoll, &QTimer::timeout, this, &Activities::pollDiscord);
    m_discordPoll.start();
#endif
#ifdef NOTCH_DISCORD_RPC
    connect(&m_rpc, &DiscordRpc::changed, this, &Activities::discordChanged);
    m_rpc.setEnabled(discordRpc());
#endif
}

void Activities::setDiscordRpc(bool on)
{
    store(QStringLiteral("discordRpc"), on);
#ifdef NOTCH_DISCORD_RPC
    m_rpc.setEnabled(on);
#endif
}

// Discord's call state from its audio session on the microphone (see WinAudioSessions).
void Activities::pollDiscord()
{
#ifdef Q_OS_WIN
    const DiscordAudio audio = m_sessions.read();
    const bool active = audio.captureActive;
    const CallState before = m_call.state;
    m_call.update(active, audio.capturePeak);
    m_discordPoll.setInterval(active ? 120 : 3000); // quick while a call can be followed, lazy otherwise
    if (m_call.state != before)
        emit discordChanged();
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
    if (!on && !m_camera.isEmpty()) {
        m_camera.clear();
        emit privacyChanged();
    }
    pollPrivacy();
}

void Activities::setIdleClock(bool on)
{
    store(QStringLiteral("idleClock"), on);
}

QString Activities::eqStyle() const
{
    const QString style = m_settings.value(QStringLiteral("eqStyle"), QStringLiteral("bars")).toString();
    return QStringList{"bars", "thin", "dots", "wave", "pulse", "off"}.contains(style) ? style : QStringLiteral("bars");
}

void Activities::setEqStyle(const QString &style)
{
    store(QStringLiteral("eqStyle"), style);
}

QString Activities::capsuleMode() const
{
#ifdef Q_OS_WIN
    const QString fallback = QStringLiteral("capsule"); // the island is on by default on Windows; the toggle in Settings turns it off
#else
    const QString fallback = QStringLiteral("hidden");
#endif
    const QString mode = m_settings.value(QStringLiteral("capsuleMode"), fallback).toString();
    return mode == QLatin1String("capsule") || mode == QLatin1String("open") ? mode : QStringLiteral("hidden");
}

void Activities::setCapsuleMode(const QString &mode)
{
    store(QStringLiteral("capsuleMode"), mode == QLatin1String("capsule") || mode == QLatin1String("open") ? mode : QStringLiteral("hidden"));
}

void Activities::toggleMicMute()
{
#ifdef Q_OS_WIN
    WinPrivacy::toggleMicMute();
    pollPrivacy();
#else
    QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-source-mute"), QStringLiteral("@DEFAULT_SOURCE@"),
                                                      QStringLiteral("toggle")});
    QTimer::singleShot(300, this, &Activities::pollPrivacy);
#endif
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
#ifdef Q_OS_WIN
    // Both from what Windows records for its own taskbar icon (WinPrivacy), so no audio API is polled for apps.
    const QStringList camera = privacyIndicators() ? WinPrivacy::appsUsing(L"webcam") : QStringList();
    const QStringList mic = WinPrivacy::appsUsing(L"microphone");
    const int muted = WinPrivacy::micMuted();
    if (camera != m_camera || mic != m_mic || muted != m_micMuted) {
        m_camera = camera;
        m_mic = mic;
        m_micMuted = muted;
        emit privacyChanged();
    }
#endif
#ifdef Q_OS_LINUX
    // Camera: a process holding /dev/video* open. Only looked for when there is a camera device.
    QStringList camera;
    if (privacyIndicators() && !QDir(QStringLiteral("/dev")).entryList({QStringLiteral("video*")}, QDir::System).isEmpty()) {
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
                                             QStringLiteral("pactl -f json list sources && echo && pactl -f json list source-outputs"
                                                            " && echo && pactl get-source-mute @DEFAULT_SOURCE@")});
    }
#endif
}

void Activities::readMicOutputs()
{
    // Lines: the sources (JSON), the recording streams (JSON), then "Mute: yes|no".
    QList<QByteArray> docs;
    int muted = -1;
    for (const QByteArray &line : m_pactlOut.split('\n')) {
        const QByteArray l = line.trimmed();
        if (l.startsWith('['))
            docs << l;
        else if (l.startsWith("Mute:"))
            muted = l.endsWith("yes") ? 1 : 0;
    }
    if (muted != m_micMuted) {
        m_micMuted = muted;
        emit privacyChanged();
    }
    if (docs.size() < 2)
        return; // no pactl, or one without JSON output
    QHash<int, bool> monitor;
    for (const QJsonValue &v : QJsonDocument::fromJson(docs.first()).array()) {
        const QJsonObject source = v.toObject();
        const QString name = source.value(QLatin1String("name")).toString();
        monitor.insert(source.value(QLatin1String("index")).toInt(), name.endsWith(QLatin1String(".monitor")));
    }
    QStringList mic;
    bool discordOpen = false; // a Discord recording stream that is not paused: Discord only holds one in a call
    for (const QJsonValue &v : QJsonDocument::fromJson(docs.last()).array()) {
        const QJsonObject stream = v.toObject();
        if (monitor.value(stream.value(QLatin1String("source")).toInt(), false))
            continue;
        const QJsonObject props = stream.value(QLatin1String("properties")).toObject();
        QString name = props.value(QLatin1String("application.name")).toString();
        if (name == QLatin1String("OlyzanoKaraoke") || name == QLatin1String("OlyzanoMicLevel"))
            continue; // our own karaoke loop and level reader are not an app listening
        if (name.isEmpty())
            name = props.value(QLatin1String("application.process.binary")).toString();
        if (!name.isEmpty() && !mic.contains(name))
            mic << name;
        if (!stream.value(QLatin1String("corked")).toBool()
            && (name.contains(QLatin1String("discord"), Qt::CaseInsensitive)
                || props.value(QLatin1String("application.process.binary")).toString().contains(QLatin1String("discord"), Qt::CaseInsensitive)))
            discordOpen = true;
    }
    // No loudness is read here (that needs a recording stream of our own), so a call shows as "in call", not "talking".
    const CallState before = m_call.state;
    m_call.update(discordOpen, m_speaking ? 1 : 0);
    if (m_call.state != before)
        emit discordChanged();
    if (mic != m_mic) {
        m_mic = mic;
        emit privacyChanged();
    }
    syncMicTap();
}

// Linux: read the microphone only while an app already has it open and it is not muted, so a voice can light the icon.
void Activities::syncMicTap()
{
    const bool want = privacyIndicators() && !m_mic.isEmpty() && m_micMuted != 1;
    if (want && m_micTap.state() == QProcess::NotRunning) {
        m_micTap.start(QStringLiteral("parec"), {QStringLiteral("--device=@DEFAULT_SOURCE@"), QStringLiteral("--raw"),
                                                 QStringLiteral("--format=s16le"), QStringLiteral("--rate=8000"),
                                                 QStringLiteral("--channels=1"), QStringLiteral("--latency-msec=60"),
                                                 QStringLiteral("--client-name=OlyzanoMicLevel")});
    } else if (!want && m_micTap.state() != QProcess::NotRunning) {
        m_micTap.kill();
        if (m_speaking) {
            m_speaking = false;
            emit micSpeakingChanged();
        }
    }
}

void Activities::onMicTap()
{
    const QByteArray data = m_micTap.readAllStandardOutput();
    const auto *samples = reinterpret_cast<const qint16 *>(data.constData());
    int peak = 0;
    for (int i = 0; i < data.size() / 2; ++i)
        peak = qMax(peak, qAbs(int(samples[i])));
    if (peak > 1300) // about -28 dBFS: a voice, not the room
        m_voiceSeen.start();
    const bool speaking = m_voiceSeen.isValid() && m_voiceSeen.elapsed() < 400; // holds between words
    if (speaking != m_speaking) {
        m_speaking = speaking;
        emit micSpeakingChanged();
    }
    if (m_call.state != CallState::Off) { // a Discord call: "talking" follows the same voice
        const CallState before = m_call.state;
        m_call.update(true, speaking ? 1 : 0);
        if (m_call.state != before)
            emit discordChanged();
    }
}
