#include "Vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace {
// Runs gocryptfs (or fusermount) with the password on stdin. Empty string on success.
QString run(const QString &program, const QStringList &args, const QString &password, int *exitCode = nullptr)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(5000))
        return QStringLiteral("%1 is not installed").arg(program);
    if (!password.isNull()) {
        QByteArray secret = password.toUtf8() + '\n';
        p.write(secret);
        secret.fill('\0');
    }
    p.closeWriteChannel();
    if (!p.waitForFinished(60000)) {
        p.kill();
        return QStringLiteral("%1 did not answer").arg(program);
    }
    if (exitCode)
        *exitCode = p.exitCode();
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
        return {};
    const QString err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed().section(u'\n', -1);
    return err.isEmpty() ? QStringLiteral("%1 failed").arg(program) : err;
}
}

Vault::Vault(const QString &cipherDir, const QString &mountDir)
    : m_cipherDir(cipherDir)
    , m_mountDir(mountDir)
{
}

bool Vault::exists() const
{
    return QFileInfo::exists(m_cipherDir + QStringLiteral("/gocryptfs.conf"));
}

bool Vault::isUnlocked() const
{
    QFile mounts(QStringLiteral("/proc/self/mounts"));
    if (!mounts.open(QIODevice::ReadOnly))
        return false;
    const QByteArray target = ' ' + QDir(m_mountDir).absolutePath().toUtf8() + ' ';
    return mounts.readAll().contains(target);
}

QString Vault::create(const QString &password)
{
    if (exists())
        return QStringLiteral("a vault already exists");
    if (password.size() < 8)
        return QStringLiteral("use at least 8 characters");
    if (!QDir().mkpath(m_cipherDir))
        return QStringLiteral("cannot create the vault folder");
    return run(QStringLiteral("gocryptfs"), {QStringLiteral("-init"), QStringLiteral("-q"), m_cipherDir}, password);
}

QString Vault::unlock(const QString &password)
{
    if (isUnlocked())
        return {};
    if (!QDir().mkpath(m_mountDir))
        return QStringLiteral("cannot create %1").arg(m_mountDir);
    int code = 0;
    const QString err = run(QStringLiteral("gocryptfs"), {QStringLiteral("-q"), m_cipherDir, m_mountDir}, password, &code);
    if (code == 12)
        return QStringLiteral("wrong password");
    return err;
}

QString Vault::lock()
{
    if (!isUnlocked())
        return {};
    return run(QStringLiteral("fusermount3"), {QStringLiteral("-u"), QStringLiteral("-z"), m_mountDir}, QString());
}
