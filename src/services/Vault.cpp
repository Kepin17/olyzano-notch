#include "Vault.h"

#include "services/ShareCrypto.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QProcess>
#include <QSaveFile>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

Vault::Vault(const QString &cipherDir, const QString &mountDir, Engine engine)
    : m_cipherDir(cipherDir)
    , m_mountDir(mountDir)
    , m_engine(engine)
{
}

Vault::~Vault()
{
    m_key.fill('\0');
}

QString Vault::configFile() const
{
    return m_cipherDir + (m_engine == Gocryptfs ? QStringLiteral("/gocryptfs.conf") : QStringLiteral("/notch-vault.json"));
}

bool Vault::exists() const
{
    return QFileInfo::exists(configFile());
}

bool Vault::isUnlocked() const
{
    if (m_engine == Files)
        return !m_key.isEmpty();
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
    if (m_engine == Files) {
        QByteArray key = ShareCrypto::newKey();
        const QString wrapped = key.isEmpty() ? QString() : ShareCrypto::wrapKey(key, password);
        key.fill('\0');
        if (wrapped.isEmpty())
            return QStringLiteral("no random numbers available");
        QSaveFile f(configFile());
        if (!f.open(QIODevice::WriteOnly))
            return QStringLiteral("cannot create the vault folder");
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("notch-vault")},
                                          {QStringLiteral("version"), 1},
                                          {QStringLiteral("key"), wrapped}})
                    .toJson());
        return f.commit() ? QString() : QStringLiteral("cannot create the vault folder");
    }
    return run(QStringLiteral("gocryptfs"), {QStringLiteral("-init"), QStringLiteral("-q"), m_cipherDir}, password);
}

QString Vault::unlock(const QString &password)
{
    if (isUnlocked())
        return {};
    if (m_engine == Files)
        return unlockFiles(password);
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
    if (m_engine == Files)
        return lockFiles();
    return run(QStringLiteral("fusermount3"), {QStringLiteral("-u"), QStringLiteral("-z"), m_mountDir}, QString());
}

// ---- Files engine ----

namespace {
const QString UnlockTemp = QStringLiteral(".notch-unlocking");

QByteArray contentHash(const QString &path)
{
    QFile f(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return f.open(QIODevice::ReadOnly) && hash.addData(&f) ? hash.result() : QByteArray();
}
}

// A keyed hash, so the encrypted file's name says nothing about the file and only this vault can map one to it.
QString Vault::cipherFileFor(const QString &name) const
{
    const QByteArray mac = QMessageAuthenticationCode::hash(name.toUtf8(), m_key, QCryptographicHash::Sha256);
    return m_cipherDir + u'/' + QString::fromLatin1(mac.toHex().left(32)) + QStringLiteral(".nv");
}

QString Vault::unlockFiles(const QString &password)
{
    QFile config(configFile());
    if (!config.open(QIODevice::ReadOnly))
        return QStringLiteral("the vault is missing");
    QByteArray key = ShareCrypto::unwrapKey(QJsonDocument::fromJson(config.readAll()).object().value(QStringLiteral("key")).toString(),
                                            password);
    if (key.isEmpty())
        return QStringLiteral("wrong password");
    if (!QDir().mkpath(m_mountDir)) {
        key.fill('\0');
        return QStringLiteral("cannot create %1").arg(m_mountDir);
    }
#ifdef Q_OS_WIN
    // Kept out of Windows Search's index; files created inside inherit it.
    const std::wstring native = QDir::toNativeSeparators(m_mountDir).toStdWString();
    SetFileAttributesW(native.c_str(), GetFileAttributesW(native.c_str()) | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
#endif
    m_key = key;
    key.fill('\0');
    m_plain.clear();

    // Files still here from a session that ended without locking are the latest state: they win over their
    // encrypted copy, and the next lock encrypts them.
    QDir mount(m_mountDir);
    const QStringList leftovers = mount.entryList(QDir::Files | QDir::Hidden | QDir::System);
    const QString temp = m_mountDir + u'/' + UnlockTemp;
    const QFileInfoList sealed = QDir(m_cipherDir).entryInfoList({QStringLiteral("*.nv")}, QDir::Files);
    for (const QFileInfo &file : sealed) {
        QDir(temp).removeRecursively();
        QStringList names;
        if (const QString err = ShareCrypto::decryptFiles(file.filePath(), temp, m_key, &names); !err.isEmpty()) {
            qWarning("Vault: %s: %s", qPrintable(file.fileName()), qPrintable(err)); // skip it, open the rest
            continue;
        }
        for (const QString &name : std::as_const(names)) {
            if (leftovers.contains(name))
                continue;
            const QString to = m_mountDir + u'/' + name;
            if (!QFile::rename(temp + u'/' + name, to))
                continue;
            m_plain.insert(name, Entry{QFileInfo(to).size(), contentHash(to)});
        }
    }
    QDir(temp).removeRecursively();
    return {};
}

QString Vault::lockFiles()
{
    const QFileInfoList files = QDir(m_mountDir).entryInfoList(QDir::Files | QDir::Hidden | QDir::System);
    QSet<QString> present;
    for (const QFileInfo &file : files) {
        const QString name = file.fileName();
        present.insert(name);
        const auto known = m_plain.constFind(name);
        const QByteArray hash = contentHash(file.filePath());
        if (known != m_plain.cend() && known->size == file.size() && known->hash == hash)
            continue; // unchanged since it was decrypted
        const QString out = cipherFileFor(name);
        const QString part = out + QStringLiteral(".part");
        if (const QString err = ShareCrypto::encryptFiles({{file.filePath(), name, QString()}}, part, m_key); !err.isEmpty()) {
            QFile::remove(part);
            return QStringLiteral("cannot encrypt %1: %2").arg(name, err);
        }
        QFile::remove(out);
        if (!QFile::rename(part, out))
            return QStringLiteral("cannot save %1").arg(name);
        m_plain.insert(name, Entry{file.size(), hash});
    }
    for (auto it = m_plain.begin(); it != m_plain.end();) {
        if (present.contains(it.key())) {
            ++it;
            continue;
        }
        QFile::remove(cipherFileFor(it.key())); // removed while unlocked
        it = m_plain.erase(it);
    }

    // Everything is sealed: now the plain copies go. One still open elsewhere (Windows won't delete it) keeps
    // the vault unlocked, so locking again later finishes the job.
    QStringList busy;
    for (const QFileInfo &file : files) {
        if (QFile::remove(file.filePath()))
            m_plain.remove(file.fileName());
        else
            busy << file.fileName();
    }
    if (!busy.isEmpty())
        return QStringLiteral("close %1 first, then lock again").arg(busy.join(QStringLiteral(", ")));
    QDir(m_mountDir).removeRecursively();
    m_key.fill('\0');
    m_key.clear();
    m_plain.clear();
    return {};
}
