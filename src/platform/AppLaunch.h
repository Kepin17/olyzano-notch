#pragma once

#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

// Starting another program from Notch. On Linux the program gets a systemd scope of its own (the way KDE starts applications):
// started the plain way it would live in Notch's own unit, and then System Monitor counts all of it as Notch's memory (a
// browser opened from the dock showed up as "Olyzano Notch, 2.6 GiB"), and a Notch that quits or crashes would leave the unit
// running for as long as the program does. Without systemd, or on Windows, it is the plain start.
namespace AppLaunch {

// The unit name for a program: app-<name>-<number>.scope, with the name made safe for a unit name.
inline QString unitName(const QString &program)
{
    QString base = QFileInfo(program).fileName().toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]")), QStringLiteral("-"));
    base = base.left(40);
    if (base.isEmpty())
        base = QStringLiteral("app");
    return QStringLiteral("app-%1-%2.scope").arg(base).arg(QRandomGenerator::global()->bounded(100000, 999999));
}

// The arguments of systemd-run for starting `program args` in its own scope. "--" ends the options, so an argument that begins
// with "-" (or a program whose name does) stays what it was.
inline QStringList scopeArgs(const QString &unit, const QString &program, const QStringList &args)
{
    return QStringList{QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"), QStringLiteral("--collect"),
                       QStringLiteral("--slice=app.slice"), QStringLiteral("--unit=") + unit, QStringLiteral("--description=") + unit,
                       QStringLiteral("--")} << program << args;
}

// Whether scopes work on this computer, learned once. Until it is known (or when it is "no") the plain start is used.
enum class Scope { Unknown, Yes, No };
inline Scope &scopeState()
{
    static Scope state = qEnvironmentVariableIsSet("NOTCH_NO_SCOPE") ? Scope::No : Scope::Unknown;
    return state;
}

// The probe: run `true` in a scope. Blocking (tests) or in the background (the app, once, at start).
inline bool probeBlocking()
{
#ifdef Q_OS_LINUX
    const QString runner = QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
    if (runner.isEmpty() || scopeState() == Scope::No) {
        scopeState() = Scope::No;
        return false;
    }
    const bool ok = QProcess::execute(runner, scopeArgs(unitName(QStringLiteral("probe")), QStringLiteral("true"), {})) == 0;
    scopeState() = ok ? Scope::Yes : Scope::No;
    return ok;
#else
    scopeState() = Scope::No;
    return false;
#endif
}

inline void probe(QObject *parent)
{
#ifdef Q_OS_LINUX
    const QString runner = QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
    if (runner.isEmpty() || scopeState() != Scope::Unknown) {
        if (scopeState() == Scope::Unknown)
            scopeState() = Scope::No;
        return;
    }
    auto *p = new QProcess(parent);
    QObject::connect(p, &QProcess::finished, p, [p](int code, QProcess::ExitStatus status) {
        scopeState() = code == 0 && status == QProcess::NormalExit ? Scope::Yes : Scope::No;
        p->deleteLater();
    });
    QObject::connect(p, &QProcess::errorOccurred, p, [p](QProcess::ProcessError) { scopeState() = Scope::No; p->deleteLater(); });
    p->start(runner, scopeArgs(unitName(QStringLiteral("probe")), QStringLiteral("true"), {}));
#else
    Q_UNUSED(parent)
    scopeState() = Scope::No;
#endif
}

inline bool start(const QString &program, const QStringList &args, const QString &workDir)
{
#ifdef Q_OS_LINUX
    if (scopeState() == Scope::Yes)
        return QProcess::startDetached(QStandardPaths::findExecutable(QStringLiteral("systemd-run")), scopeArgs(unitName(program), program, args), workDir);
#endif
    return QProcess::startDetached(program, args, workDir);
}

} // namespace AppLaunch
