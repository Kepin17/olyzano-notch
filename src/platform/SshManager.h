#pragma once

#include "platform/SshModel.h"

#include <QObject>
#include <QProcess>
#include <QVariantList>

// The SSH manager as `sshManager` in QML: save a list of hosts, test them, run one remote command
// (stdout/stderr in a pane) and open a system terminal. Passwords never go on argv; key/agent is preferred.
class SshManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool commandRunning READ commandRunning NOTIFY commandRunningChanged)
    Q_PROPERTY(QString lastOutput READ lastOutput NOTIFY outputChanged)

public:
    explicit SshManager(QObject *parent = nullptr);

    QVariantList connections() const;
    bool busy() const { return m_busy; }
    bool commandRunning() const { return m_cmd != nullptr; }
    QString lastOutput() const { return m_lastOutput; }

    Q_INVOKABLE QString addConnection(const QVariantMap &fields); // -> new id, "" on invalid host/user
    Q_INVOKABLE bool updateConnection(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool removeConnection(const QString &id);
    Q_INVOKABLE QString duplicateConnection(const QString &id);
    Q_INVOKABLE QVariantMap connectionById(const QString &id) const;
    Q_INVOKABLE void testConnection(const QString &id);
    Q_INVOKABLE void runCommand(const QString &id, const QString &command);
    Q_INVOKABLE void openTerminal(const QString &id); // system terminal, pre-connected (argv only)
    Q_INVOKABLE void cancelCommand();

signals:
    void changed();
    void busyChanged();
    void commandRunningChanged();
    void outputChanged();
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
    void finishTest(const QString &id, bool ok, const QString &error, qint64 ms);

    QList<SshModel::Connection> m_list;
    QProcess *m_cmd = nullptr;
    QProcess *m_test = nullptr;
    QString m_lastOutput;
    bool m_busy = false;
};
