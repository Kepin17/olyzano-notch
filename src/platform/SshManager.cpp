#include "SshManager.h"

#include "platform/NotchPaths.h"
#include "platform/SecureFiles.h"
#include "platform/SecretsStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <functional>

namespace {

// secrets.json section key for this manager
constexpr auto kSecretsSection = "ssh";

} // namespace

SshManager::SshManager(QObject *parent)
    : QObject(parent)
{
    load();
}

SshManager::~SshManager()
{
    removeAskPass(m_shellAskPass);
}

QString SshManager::configPath() const
{
    return SecureFiles::ownerOnlyPath(NotchPaths::devDir(), QStringLiteral("ssh-connections.json"));
}

QString SshManager::secretsPath() const
{
    return SecretsStore::secretsPath();
}

void SshManager::load()
{
    QFile f(configPath());
    if (f.open(QIODevice::ReadOnly))
        m_list = SshModel::listFromJson(f.readAll());
}

void SshManager::save() const
{
    QSaveFile f(configPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(SshModel::listToJson(m_list));
        f.commit();
    }
    QFile::setPermissions(configPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QString SshManager::expandTilde(const QString &path)
{
    if (path.startsWith(QLatin1Char('~')))
        return QDir::homePath() + path.mid(1);
    return path;
}

QString SshManager::findSsh()
{
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("ssh"));
    if (!onPath.isEmpty())
        return onPath;
#ifdef Q_OS_WIN
    // GUI apps often miss System32 when OpenSSH is the optional feature (not always on the
    // inherited PATH). Prefer the well-known install when `ssh` is not resolvable.
    const QString root = QProcessEnvironment::systemEnvironment().value(QStringLiteral("SystemRoot"),
                                                                         QStringLiteral("C:\\Windows"));
    const QString wellKnown = root + QStringLiteral("\\System32\\OpenSSH\\ssh.exe");
    if (QFileInfo::exists(wellKnown))
        return wellKnown;
#endif
    return {};
}

QProcessEnvironment SshManager::sshEnvironment(const SshModel::Connection &c, QString *askPassDir)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (askPassDir)
        askPassDir->clear();
    if (c.auth != QLatin1String("password") || !c.savePassword)
        return env;
    const QString pw = passwordFor(c.id);
    if (pw.isEmpty())
        return env;

    // Unique per process so an overlapping Test / Run / shell does not delete a live helper.
    const QString dir = QDir::tempPath() + QStringLiteral("/notch-ssh-askpass-")
                        + QString::number(QCoreApplication::applicationPid()) + QLatin1Char('-')
                        + QString::number(QDateTime::currentMSecsSinceEpoch());
    if (!QDir().mkpath(dir)) {
        emit statusMessage(QStringLiteral("SSH — cannot create askpass helper dir"));
        return env;
    }
    // Helper prints the password with no trailing newline. On Windows the password lives in a
    // sibling file so the .bat does not have to escape special characters via `echo`.
    const QString pwFile = dir + QStringLiteral("/pw");
    {
        QFile f(pwFile);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit statusMessage(QStringLiteral("SSH — cannot write askpass helper"));
            removeAskPass(dir);
            return env;
        }
        f.write(pw.toUtf8());
        f.close();
        QFile::setPermissions(pwFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
#ifdef Q_OS_WIN
    const QString helper = dir + QStringLiteral("/askpass.bat");
    QFile f(helper);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        removeAskPass(dir);
        return env;
    }
    // `type` copies the file bytes as-is; avoid `echo` (caret/percent mangling).
    f.write("@echo off\r\ntype \"");
    f.write(QDir::toNativeSeparators(pwFile).toUtf8());
    f.write("\"\r\n");
    f.close();
    QFile::setPermissions(helper, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    env.insert(QStringLiteral("SSH_ASKPASS"), helper);
    // OpenSSH 8.6+ on Windows: use askpass even when a console exists (QProcess has none).
    env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
#else
    const QString helper = dir + QStringLiteral("/askpass");
    QFile f(helper);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        removeAskPass(dir);
        return env;
    }
    f.write("#!/bin/sh\nexec cat '");
    f.write(pwFile.toUtf8());
    f.write("'\n");
    f.close();
    QFile::setPermissions(helper, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    env.insert(QStringLiteral("SSH_ASKPASS"), helper);
    env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
    // Without a display some ssh builds skip askpass entirely.
    if (!env.contains(QStringLiteral("DISPLAY")))
        env.insert(QStringLiteral("DISPLAY"), QStringLiteral(":0"));
#endif
    if (askPassDir)
        *askPassDir = dir;
    else
        removeAskPass(dir); // no owner — do not leave a password file behind
    return env;
}

void SshManager::removeAskPass(const QString &dir)
{
    if (dir.isEmpty())
        return;
    QFile::remove(dir + QStringLiteral("/pw"));
    QFile::remove(dir + QStringLiteral("/askpass"));
    QFile::remove(dir + QStringLiteral("/askpass.bat"));
    QDir(dir).removeRecursively();
}

SshModel::Connection *SshManager::find(const QString &id)
{
    for (auto &c : m_list)
        if (c.id == id)
            return &c;
    return nullptr;
}

const SshModel::Connection *SshManager::find(const QString &id) const
{
    return const_cast<SshManager *>(this)->find(id);
}

QVariantList SshManager::connections() const
{
    QVariantList out;
    for (const auto &c : m_list)
        out << QVariantMap{{QStringLiteral("id"), c.id},       {QStringLiteral("name"), c.name},
                           {QStringLiteral("host"), c.host},   {QStringLiteral("user"), c.user},
                           {QStringLiteral("port"), c.port},   {QStringLiteral("auth"), c.auth},
                           {QStringLiteral("keyPath"), c.keyPath},
                           {QStringLiteral("defaultCommand"), c.defaultCommand},
                           {QStringLiteral("savePassword"), c.savePassword},
                           {QStringLiteral("label"),
                            QStringLiteral("%1@%2:%3").arg(c.user, c.host).arg(c.port)}};
    return out;
}

QVariantMap SshManager::connectionById(const QString &id) const
{
    const auto *c = find(id);
    if (!c)
        return {};
    return QVariantMap{{QStringLiteral("id"), c->id},       {QStringLiteral("name"), c->name},
                       {QStringLiteral("host"), c->host},   {QStringLiteral("user"), c->user},
                       {QStringLiteral("port"), c->port},   {QStringLiteral("auth"), c->auth},
                       {QStringLiteral("keyPath"), c->keyPath},
                       {QStringLiteral("defaultCommand"), c->defaultCommand},
                       {QStringLiteral("savePassword"), c->savePassword}};
}

QString SshManager::addConnection(const QVariantMap &fields)
{
    SshModel::Connection c;
    c.id = SshModel::newId();
    c.name = fields.value(QStringLiteral("name")).toString().trimmed();
    SshModel::applyFields(c, QJsonObject::fromVariantMap(fields));
    if (c.name.isEmpty())
        c.name = c.host.isEmpty() ? QStringLiteral("New connection") : c.host;
    if (!SshModel::validHost(c.host) || !SshModel::validUser(c.user)) {
        emit statusMessage(QStringLiteral("SSH — host or user is not a valid name"));
        return {};
    }
    if (!SshModel::validPort(c.port))
        c.port = 22;
    m_list << c;
    save();
    emit changed();
    return c.id;
}

bool SshManager::updateConnection(const QString &id, const QVariantMap &fields)
{
    auto *c = find(id);
    if (!c)
        return false;
    const QString prevHost = c->host;
    const QString prevUser = c->user;
    SshModel::applyFields(*c, QJsonObject::fromVariantMap(fields));
    if (!SshModel::validHost(c->host) || !SshModel::validUser(c->user)) {
        c->host = prevHost;
        c->user = prevUser;
        emit statusMessage(QStringLiteral("SSH — host or user is not a valid name"));
        return false;
    }
    if (!SshModel::validPort(c->port))
        c->port = 22;
    if (c->name.trimmed().isEmpty())
        c->name = c->host;
    save();
    emit changed();
    return true;
}

bool SshManager::removeConnection(const QString &id)
{
    for (int i = 0; i < m_list.size(); ++i)
        if (m_list[i].id == id) {
            dropPassword(id);
            m_list.removeAt(i);
            save();
            emit changed();
            return true;
        }
    return false;
}

QString SshManager::duplicateConnection(const QString &id)
{
    for (int i = 0; i < m_list.size(); ++i)
        if (m_list[i].id == id) {
            SshModel::Connection c = m_list[i];
            c.id = SshModel::newId();
            c.name += QStringLiteral(" copy");
            m_list.insert(i + 1, c);
            save();
            emit changed();
            return c.id;
        }
    return {};
}

// secrets.json: { "ssh": { "<id>": "password" } } — opt-in, owner-only file, never on argv.
QString SshManager::passwordFor(const QString &id) const
{
    return SecretsStore::get(QString::fromLatin1(kSecretsSection), id);
}

void SshManager::storePassword(const QString &id, const QString &password)
{
    SecretsStore::set(QString::fromLatin1(kSecretsSection), id, password);
}

void SshManager::dropPassword(const QString &id)
{
    storePassword(id, {});
}

SshManager::SshProcessSpec SshManager::buildSsh(const SshModel::Connection &c, const QString &command, bool batchMode) const
{
    SshProcessSpec s;
    // Absolute path when known: QProcess then fails with a precise "not found" on Windows
    // instead of silently depending on a PATH the GUI process may not inherit.
    const QString bin = findSsh();
    s.program = bin.isEmpty() ? QStringLiteral("ssh") : bin;
    // interactive shell: force a remote TTY so the session behaves like a normal ssh login
    if (command.trimmed().isEmpty())
        s.args << QStringLiteral("-tt");
    s.args << QStringLiteral("-p") << QString::number(c.port);
    s.args << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=accept-new");
    // BatchMode only for non-interactive (test / one-shot): a login shell may need prompts
    if (batchMode)
        s.args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes");
    if (c.auth == QLatin1String("key") && !c.keyPath.isEmpty())
        s.args << QStringLiteral("-i") << expandTilde(c.keyPath);
    s.args << QStringLiteral("%1@%2").arg(c.user, c.host);
    if (!command.trimmed().isEmpty())
        s.args << command;
    return s;
}

bool SshManager::useAskPassFor(const SshModel::Connection &c) const
{
    return c.auth == QLatin1String("password") && c.savePassword && !passwordFor(c.id).isEmpty();
}

QString SshManager::prepareSshProcess(QProcess *proc, const SshModel::Connection &c, const QString &command, bool batchMode)
{
    const auto spec = buildSsh(c, command, batchMode);
    QString askDir;
    proc->setProcessEnvironment(sshEnvironment(c, &askDir));
    proc->start(spec.program, spec.args);
    return askDir;
}

void SshManager::testConnection(const QString &id)
{
    if (m_test)
        return;
    const auto *cp = find(id);
    if (!cp)
        return;
    const SshModel::Connection c = *cp;
    if (!SshModel::validHost(c.host) || !SshModel::validUser(c.user)) {
        finishTest(id, false, QStringLiteral("host or user is not a valid name"), 0);
        return;
    }
    m_busy = true;
    emit busyChanged();

    m_test = new QProcess(this);
    // BatchMode: never prompts; fails fast when the key/agent cannot authenticate.
    // Password + saved secret uses askpass instead (no real TTY under QProcess), so skip BatchMode.
    const bool ask = useAskPassFor(c);
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    const QString askDir = prepareSshProcess(m_test, c, QStringLiteral("true"), !ask);
    connect(m_test, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, id, startedAt, askDir](int code, QProcess::ExitStatus st) {
        const QString err = QString::fromLocal8Bit(m_test ? m_test->readAllStandardError() : QByteArray()).trimmed();
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (m_test) {
            m_test->deleteLater();
            m_test = nullptr;
        }
        removeAskPass(askDir);
        m_busy = false;
        emit busyChanged();
        finishTest(id, ok, ok ? QString() : (err.isEmpty() ? QStringLiteral("connection failed") : err),
                   QDateTime::currentMSecsSinceEpoch() - startedAt);
    });
    connect(m_test, &QProcess::errorOccurred, this, [this, id, startedAt, askDir](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        const QString msg = QStringLiteral("ssh binary not found (install OpenSSH on Windows: Settings → Optional features)");
        removeAskPass(askDir);
        if (m_test) {
            m_test->deleteLater();
            m_test = nullptr;
        }
        m_busy = false;
        emit busyChanged();
        finishTest(id, false, msg, QDateTime::currentMSecsSinceEpoch() - startedAt);
    });
}

void SshManager::finishTest(const QString &id, bool ok, const QString &error, qint64 ms)
{
    emit testFinished(
        QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("ok"), ok}, {QStringLiteral("error"), error}, {QStringLiteral("latencyMs"), ms}});
    emit statusMessage(ok ? QStringLiteral("SSH — reachable") : QStringLiteral("SSH — ") + error);
}

void SshManager::runCommand(const QString &id, const QString &command)
{
    if (m_cmd || command.trimmed().isEmpty())
        return;
    const auto *cp = find(id);
    if (!cp)
        return;
    const SshModel::Connection c = *cp;
    if (!SshModel::validHost(c.host) || !SshModel::validUser(c.user)) {
        emit statusMessage(QStringLiteral("SSH — host or user is not a valid name"));
        return;
    }
    m_lastOutput.clear();
    emit outputChanged();
    m_cmd = new QProcess(this);
    const bool ask = useAskPassFor(c);
    // Password without a saved secret still needs an interactive prompt (open a terminal for that).
    const QString askDir = prepareSshProcess(m_cmd, c, command, c.auth != QLatin1String("password") || !ask);
    connect(m_cmd, &QProcess::readyReadStandardOutput, this, [this] {
        m_lastOutput += QString::fromLocal8Bit(m_cmd->readAllStandardOutput());
        emit outputChanged();
    });
    connect(m_cmd, &QProcess::readyReadStandardError, this, [this] {
        m_lastOutput += QString::fromLocal8Bit(m_cmd->readAllStandardError());
        emit outputChanged();
    });
    connect(m_cmd, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, command, askDir](int code, QProcess::ExitStatus st) {
        m_lastOutput += QString::fromLocal8Bit(m_cmd->readAllStandardOutput()) + QString::fromLocal8Bit(m_cmd->readAllStandardError());
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (m_cmd) {
            m_cmd->deleteLater();
            m_cmd = nullptr;
        }
        removeAskPass(askDir);
        emit commandRunningChanged();
        emit outputChanged();
        emit commandFinished(QVariantMap{{QStringLiteral("ok"), ok},
                                         {QStringLiteral("exitCode"), code},
                                         {QStringLiteral("output"), m_lastOutput},
                                         {QStringLiteral("command"), command}});
        emit statusMessage(ok ? QStringLiteral("SSH — command finished") : QStringLiteral("SSH — command failed (") + QString::number(code) + QStringLiteral(")"));
    });
    connect(m_cmd, &QProcess::errorOccurred, this, [this, command, askDir](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_lastOutput = QStringLiteral("Failed to start ssh");
        if (m_cmd) {
            m_cmd->deleteLater();
            m_cmd = nullptr;
        }
        removeAskPass(askDir);
        emit commandRunningChanged();
        emit outputChanged();
        emit commandFinished(QVariantMap{{QStringLiteral("ok"), false},
                                         {QStringLiteral("exitCode"), -1},
                                         {QStringLiteral("output"), m_lastOutput},
                                         {QStringLiteral("command"), command}});
        emit statusMessage(QStringLiteral("SSH — ssh binary not found (install OpenSSH on Windows: Settings → Optional features)"));
    });
    emit commandRunningChanged();
}

void SshManager::cancelCommand()
{
    if (m_cmd)
        m_cmd->kill();
}

// ---- persistent interactive shell (ssh -tt) ----

void SshManager::teardownShell(const QString &note)
{
    if (!m_shell)
        return;
    m_shell->disconnect(this);
    if (m_shell->state() != QProcess::NotRunning) {
        m_shell->write("exit\n");
        if (!m_shell->waitForFinished(800))
            m_shell->kill();
        m_shell->waitForFinished(400);
    }
    m_shell->deleteLater();
    m_shell = nullptr;
    removeAskPass(m_shellAskPass);
    m_shellAskPass.clear();
    m_shellHost.clear();
    emit shellStateChanged();
    if (!note.isEmpty())
        emit statusMessage(note);
}

void SshManager::openShell(const QString &id)
{
    teardownShell({});
    const auto *cp = find(id);
    if (!cp) {
        emit statusMessage(QStringLiteral("SSH — unknown host"));
        return;
    }
    const SshModel::Connection c = *cp;
    if (!SshModel::validHost(c.host) || !SshModel::validUser(c.user)) {
        emit statusMessage(QStringLiteral("SSH — host or user is not a valid name"));
        return;
    }
    m_lastOutput.clear();
    emit outputChanged();

    m_shell = new QProcess(this);
    m_shell->setProcessChannelMode(QProcess::MergedChannels);
    // -tt + no BatchMode: interactive login shell; key path still passed when set
    m_shellAskPass = prepareSshProcess(m_shell, c, {}, false);
    connect(m_shell, &QProcess::readyReadStandardOutput, this, [this] {
        const QString chunk = QString::fromLocal8Bit(m_shell->readAllStandardOutput());
        m_lastOutput += chunk;
        emit outputChanged();
        emit shellOutput(chunk);
    });
    connect(m_shell, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, c](int, QProcess::ExitStatus) {
        m_lastOutput += QStringLiteral("\n— shell closed —\n");
        if (m_shell) {
            m_shell->deleteLater();
            m_shell = nullptr;
        }
        removeAskPass(m_shellAskPass);
        m_shellAskPass.clear();
        m_shellHost.clear();
        emit shellStateChanged();
        emit outputChanged();
        emit statusMessage(QStringLiteral("SSH — shell closed for %1@%2").arg(c.user, c.host));
    });
    connect(m_shell, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_lastOutput += QStringLiteral("Failed to start ssh\n");
        if (m_shell) {
            m_shell->deleteLater();
            m_shell = nullptr;
        }
        removeAskPass(m_shellAskPass);
        m_shellAskPass.clear();
        m_shellHost.clear();
        emit shellStateChanged();
        emit outputChanged();
        emit statusMessage(QStringLiteral("SSH — ssh binary not found (install OpenSSH on Windows: Settings → Optional features)"));
    });
    m_shellHost = QStringLiteral("%1@%2:%3").arg(c.user, c.host).arg(c.port);
    emit shellStateChanged();
    emit statusMessage(QStringLiteral("SSH — connecting shell to %1").arg(m_shellHost));
}

void SshManager::closeShell()
{
    teardownShell(QStringLiteral("SSH — shell closed"));
}

void SshManager::writeShell(const QString &line)
{
    if (!m_shell || m_shell->state() != QProcess::Running)
        return;
    QString out = line;
    if (!out.endsWith(QLatin1Char('\n')))
        out += QLatin1Char('\n');
    m_shell->write(out.toUtf8());
}

// Open a system terminal already running `ssh user@host`. argv-only where the terminal allows it;
// Windows `start` is the one place a joined string is unavoidable (host/user already validated).
void SshManager::openTerminal(const QString &id)
{
    const auto *cp = find(id);
    if (!cp)
        return;
    const SshModel::Connection c = *cp;
    if (!SshModel::validHost(c.host) || !SshModel::validUser(c.user)) {
        emit statusMessage(QStringLiteral("SSH — host or user is not a valid name"));
        return;
    }
    const auto ssh = buildSsh(c, {}, c.auth != QLatin1String("password"));

    struct Term {
        QString program;
        QStringList args;
        bool ok = false;
    };
    const QStringList candidates{QStringLiteral("xdg-terminal-exec"), QStringLiteral("gnome-terminal"), QStringLiteral("konsole"),
                                 QStringLiteral("kitty"), QStringLiteral("alacritty"), QStringLiteral("foot")};
    Term term;
#ifdef Q_OS_WIN
    // wt.exe preferred; fall back to the classic console host. Windows Terminal takes the
    // remote command as trailing argv — omit the bare "ssh" token and the window dies instantly.
    if (QStandardPaths::findExecutable(QStringLiteral("wt.exe")).isEmpty()) {
        term.program = QStringLiteral("cmd");
        term.args = {QStringLiteral("/c"), QStringLiteral("start"), QStringLiteral("\"\""), ssh.program};
        term.args << ssh.args;
        term.ok = true;
    } else {
        term.program = QStringLiteral("wt.exe");
        term.args = QStringList{QStringLiteral("ssh")} << ssh.args;
        // Prefer the resolved binary when we have one (System32 OpenSSH); keep the bare name otherwise.
        if (ssh.program != QLatin1String("ssh"))
            term.args[0] = ssh.program;
        term.ok = true;
    }
    if (findSsh().isEmpty()) {
        emit statusMessage(QStringLiteral("SSH — OpenSSH client not found (Settings → Apps → Optional features → OpenSSH Client)"));
        return;
    }
#else
    for (const QString &name : candidates) {
        const QString path = QStandardPaths::findExecutable(name);
        if (path.isEmpty())
            continue;
        term.program = path;
        if (name == QLatin1String("gnome-terminal")) {
            term.args = QStringList{QStringLiteral("--")} << QStringLiteral("ssh") << ssh.args;
        } else if (name == QLatin1String("konsole")) {
            term.args = QStringList{QStringLiteral("-e")} << QStringLiteral("ssh") << ssh.args;
        } else {
            // xdg-terminal-exec / kitty / alacritty / foot: the command is the trailing argv.
            term.args = QStringList{QStringLiteral("ssh")} << ssh.args;
        }
        term.ok = true;
        break;
    }
#endif
    if (!term.ok) {
#ifdef Q_OS_WIN
        emit statusMessage(QStringLiteral("SSH — no terminal found (install Windows Terminal)"));
#else
        emit statusMessage(QStringLiteral("SSH — no terminal found (try gnome-terminal or konsole)"));
#endif
        return;
    }
    QProcess::startDetached(term.program, term.args);
    emit statusMessage(QStringLiteral("SSH — opened terminal for %1@%2").arg(c.user, c.host));
}
