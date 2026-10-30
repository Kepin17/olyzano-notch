#pragma once

#include "platform/SshModel.h"

#include <QObject>
#include <QProcess>
#include <QVariantList>

// The SSH manager as `sshManager` in QML: save a list of hosts, test them, keep a persistent
// interactive shell (`ssh -tt`) for the terminal pane, run one-shot commands, or open a system terminal.
// Passwords never go on argv; key/agent is preferred.
class SshManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool commandRunning READ commandRunning NOTIFY commandRunningChanged)
    Q_PROPERTY(bool shellOpen READ shellOpen NOTIFY shellStateChanged)
    Q_PROPERTY(QString shellHost READ shellHost NOTIFY shellStateChanged)
    Q_PROPERTY(QString lastOutput READ lastOutput NOTIFY outputChanged)

public:
    explicit SshManager(QObject *parent = nullptr);
    ~SshManager() override;

    QVariantList connections() const;
    bool busy() const { return m_busy; }
    bool commandRunning() const { return m_cmd != nullptr; }
    bool shellOpen() const { return m_shell != nullptr; }
    QString shellHost() const { return m_shellHost; }
    QString lastOutput() const { return m_lastOutput; }

    Q_INVOKABLE QString addConnection(const QVariantMap &fields); // -> new id, "" on invalid host/user
    Q_INVOKABLE bool updateConnection(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool removeConnection(const QString &id);
    Q_INVOKABLE QString duplicateConnection(const QString &id);
    Q_INVOKABLE QVariantMap connectionById(const QString &id) const;
    Q_INVOKABLE void testConnection(const QString &id);
    // Persistent interactive shell (ssh -tt). One shell at a time; openShell replaces an old one.
    Q_INVOKABLE void openShell(const QString &id);
    Q_INVOKABLE void closeShell();
    Q_INVOKABLE void writeShell(const QString &line); // one line to the remote shell (adds \\n)
    Q_INVOKABLE void runCommand(const QString &id, const QString &command); // one-shot (no shell)
    Q_INVOKABLE void openTerminal(const QString &id); // system terminal, pre-connected (argv only)
    Q_INVOKABLE void cancelCommand();

signals:
    void changed();
    void busyChanged();
    void commandRunningChanged();
    void shellStateChanged();
    void outputChanged();
    void shellOutput(const QString &chunk); // live bytes from the interactive shell
    void testFinished(const QVariantMap &result); // {id, ok, error, latencyMs}
    void commandFinished(const QVariantMap &result); // {ok, exitCode, output, command}
    void statusMessage(const QString &text);

private:
    struct SshProcessSpec {
        QString program;
        QStringList args;
    };

    void load();
    void save() const;
    QString configPath() const;
    QString secretsPath() const;
    SshModel::Connection *find(const QString &id);
    const SshModel::Connection *find(const QString &id) const;
    QString passwordFor(const QString &id) const;
    void storePassword(const QString &id, const QString &password);
    void dropPassword(const QString &id);
    SshProcessSpec buildSsh(const SshModel::Connection &c, const QString &command, bool batchMode) const;
    static QString expandTilde(const QString &path);
    // Absolute path to the OpenSSH client, or "" when none is on PATH / the Windows system location.
    static QString findSsh();
    // When auth is password and a secret is saved, point SSH_ASKPASS at a private temp helper so
    // Test / Run / the embedded shell can authenticate without a real TTY (Windows especially).
    // Returns system env (unchanged) when no saved password applies. On success *askPassDir is the
    // unique temp dir the caller must remove after the process finishes (removeAskPass).
    QProcessEnvironment sshEnvironment(const SshModel::Connection &c, QString *askPassDir);
    static void removeAskPass(const QString &dir);
    void finishTest(const QString &id, bool ok, const QString &error, qint64 ms);
    void teardownShell(const QString &note);

    QList<SshModel::Connection> m_list;
    QProcess *m_cmd = nullptr;
    QProcess *m_test = nullptr;
    QProcess *m_shell = nullptr;
    QString m_shellHost;
    QString m_lastOutput;
    QString m_shellAskPass; // temp dir for the live shell's askpass helper ("" = none)
    bool m_busy = false;
};
