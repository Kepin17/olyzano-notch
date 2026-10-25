#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

// Encrypted storage for locked categories, with one of two engines:
// - Gocryptfs (Linux): the engine behind Plasma Vault. The encrypted files live in cipherDir on disk;
//   while unlocked they are reachable in plain form only through a FUSE mount at mountDir (put it on
//   tmpfs, e.g. $XDG_RUNTIME_DIR). Passwords go to gocryptfs over stdin and are never stored or logged.
// - Files (Windows, elsewhere): each file is its own AES-256-GCM container in cipherDir (ShareCrypto's
//   format, its name inside, the file named by a keyed hash), under one random key that is kept wrapped
//   by the password (PBKDF2). Unlocking decrypts them into mountDir; locking encrypts what changed, drops
//   what was removed and deletes mountDir. The key lives only in memory, so after a crash the plain files
//   stay in mountDir, unseen by Notch, until the next unlock picks them up as the latest state.
class Vault
{
public:
    enum Engine { Gocryptfs, Files };
    static Engine defaultEngine()
    {
#ifdef Q_OS_LINUX
        return Gocryptfs;
#else
        return Files;
#endif
    }

    Vault(const QString &cipherDir, const QString &mountDir, Engine engine = defaultEngine());
    ~Vault();

    static bool supported() { return true; }

    QString cipherDir() const { return m_cipherDir; }
    QString mountDir() const { return m_mountDir; }
    bool exists() const;     // created
    bool isUnlocked() const; // mounted / decrypted right now

    // Each returns an empty string on success, else a user-facing reason.
    QString create(const QString &password);
    QString unlock(const QString &password);
    QString lock(); // Gocryptfs: lazy unmount, works even while a viewer still has a file open

private:
    QString configFile() const;
    QString cipherFileFor(const QString &name) const;
    QString unlockFiles(const QString &password);
    QString lockFiles();

    struct Entry {
        qint64 size = -1;
        QByteArray hash; // SHA-256 of the content
    };

    QString m_cipherDir;
    QString m_mountDir;
    Engine m_engine;
    QByteArray m_key;               // Files: the vault key while unlocked
    QHash<QString, Entry> m_plain;  // Files: name -> the decrypted file as written, to find changes on lock
};
