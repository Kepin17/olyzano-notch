#include "platform/NotchPaths.h"
#include "MemoryTuner.h"

#include <QDateTime>
#include <QHash>
#include <QPair>
#include <algorithm>
#include <cctype>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <cstdio>
#include <QPixmapCache>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QProcess>
#include <QThreadPool>

#ifdef Q_OS_LINUX
#include <malloc.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
constexpr qint64 WatchOverMb = 450;      // an idle Notch above this (private memory) is written up and trimmed
constexpr qint64 RestartIdleMs = 10 * 60 * 1000;
constexpr qint64 LogLimit = 200 * 1024;

struct Usage { qint64 rss = 0, priv = 0, file = 0; }; // MB. priv: anonymous memory (Linux) / private bytes (Windows)
Usage usage()
{
    Usage u;
#ifdef Q_OS_LINUX
    QFile f(QStringLiteral("/proc/self/status"));
    if (f.open(QIODevice::ReadOnly))
        for (const QByteArray &line : f.readAll().split('\n')) {
            const QList<QByteArray> p = line.simplified().split(' ');
            if (p.size() < 2) continue;
            if (p[0] == "VmRSS:") u.rss = p[1].toLongLong() / 1024;
            else if (p[0] == "RssAnon:") u.priv = p[1].toLongLong() / 1024;
            else if (p[0] == "RssFile:") u.file = p[1].toLongLong() / 1024;
        }
#elif defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX c = {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&c), sizeof(c))) {
        u.rss = qint64(c.WorkingSetSize) / (1024 * 1024);
        u.priv = qint64(c.PrivateUsage) / (1024 * 1024);
    }
#endif
    return u;
}

// The biggest mappings by resident size: what kind of memory it is (heap, anonymous, a library) tells where to look.
int threadCount()
{
#ifdef Q_OS_LINUX
    return QDir(QStringLiteral("/proc/self/task")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
#else
    return 0;
#endif
}

QString topMappings()
{
    QString out;
#ifdef Q_OS_LINUX
    QFile f(QStringLiteral("/proc/self/smaps"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    QHash<QString, qint64> kb;
    QList<QPair<qint64, QString>> regions; // the biggest single mappings: one huge anonymous block looks different from a heap of small ones
    QString name, range;
    for (const QByteArray &line : f.readAll().split('\n')) {
        if (!line.isEmpty() && (std::isxdigit(uchar(line[0]))) && line.contains('-') && line.contains(' ')) {
            const QList<QByteArray> p = line.simplified().split(' ');
            name = p.size() > 5 ? QString::fromUtf8(p[5]) : QStringLiteral("[anon]");
            range = QString::fromUtf8(p[0]);
        } else if (line.startsWith("Rss:")) {
            const qint64 v = line.simplified().split(' ').value(1).toLongLong();
            kb[name] += v;
            if (v > 20 * 1024) regions.append({v, range + QLatin1Char(' ') + name});
        }
    }
    QList<QPair<qint64, QString>> sorted;
    for (auto it = kb.begin(); it != kb.end(); ++it) sorted.append({it.value(), it.key()});
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (int i = 0; i < qMin(8, int(sorted.size())); ++i) out += QStringLiteral("    %1 MB  %2\n").arg(sorted[i].first / 1024).arg(sorted[i].second);
    std::sort(regions.begin(), regions.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    if (!regions.isEmpty()) out += QStringLiteral("  biggest single blocks:\n");
    for (int i = 0; i < qMin(5, int(regions.size())); ++i) out += QStringLiteral("    %1 MB  %2\n").arg(regions[i].first / 1024).arg(regions[i].second);
#endif
    return out;
}

void writeLog(const QString &text)
{
    const QString path = NotchPaths::dataDir() + QStringLiteral("/memory.log");
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFileInfo(path).size() > LogLimit) { QFile::remove(path + QStringLiteral(".1")); QFile::rename(path, path + QStringLiteral(".1")); }
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(text.toUtf8());
}

QString choiceFile() { return NotchPaths::conf(QStringLiteral("perf")); }
}

MemoryTuner::MemoryTuner(QObject *parent) : QObject(parent)
{
    QThreadPool::globalInstance()->setMaxThreadCount(4); // QtConcurrent jobs (copying, encrypting, speech): a few at a time is plenty
    m_watch.setInterval(60000);
    connect(&m_watch, &QTimer::timeout, this, &MemoryTuner::check);
    m_watch.start();
    if (qEnvironmentVariableIntValue("NOTCH_MEMLOG") > 0) {
        m_log.setInterval(30000);
        connect(&m_log, &QTimer::timeout, this, [this] { report("tick"); });
        m_log.start();
        report("start");
    }
}

bool MemoryTuner::freeWhenIdle() const { return QSettings(choiceFile(), QSettings::IniFormat).value(QStringLiteral("freeWhenIdle"), true).toBool(); }

void MemoryTuner::setFreeWhenIdle(bool on)
{
    if (on == freeWhenIdle())
        return;
    QDir().mkpath(QFileInfo(choiceFile()).absolutePath());
    QSettings s(choiceFile(), QSettings::IniFormat);
    s.setValue(QStringLiteral("freeWhenIdle"), on);
    s.sync();
    emit changed();
}

void MemoryTuner::report(const char *why) const
{
    const Usage u = usage();
    fprintf(stderr, "notch-mem %s rss=%lld MB private=%lld MB file=%lld MB\n", why, qint64(u.rss), qint64(u.priv), qint64(u.file));
}

int MemoryTuner::restartOverMb() const { return QSettings(choiceFile(), QSettings::IniFormat).value(QStringLiteral("restartOverMb"), 0).toInt(); }

void MemoryTuner::setRestartOverMb(int mb)
{
    mb = qMax(0, mb);
    if (mb == restartOverMb())
        return;
    QDir().mkpath(QFileInfo(choiceFile()).absolutePath());
    QSettings s(choiceFile(), QSettings::IniFormat);
    s.setValue(QStringLiteral("restartOverMb"), mb);
    s.sync();
    emit changed();
}

void MemoryTuner::setPanelIdle(bool idle)
{
    if (idle == m_panelIdle)
        return;
    m_panelIdle = idle;
    if (idle) m_idleSince.restart();
}

// Once a minute, only while the panel is closed: a Notch that holds a lot writes down what holds it (so the cause can be read from
// memory.log, not guessed), is trimmed, and, if the person asked for it, restarted when it is still big after ten quiet minutes.
void MemoryTuner::check()
{
    Usage u = usage();
    // Growth while the panel is in use never reaches the idle report below: each time private memory first passes a step, it is written down once.
    static const qint64 steps[] = {qEnvironmentVariableIsSet("NOTCH_MEM_WATCH_MB") ? qEnvironmentVariableIntValue("NOTCH_MEM_WATCH_MB") : 450, 900, 1500, 2500, 4000}; // (the variable is for testing)
    bool passed = false;
    while (m_step < int(sizeof(steps) / sizeof(steps[0])) && u.priv >= steps[m_step]) { ++m_step; passed = true; }
    if (passed)
        writeLog(QStringLiteral("%1 %2, page \"%3\"%4: rss %5 MB, private %6 MB, file %7 MB, %8 threads\n%9\n")
                     .arg(QDateTime::currentDateTime().toString(Qt::ISODate), m_panelIdle ? QStringLiteral("idle") : QStringLiteral("in use"), m_context,
                          m_busy ? QStringLiteral(" (agent working)") : QString()).arg(u.rss).arg(u.priv).arg(u.file).arg(threadCount()).arg(topMappings()));
    if (!m_panelIdle)
        return;
    static const qint64 over = qEnvironmentVariableIsSet("NOTCH_MEM_WATCH_MB") ? qEnvironmentVariableIntValue("NOTCH_MEM_WATCH_MB") : WatchOverMb; // the variable is for testing
    if (u.priv < over)
        return;
    if (!m_reported.isValid() || m_reported.elapsed() > 10 * 60 * 1000) {
        m_reported.restart();
        const QString before = QStringLiteral("%1 idle, last page \"%2\": rss %3 MB, private %4 MB, file %5 MB\n%6")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate), m_context).arg(u.rss).arg(u.priv).arg(u.file).arg(topMappings());
        trim();
        u = usage();
        writeLog(before + QStringLiteral("  after trim: rss %1 MB, private %2 MB\n\n").arg(u.rss).arg(u.priv));
    }
    const int limit = restartOverMb();
    if (limit > 0 && u.priv > limit && !m_busy && m_idleSince.isValid() && m_idleSince.elapsed() > RestartIdleMs) {
        writeLog(QStringLiteral("%1 restarting: private %2 MB is over %3 MB after %4 min idle\n\n")
                     .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).arg(u.priv).arg(limit).arg(m_idleSince.elapsed() / 60000));
        restart();
    }
}

void MemoryTuner::applyEarly()
{
#ifdef Q_OS_LINUX
    mallopt(M_ARENA_MAX, 2);
    if (!qEnvironmentVariableIsSet("MESA_GLTHREAD"))
        qputenv("MESA_GLTHREAD", "false"); // Mesa's own helper thread: a thread and a few MB for nothing on a small window
#endif
    if (QSettings(choiceFile(), QSettings::IniFormat).value(QStringLiteral("lowMemory"), false).toBool()
        && !qEnvironmentVariableIsSet("QT_QUICK_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", "software");
#ifdef Q_OS_LINUX
        // With the software renderer the Wayland plug-in must not set up EGL either: that is what loads Mesa and LLVM (some 30 MB).
        if (!qEnvironmentVariableIsSet("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION"))
            qputenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "shm");
#endif
    }
}

bool MemoryTuner::available() const
{
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

bool MemoryTuner::lowMemory() const { return QSettings(choiceFile(), QSettings::IniFormat).value(QStringLiteral("lowMemory"), false).toBool(); }

void MemoryTuner::setLowMemory(bool on)
{
    if (on == lowMemory())
        return;
    QDir().mkpath(QFileInfo(choiceFile()).absolutePath());
    QSettings s(choiceFile(), QSettings::IniFormat);
    s.setValue(QStringLiteral("lowMemory"), on);
    s.sync();
    emit changed();
}

bool MemoryTuner::running() const { return qgetenv("QT_QUICK_BACKEND") == "software"; }

void MemoryTuner::trim()
{
    if (qEnvironmentVariableIntValue("NOTCH_MEMLOG") > 0) report("before trim");
    if (freeWhenIdle()) { // what QML still holds after a page was unloaded: its heap, the compiled components nothing uses, the pictures
        if (m_engine) { m_engine->collectGarbage(); m_engine->trimComponentCache(); }
        QPixmapCache::clear();
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *q = qobject_cast<QQuickWindow *>(w)) q->releaseResources();
    }
#ifdef Q_OS_LINUX
    malloc_trim(0);
#elif defined(Q_OS_WIN)
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); // pages the working set back out; faults in again on use
#endif
    if (qEnvironmentVariableIntValue("NOTCH_MEMLOG") > 0) report("after trim");
}

void MemoryTuner::restart()
{
    // the new copy waits a moment so this one has let go of the single-instance lock
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("cmd.exe"),
                            {QStringLiteral("/c"),
                             QStringLiteral("timeout /t 1 /nobreak >nul & start \"\" \"%1\"").arg(QCoreApplication::applicationFilePath())});
#else
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 1; exec \"$0\""), QCoreApplication::applicationFilePath()});
#endif
    QCoreApplication::quit();
}
