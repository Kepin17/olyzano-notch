#pragma once

#include "platform/ApiTesterModel.h"

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QVariantList>

// The API tester as `apiTester` in QML: craft HTTP requests (local or internet), keep collections /
// environments / history on disk, run short pre/test scripts, import-export Postman v2.1, and
// check local ports (list / stop the listener / run a stop command).
class ApiTester : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList collections READ collections NOTIFY changed)   // flattened rows for the sidebar
    Q_PROPERTY(QVariantList environments READ environments NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(QVariantList ports READ ports NOTIFY portsChanged)         // watched local ports + last probe
    Q_PROPERTY(QString activeEnvId READ activeEnvId WRITE setActiveEnvId NOTIFY changed)
    Q_PROPERTY(QVariantList runItems READ runItems NOTIFY runChanged)  // the requests of the run in progress or just done
    Q_PROPERTY(bool running READ running NOTIFY runChanged)
    Q_PROPERTY(QVariantMap runSummary READ runSummary NOTIFY runChanged) // {passed, failed, pending, totalMs, total}
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap lastResponse READ lastResponse NOTIFY responseChanged)
    Q_PROPERTY(bool historyEnabled READ historyEnabled WRITE setHistoryEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int historyLimit READ historyLimit WRITE setHistoryLimit NOTIFY settingsChanged)
    Q_PROPERTY(bool prettyJson READ prettyJson WRITE setPrettyJson NOTIFY settingsChanged)
    Q_PROPERTY(bool followRedirects READ followRedirects WRITE setFollowRedirects NOTIFY settingsChanged)
    Q_PROPERTY(int timeoutMs READ timeoutMs WRITE setTimeoutMs NOTIFY settingsChanged)
    Q_PROPERTY(int portInterval READ portInterval WRITE setPortInterval NOTIFY settingsChanged) // seconds between background checks of the ports with an alert
    Q_PROPERTY(bool commandRunning READ commandRunning NOTIFY commandRunningChanged)

public:
    explicit ApiTester(QObject *parent = nullptr);

    QVariantList collections() const;
    QVariantList environments() const;
    QVariantList history() const;
    QVariantList ports() const;
    QString activeEnvId() const { return m_activeEnvId; }
    void setActiveEnvId(const QString &id);
    bool busy() const { return m_busy; }
    QVariantMap lastResponse() const { return m_last; }

    bool historyEnabled() const { return m_settings.value(QStringLiteral("historyEnabled"), true).toBool(); }
    void setHistoryEnabled(bool on);
    int historyLimit() const { return m_settings.value(QStringLiteral("historyLimit"), 50).toInt(); }
    void setHistoryLimit(int n);
    bool prettyJson() const { return m_settings.value(QStringLiteral("prettyJson"), true).toBool(); }
    void setPrettyJson(bool on);
    bool followRedirects() const { return m_settings.value(QStringLiteral("followRedirects"), true).toBool(); }
    void setFollowRedirects(bool on);
    int timeoutMs() const { return m_settings.value(QStringLiteral("timeoutMs"), 30000).toInt(); }
    void setTimeoutMs(int ms);

    // Draft comes from the builder: method, url, headers[], query[], bodyMode, body, auth{...}, preScript, testScript.
    Q_INVOKABLE void send(const QVariantMap &draft);
    Q_INVOKABLE void cancel();
    // Run every request under a collection or folder (or one request), one after the other, and judge each by its tests (or its status).
    Q_INVOKABLE void runCollection(const QString &id, bool stopOnFailure = false);
    Q_INVOKABLE void cancelRun();
    Q_INVOKABLE QVariantMap runResponse(int index) const; // the full response of one finished item of the run
    QVariantList runItems() const;
    bool running() const { return m_running; }
    QVariantMap runSummary() const; // drops the in-flight reply (response becomes a cancelled error)

    Q_INVOKABLE QVariantMap requestById(const QString &id) const; // full RequestSpec as a map for the builder
    // The tree: every collection, folder and request is the person's own, so all of it can be added, renamed, copied, moved and deleted.
    Q_INVOKABLE QString newCollection(const QString &name);
    Q_INVOKABLE QString addRequest(const QString &parentId, const QString &folderId = QString()); // parentId: a collection or a folder
    Q_INVOKABLE QString addFolder(const QString &parentId, const QString &name = QString());
    Q_INVOKABLE QString duplicateNode(const QString &id); // a collection, folder or request; -> the copy's id
    Q_INVOKABLE bool moveNode(const QString &id, const QString &parentId, int index = -1);
    // curl: a request as a command (variables kept, or filled from the active environment), and a command as a new request in `parentId`.
    Q_INVOKABLE QString copyAsCurl(const QVariantMap &draft, bool resolved) const;
    Q_INVOKABLE QString importCurl(const QString &command, const QString &parentId); // -> the new request's id; "" and a status message when it is not understood
    Q_INVOKABLE QVariantMap parseCurl(const QString &command) const;                  // {ok, error, request:{…builder map…}} without adding anything
    Q_INVOKABLE QString saveRequestAs(const QVariantMap &draft, const QString &parentId, const QString &name);
    Q_INVOKABLE void updateRequest(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE void renameNode(const QString &id, const QString &name);
    Q_INVOKABLE void removeNode(const QString &id);

    Q_INVOKABLE QString newEnvironment(const QString &name);
    Q_INVOKABLE void updateEnvironment(const QString &id, const QVariantMap &fields); // name + variables as list of maps
    Q_INVOKABLE void removeEnvironment(const QString &id);
    Q_INVOKABLE QString duplicateEnvironment(const QString &id);
    Q_INVOKABLE QVariantMap environmentById(const QString &id) const;

    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void removeHistory(const QString &id);

    Q_INVOKABLE QString importPostmanCollection(const QString &json); // "" on success, else error; returns status text
    Q_INVOKABLE QString exportPostmanCollection(const QString &id) const;
    Q_INVOKABLE QString importNativeCollection(const QString &json);
    Q_INVOKABLE QString exportNativeCollection(const QString &id) const;
    Q_INVOKABLE QString importEnvironment(const QString &json);
    Q_INVOKABLE QString exportEnvironment(const QString &id) const;

    // Local ports: watch a list, probe TCP on 127.0.0.1, stop the listener, or run a shell stop command.
    Q_INVOKABLE void addPort(int port);
    Q_INVOKABLE void removePort(int port);
    Q_INVOKABLE void checkPort(int port);   // sync probe (localhost, 400 ms) → portChecked + portsChanged
    Q_INVOKABLE void checkAllPorts();
    Q_INVOKABLE void setPortAlert(int port, bool on); // watch it in the background and tell when it goes up or down
    int portInterval() const { return m_settings.value(QStringLiteral("portInterval"), 10).toInt(); }
    void setPortInterval(int seconds);
    Q_INVOKABLE QString stopService(int port); // SIGTERM / taskkill on the listening PID; returns a status line
    Q_INVOKABLE void runServiceCommand(const QString &cmd); // e.g. "docker compose stop relay"; async → serviceCommandFinished
    bool commandRunning() const { return m_cmdRunning; }

    Q_INVOKABLE static QString pretty(const QByteArray &body) { return ApiTesterModel::prettyJson(body); }

signals:
    void changed();
    void busyChanged();
    void runChanged();
    void responseChanged();
    void settingsChanged();
    void statusMessage(const QString &text); // import/export/save toasts
    void portsChanged();
    void portStateChanged(int port, bool open, const QString &process); // a port with an alert went up or down
    void portChecked(const QVariantMap &info); // {port, open, pid, process, error}
    void serviceCommandFinished(const QVariantMap &result); // {ok, output, exitCode, command}
    void commandRunningChanged();

private:
    struct FlatRow {
        QString id, parentId, name, kind; // kind: collection | folder | request | history
        QString method;
        int depth = 0;
    };

    void load();
    void commit(); // save the collections and tell QML
    void runNext();
    void onRunResponse();
    void saveCollections() const;
    void saveEnvironments() const;
    void saveHistory() const;
    void flatten(const ApiTesterModel::RequestNode &n, const QString &parentId, int depth, QList<FlatRow> &out) const;
    ApiTesterModel::RequestNode *findNode(const QString &id, ApiTesterModel::Collection **coll = nullptr);
    const ApiTesterModel::RequestNode *findNode(const QString &id) const;
    void applyDraftTo(ApiTesterModel::RequestSpec &r, const QVariantMap &draft) const;
    QVariantMap specToMap(const ApiTesterModel::RequestSpec &r) const;
    QVector<ApiTesterModel::KeyValue> activeVars() const;
    void runScript(const QString &source, bool isTest, const QVariantMap &response);
    QByteArray buildBody(const ApiTesterModel::RequestSpec &r, QString *contentType) const;

    QString dataDir() const;
    QString configPath() const;
    void savePorts() const;
    int findListenerPid(int port, QString *process) const; // -1 when unknown

    struct PortEntry {
        int port = 0;
        bool open = false;
        int pid = -1;
        QString process;
        QString checkedAt; // local time HH:mm:ss of last probe
        QString error;
        bool alert = false; // tell me when it goes up or down
        int known = -1;     // what the last probe said: -1 none yet, 0 closed, 1 open
    };
    void applyProbe(int port, bool open);
    void probeAlertPorts();
    void updatePortTimer();

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    mutable QSettings m_settings;
    QList<ApiTesterModel::Collection> m_collections;
    QList<ApiTesterModel::Environment> m_environments;
    QList<ApiTesterModel::HistoryEntry> m_history;
    QList<PortEntry> m_ports;
    QTimer m_portTimer;
    QString m_activeEnvId;
    bool m_busy = false;
    bool m_running = false, m_runStopOnFail = false;
    QList<ApiTesterModel::RequestSpec> m_runQueue;
    QList<ApiTesterModel::RunItem> m_runItems;
    QList<QVariantMap> m_runResponses;
    int m_runIndex = -1;
    bool m_cmdRunning = false;
    QVariantMap m_last;
    QStringList m_scriptLog;
    QList<ApiTesterModel::TestResult> m_pendingTests;
    QStringList m_pendingPreLog;
    QElapsedTimer m_sendTimer;
    ApiTesterModel::RequestSpec m_inflight; // substituted copy used for history
    bool m_migrated = false; // the file on disk still held the old built-in templates
    QProcess *m_cmdProc = nullptr;
};
