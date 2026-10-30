#pragma once

#include "platform/DbModel.h"

#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>

// The database browser as `dbManager` in QML: save connections (SQLite / MySQL / PostgreSQL),
// list schemas and tables, browse a table, and run free-form SQL. Drivers that are not installed
// show up as unavailable instead of crashing.
class DatabaseManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)
    Q_PROPERTY(QStringList drivers READ drivers NOTIFY driversChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString activeId READ activeId NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY resultReady)
    Q_PROPERTY(QVariantMap lastResult READ lastResult NOTIFY resultReady)

public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager() override;

    QVariantList connections() const;
    QStringList drivers() const; // ui ids that QSqlDatabase reports available
    bool connected() const { return m_connected; }
    QString activeId() const { return m_activeId; }
    bool busy() const { return m_busy; }
    QString lastError() const { return m_lastError; }
    QVariantMap lastResult() const { return m_last; }

    Q_INVOKABLE QString addConnection(const QVariantMap &fields);
    Q_INVOKABLE bool updateConnection(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool removeConnection(const QString &id);
    Q_INVOKABLE QVariantMap connectionById(const QString &id) const;
    Q_INVOKABLE void connectTo(const QString &id);
    Q_INVOKABLE void disconnectDb();
    Q_INVOKABLE QVariantList schemas();
    Q_INVOKABLE QVariantList tables(const QString &schema = QString());
    Q_INVOKABLE bool useDatabase(const QString &schema); // MySQL: USE `schema`; others: just switch browse context
    Q_INVOKABLE void browseTable(const QString &schema, const QString &table, int limit = 50, int offset = 0);
    Q_INVOKABLE void execute(const QString &sql);

signals:
    void changed();
    void driversChanged();
    void connectedChanged();
    void busyChanged();
    void resultReady(); // lastResult / lastError updated
    void statusMessage(const QString &text);

private:
    void load();
    void save() const;
    QString configPath() const;
    QString secretsPath() const;
    DbModel::Connection *find(const QString &id);
    const DbModel::Connection *find(const QString &id) const;
    QString passwordFor(const QString &id) const;
    void storePassword(const QString &id, const QString &password);
    void dropPassword(const QString &id);
    void closeActive();
    void setBusy(bool on);
    static QString expandTilde(const QString &path);

    QList<DbModel::Connection> m_list;
    QString m_activeId;
    QString m_connName;
    bool m_connected = false;
    bool m_busy = false;
    QString m_lastError;
    QVariantMap m_last;
};
