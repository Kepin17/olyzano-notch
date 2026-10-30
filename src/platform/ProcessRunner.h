#pragma once

#include <QProcess>
#include <QString>
#include <QStringList>

// Thin sync wrapper around QProcess for one-shot external tools (PDF, OCR, config readers,
// port probes…). Interactive shells and processes that need stdin stay on raw QProcess.
namespace ProcessRunner {

struct Result {
    bool ok = false;          // started, finished within the timeout, exit code 0
    int exitCode = -1;
    bool timedOut = false;
    bool failedToStart = false;
    QByteArray out;           // stdout
    QByteArray err;           // stderr
    QString error;            // human-readable summary when !ok (empty when ok)
};

// Run program to completion. timeoutMs covers waitForStarted + waitForFinished together.
inline Result run(const QString &program, const QStringList &args, int timeoutMs = 120000)
{
    Result r;
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(5000)) {
        r.failedToStart = true;
        r.error = QStringLiteral("%1 is not installed").arg(program);
        return r;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        r.timedOut = true;
        r.error = QStringLiteral("%1 took too long").arg(program);
        return r;
    }
    r.out = p.readAllStandardOutput();
    r.err = p.readAllStandardError();
    r.exitCode = (p.exitStatus() == QProcess::NormalExit) ? p.exitCode() : -1;
    r.ok = (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0);
    if (!r.ok) {
        const QString firstLine = QString::fromLocal8Bit(r.err).trimmed().section(u'\n', 0, 0);
        r.error = firstLine.isEmpty() ? QStringLiteral("%1 failed (exit %2)").arg(program).arg(r.exitCode) : firstLine;
    }
    return r;
}

// Convenience: stdout only, no error message. Empty string when the process fails or times out.
inline QByteArray captureOut(const QString &program, const QStringList &args, int timeoutMs = 5000)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        if (p.state() != QProcess::NotRunning)
            p.kill();
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return p.readAllStandardOutput();
}

} // namespace ProcessRunner
