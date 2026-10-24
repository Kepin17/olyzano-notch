#pragma once

#include <QString>

// Encrypted storage for locked categories, using gocryptfs (the engine behind Plasma Vault).
// The encrypted files live in cipherDir on disk; while unlocked they are reachable in plain form
// only through a FUSE mount at mountDir (put it on tmpfs, e.g. $XDG_RUNTIME_DIR). Passwords go
// to gocryptfs over stdin and are never stored or logged.
class Vault
{
public:
    Vault(const QString &cipherDir, const QString &mountDir);

    // Locked categories rely on gocryptfs + FUSE: Linux only for now.
    static bool supported()
    {
#ifdef Q_OS_LINUX
        return true;
#else
        return false;
#endif
    }

    QString cipherDir() const { return m_cipherDir; }
    QString mountDir() const { return m_mountDir; }
    bool exists() const;     // created (gocryptfs.conf present)
    bool isUnlocked() const; // mounted right now

    // Each returns an empty string on success, else a user-facing reason.
    QString create(const QString &password);
    QString unlock(const QString &password);
    QString lock(); // lazy unmount: works even while a viewer still has a file open

private:
    QString m_cipherDir;
    QString m_mountDir;
};
