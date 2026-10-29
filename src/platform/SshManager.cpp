#include "SshManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <functional>

namespace {

QString ownerOnlyPath(const QString &dir, const QString &file)
{
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + file;
    if (!QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.close();
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return path;
}

} // namespace

SshManager::SshManager(QObject *parent)
    : QObject(parent)
{
    load();
}

QString SshManager::configPath() const
{
    return ownerOnlyPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/dev"),
                         QStringLiteral("ssh-connections.json"));
}

QString SshManager::secretsPath() const
{
    return ownerOnlyPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/dev"),
                         QStringLiteral("secrets.json"));
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
    QFile f(secretsPath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    return root.value(QStringLiteral("ssh")).toObject().value(id).toString();
}

void SshManager::storePassword(const QString &id, const QString &password)
{
    QJsonObject root;
    {
        QFile f(secretsPath());
        if (f.open(QIODevice::ReadOnly))
            root = QJsonDocument::fromJson(f.readAll()).object();
    }
    QJsonObject ssh = root.value(QStringLiteral("ssh")).toObject();
    if (password.isEmpty())
        ssh.remove(id);
    else
        ssh.insert(id, password);
    root.insert(QStringLiteral("ssh"), ssh);
    QSaveFile f(secretsPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.commit();
    }
    QFile::setPermissions(secretsPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void SshManager::dropPassword(const QString &id)
{
    storePassword(id, {});
}

SshManager::SshProcessSpec SshManager::buildSsh(const SshModel::Connection &c, const QString &command, bool batchMode) const
{
    SshProcessSpec s;
    s.program = QStringLiteral("ssh");
    s.args << QStringLiteral("-p") << QString::number(c.port);
    s.args << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=accept-new");
    if (c.auth == QLatin1String("key") || batchMode)
        s.args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes");
    if (c.auth == QLatin1String("key") && !c.keyPath.isEmpty())
        s.args << QStringLiteral("-i") << expandTilde(c.keyPath);
    s.args << QStringLiteral("%1@%2").arg(c.user, c.host);
    if (!command.trimmed().isEmpty())
        s.args << command;
    return s;
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
    const auto spec = buildSsh(c, QStringLiteral("true"), true);
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    connect(m_test, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, id, startedAt](int code, QProcess::ExitStatus st) {
        const QString err = QString::fromLocal8Bit(m_test ? m_test->readAllStandardError() : QByteArray()).trimmed();
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (m_test) {
            m_test->deleteLater();
            m_test = nullptr;
        }
        m_busy = false;
        emit busyChanged();
        finishTest(id, ok, ok ? QString() : (err.isEmpty() ? QStringLiteral("connection failed") : err),
                   QDateTime::currentMSecsSinceEpoch() - startedAt);
    });
    connect(m_test, &QProcess::errorOccurred, this, [this, id, startedAt](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        const QString msg = QStringLiteral("ssh binary not found");
        if (m_test) {
            m_test->deleteLater();
            m_test = nullptr;
        }
        m_busy = false;
        emit busyChanged();
        finishTest(id, false, msg, QDateTime::currentMSecsSinceEpoch() - startedAt);
    });
    m_test->start(spec.program, spec.args);
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
    const auto spec = buildSsh(c, command, c.auth != QLatin1String("password"));
    connect(m_cmd, &QProcess::readyReadStandardOutput, this, [this] {
        m_lastOutput += QString::fromLocal8Bit(m_cmd->readAllStandardOutput());
        emit outputChanged();
    });
    connect(m_cmd, &QProcess::readyReadStandardError, this, [this] {
        m_lastOutput += QString::fromLocal8Bit(m_cmd->readAllStandardError());
        emit outputChanged();
    });
    connect(m_cmd, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, command](int code, QProcess::ExitStatus st) {
        m_lastOutput += QString::fromLocal8Bit(m_cmd->readAllStandardOutput()) + QString::fromLocal8Bit(m_cmd->readAllStandardError());
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (m_cmd) {
            m_cmd->deleteLater();
            m_cmd = nullptr;
        }
        emit commandRunningChanged();
        emit outputChanged();
        emit commandFinished(QVariantMap{{QStringLiteral("ok"), ok},
                                         {QStringLiteral("exitCode"), code},
                                         {QStringLiteral("output"), m_lastOutput},
                                         {QStringLiteral("command"), command}});
        emit statusMessage(ok ? QStringLiteral("SSH — command finished") : QStringLiteral("SSH — command failed (") + QString::number(code) + QStringLiteral(")"));
    });
    connect(m_cmd, &QProcess::errorOccurred, this, [this, command](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_lastOutput = QStringLiteral("Failed to start ssh");
        if (m_cmd) {
            m_cmd->deleteLater();
            m_cmd = nullptr;
        }
        emit commandRunningChanged();
        emit outputChanged();
        emit commandFinished(QVariantMap{{QStringLiteral("ok"), false},
                                         {QStringLiteral("exitCode"), -1},
                                         {QStringLiteral("output"), m_lastOutput},
                                         {QStringLiteral("command"), command}});
        emit statusMessage(QStringLiteral("SSH — ssh binary not found"));
    });
    emit commandRunningChanged();
    m_cmd->start(spec.program, spec.args);
}

void SshManager::cancelCommand()
{
    if (m_cmd)
        m_cmd->kill();
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
    // wt.exe preferred; fall back to the classic console host.
    if (QStandardPaths::findExecutable(QStringLiteral("wt.exe")).isEmpty()) {
        term.program = QStringLiteral("cmd");
        term.args = {QStringLiteral("/c"), QStringLiteral("start"), QStringLiteral("\"\""), QStringLiteral("ssh")};
        term.args << ssh.args;
        term.ok = true;
    } else {
        term.program = QStringLiteral("wt.exe");
        term.args = ssh.args;
        term.ok = true;
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
        emit statusMessage(QStringLiteral("SSH — no terminal found (try gnome-terminal or konsole)"));
        return;
    }
    QProcess::startDetached(term.program, term.args);
    emit statusMessage(QStringLiteral("SSH — opened terminal for %1@%2").arg(c.user, c.host));
}
