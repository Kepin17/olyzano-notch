#pragma once

#include "platform/ChildLife.h"

#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

#include <atomic>

// Runs one of the programs of the sherpa-onnx helper (or ffmpeg) to the end, with its libraries found, dead when Notch is, and stopped when the
// job is cancelled or takes too long. Blocking.
namespace Audio {
namespace SherpaRun {

struct Result {
    bool started = false, cancelled = false, timedOut = false;
    int code = -1;
    QString output; // stdout and stderr together
    bool ok() const { return started && !cancelled && !timedOut && code == 0; }
};

// `folder`: where the program is (its libraries are in ../lib)
inline QProcessEnvironment environment(const QString &folder)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString libs = QDir(folder).absoluteFilePath(QStringLiteral("../lib"));
#ifdef Q_OS_WIN
    env.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(folder) + u';' + QDir::toNativeSeparators(libs) + u';' + env.value(QStringLiteral("PATH")));
#else
    env.insert(QStringLiteral("LD_LIBRARY_PATH"), folder + u':' + libs + u':' + env.value(QStringLiteral("LD_LIBRARY_PATH")));
#endif
    return env;
}

inline QString program(const QString &folder, const QString &name)
{
#ifdef Q_OS_WIN
    return QDir(folder).filePath(name + QStringLiteral(".exe"));
#else
    return QDir(folder).filePath(name);
#endif
}

inline Result run(const QString &exe, const QStringList &args, const QString &folder, const std::atomic_bool *cancel, qint64 timeoutMs)
{
    Result r;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    if (!folder.isEmpty()) {
        p.setProcessEnvironment(environment(folder));
        p.setWorkingDirectory(folder);
    }
    ChildLife::dieWithParent(p);
    p.start(exe, args);
    if (!p.waitForStarted(10000))
        return r;
    r.started = true;
    QElapsedTimer clock;
    clock.start();
    while (p.state() != QProcess::NotRunning) {
        if (cancel && cancel->load()) {
            r.cancelled = true;
        } else if (clock.elapsed() > timeoutMs) {
            r.timedOut = true;
        }
        if (r.cancelled || r.timedOut) {
            p.kill();
            p.waitForFinished(3000);
            break;
        }
        if (p.waitForReadyRead(200))
            r.output += QString::fromUtf8(p.readAll());
    }
    r.output += QString::fromUtf8(p.readAll());
    r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return r;
}

inline QString tail(QString text)
{
    const QStringList lines = text.trimmed().split(u'\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.mid(qMax(0, int(lines.size()) - 3)).join(QStringLiteral(" | ")).left(300);
}

// the sentence an online-punctuation program printed: "Output text: ..." when it labels it, else its last line that is not a log line
inline QString parsePunctuated(const QString &output)
{
    QString last;
    for (const QString &raw : output.split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const int label = line.indexOf(QStringLiteral("Output text:"), 0, Qt::CaseInsensitive);
        if (label >= 0)
            return line.mid(label + 12).trimmed();
        if (line.contains(QStringLiteral(".cc:")) || line.startsWith(QStringLiteral("Input text"), Qt::CaseInsensitive) || line.startsWith(QStringLiteral("OnlinePunctuation"))
            || line.startsWith(QStringLiteral("/")) || line.startsWith(u'['))
            continue;
        last = line;
    }
    return last;
}

} // namespace SherpaRun
} // namespace Audio
