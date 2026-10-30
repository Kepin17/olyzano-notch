#include "DatabaseManager.h"

#include "platform/NotchPaths.h"
#include "platform/SecureFiles.h"
#include "platform/SecretsStore.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QUuid>

namespace {

// secrets.json section key for this manager
constexpr auto kSecretsSection = "db";

QStringList knownDrivers()
{
    return {QStringLiteral("sqlite"), QStringLiteral("mysql"), QStringLiteral("postgres")};
}

} // namespace

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
{
    load();
}

DatabaseManager::~DatabaseManager()
{
    closeActive();
}

QString DatabaseManager::configPath() const
{
    return SecureFiles::ownerOnlyPath(NotchPaths::devDir(), QStringLiteral("db-connections.json"));
}

QString DatabaseManager::secretsPath() const
{
    return SecretsStore::secretsPath();
}

void DatabaseManager::load()
{
    QFile f(configPath());
    if (f.open(QIODevice::ReadOnly))
        m_list = DbModel::listFromJson(f.readAll());
}

void DatabaseManager::save() const
{
    QSaveFile f(configPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(DbModel::listToJson(m_list));
        f.commit();
    }
    QFile::setPermissions(configPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QString DatabaseManager::expandTilde(const QString &path)
{
    if (path.startsWith(QLatin1Char('~')))
        return QDir::homePath() + path.mid(1);
    return path;
}

DbModel::Connection *DatabaseManager::find(const QString &id)
{
    for (auto &c : m_list)
        if (c.id == id)
            return &c;
    return nullptr;
}

const DbModel::Connection *DatabaseManager::find(const QString &id) const
{
    return const_cast<DatabaseManager *>(this)->find(id);
}

QString DatabaseManager::passwordFor(const QString &id) const
{
    return SecretsStore::get(QString::fromLatin1(kSecretsSection), id);
}

void DatabaseManager::storePassword(const QString &id, const QString &password)
{
    SecretsStore::set(QString::fromLatin1(kSecretsSection), id, password);
}

void DatabaseManager::dropPassword(const QString &id)
{
    storePassword(id, {});
}

QStringList DatabaseManager::drivers() const
{
    QStringList out;
    for (const QString &ui : knownDrivers())
        if (QSqlDatabase::isDriverAvailable(DbModel::qtDriver(ui)))
            out << ui;
    return out;
}

QVariantList DatabaseManager::connections() const
{
    QVariantList out;
    const QStringList avail = drivers();
    for (const auto &c : m_list)
        out << QVariantMap{{QStringLiteral("id"), c.id},
                           {QStringLiteral("name"), c.name},
                           {QStringLiteral("driver"), c.driver},
                           {QStringLiteral("host"), c.host},
                           {QStringLiteral("port"), c.port},
                           {QStringLiteral("user"), c.user},
                           {QStringLiteral("database"), c.database},
                           {QStringLiteral("sqlitePath"), c.sqlitePath},
                           {QStringLiteral("savePassword"), c.savePassword},
                           {QStringLiteral("hasPassword"), !passwordFor(c.id).isEmpty()},
                           {QStringLiteral("driverAvailable"), avail.contains(c.driver)},
                           {QStringLiteral("label"),
                            c.driver == QLatin1String("sqlite")
                                ? QFileInfo(c.sqlitePath).fileName()
                                : QStringLiteral("%1@%2:%3/%4").arg(c.user, c.host).arg(c.port).arg(c.database)}};
    return out;
}

QVariantMap DatabaseManager::connectionById(const QString &id) const
{
    const auto *c = find(id);
    if (!c)
        return {};
    return QVariantMap{{QStringLiteral("id"), c->id},
                       {QStringLiteral("name"), c->name},
                       {QStringLiteral("driver"), c->driver},
                       {QStringLiteral("host"), c->host},
                       {QStringLiteral("port"), c->port},
                       {QStringLiteral("user"), c->user},
                       {QStringLiteral("database"), c->database},
                       {QStringLiteral("sqlitePath"), c->sqlitePath},
                       {QStringLiteral("savePassword"), c->savePassword},
                       {QStringLiteral("hasPassword"), !passwordFor(c->id).isEmpty()}};
}

QString DatabaseManager::addConnection(const QVariantMap &fields)
{
    DbModel::Connection c;
    c.id = DbModel::newId();
    DbModel::applyFields(c, QJsonObject::fromVariantMap(fields));
    if (c.name.isEmpty())
        c.name = c.driver == QLatin1String("sqlite")
                     ? (c.sqlitePath.isEmpty() ? QStringLiteral("SQLite") : QFileInfo(c.sqlitePath).fileName())
                     : (c.host.isEmpty() ? QStringLiteral("Connection") : c.host);
    if (c.driver != QLatin1String("mysql") && c.driver != QLatin1String("postgres") && c.driver != QLatin1String("sqlite"))
        c.driver = QStringLiteral("sqlite");
    if (c.port <= 0)
        c.port = DbModel::defaultPort(c.driver);
    m_list << c;
    // password lives only in secrets.json, never in the connection list
    if (fields.contains(QStringLiteral("password")))
        storePassword(c.id, fields.value(QStringLiteral("password")).toString());
    save();
    emit changed();
    return c.id;
}

bool DatabaseManager::updateConnection(const QString &id, const QVariantMap &fields)
{
    auto *c = find(id);
    if (!c)
        return false;
    DbModel::applyFields(*c, QJsonObject::fromVariantMap(fields));
    if (c->name.trimmed().isEmpty())
        c->name = c->host.isEmpty() ? QStringLiteral("Connection") : c->host;
    if (c->port <= 0)
        c->port = DbModel::defaultPort(c->driver);
    if (fields.contains(QStringLiteral("password")))
        storePassword(c->id, fields.value(QStringLiteral("password")).toString());
    save();
    emit changed();
    return true;
}

bool DatabaseManager::removeConnection(const QString &id)
{
    for (int i = 0; i < m_list.size(); ++i)
        if (m_list[i].id == id) {
            if (m_activeId == id)
                closeActive();
            dropPassword(id);
            m_list.removeAt(i);
            save();
            emit changed();
            return true;
        }
    return false;
}

void DatabaseManager::setBusy(bool on)
{
    if (m_busy == on)
        return;
    m_busy = on;
    emit busyChanged();
}

void DatabaseManager::closeActive()
{
    if (!m_connName.isEmpty()) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connName, false);
            if (db.isValid())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_connName);
        m_connName.clear();
    }
    if (m_connected) {
        m_connected = false;
        m_activeId.clear();
        emit connectedChanged();
    }
}

void DatabaseManager::connectTo(const QString &id)
{
    closeActive();
    m_lastError.clear();
    m_last = {};
    emit resultReady();
    const auto *cp = find(id);
    if (!cp) {
        m_lastError = QStringLiteral("Unknown connection");
        emit resultReady();
        return;
    }
    const DbModel::Connection c = *cp;
    const QString qt = DbModel::qtDriver(c.driver);
    if (!QSqlDatabase::isDriverAvailable(qt)) {
        m_lastError = QStringLiteral("Qt SQL driver %1 is not installed").arg(qt);
        emit resultReady();
        emit statusMessage(QStringLiteral("DB — driver %1 missing (install Qt SQL plugin)").arg(qt));
        return;
    }
    m_connName = QStringLiteral("notch-db-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db = QSqlDatabase::addDatabase(qt, m_connName);
    if (c.driver == QLatin1String("sqlite")) {
        const QString path = expandTilde(c.sqlitePath);
        if (path.isEmpty()) {
            m_lastError = QStringLiteral("Pick a SQLite file path");
            QSqlDatabase::removeDatabase(m_connName);
            m_connName.clear();
            emit resultReady();
            return;
        }
        db.setDatabaseName(path);
    } else {
        db.setHostName(c.host);
        db.setPort(c.port > 0 ? c.port : DbModel::defaultPort(c.driver));
        db.setUserName(c.user);
        // empty database name is allowed for MySQL (server-level connection)
        if (!c.database.isEmpty())
            db.setDatabaseName(c.database);
        const QString pw = passwordFor(c.id);
        if (!pw.isEmpty())
            db.setPassword(pw);
    }
    setBusy(true);
    if (!db.open()) {
        m_lastError = db.lastError().text().trimmed();
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("Could not open database");
        const QString failedName = m_connName;
        db = QSqlDatabase();
        m_connName.clear();
        QSqlDatabase::removeDatabase(failedName);
        setBusy(false);
        emit resultReady();
        emit statusMessage(QStringLiteral("DB — ") + m_lastError);
        return;
    }
    m_connected = true;
    m_activeId = id;
    m_lastError.clear();
    setBusy(false);
    emit connectedChanged();
    emit statusMessage(QStringLiteral("DB — connected to %1").arg(c.name));
}

QVariantList DatabaseManager::schemas()
{
    QVariantList out;
    if (!m_connected)
        return out;
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    const auto *cp = find(m_activeId);
    const QString drv = cp ? cp->driver : QStringLiteral("sqlite");
    if (drv == QLatin1String("sqlite")) {
        out << QVariantMap{{QStringLiteral("name"), QStringLiteral("main")}, {QStringLiteral("driver"), drv}};
        return out;
    }
    QSqlQuery q(db);
    if (drv == QLatin1String("mysql")) {
        if (q.exec(QStringLiteral("SHOW DATABASES"))) {
            while (q.next())
                out << QVariantMap{{QStringLiteral("name"), q.value(0).toString()}, {QStringLiteral("driver"), drv}};
        }
    } else {
        if (q.exec(QStringLiteral("SELECT schema_name FROM information_schema.schemata ORDER BY schema_name"))) {
            while (q.next()) {
                const QString n = q.value(0).toString();
                if (n == QLatin1String("information_schema") || n == QLatin1String("pg_catalog") || n == QLatin1String("pg_toast"))
                    continue;
                out << QVariantMap{{QStringLiteral("name"), n}, {QStringLiteral("driver"), drv}};
            }
        }
    }
    return out;
}

QVariantList DatabaseManager::tables(const QString &schema)
{
    QVariantList out;
    if (!m_connected)
        return out;
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    const auto *cp = find(m_activeId);
    const QString drv = cp ? cp->driver : QStringLiteral("sqlite");
    QSqlQuery q(db);
    if (drv == QLatin1String("sqlite")) {
        if (q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name"))) {
            while (q.next())
                out << QVariantMap{{QStringLiteral("name"), q.value(0).toString()}, {QStringLiteral("schema"), QStringLiteral("main")}};
        }
        return out;
    }
    const QString schemaName = schema.isEmpty() ? db.databaseName() : schema;
    if (schemaName.isEmpty())
        return out; // no database selected yet — pick one from the list first
    // catalog query with a bound schema name
    const QString sql = QStringLiteral(
                            "SELECT table_name FROM information_schema.tables WHERE table_schema = ? AND table_type = 'BASE TABLE' "
                            "ORDER BY table_name");
    q.prepare(sql);
    q.addBindValue(schemaName);
    if (q.exec()) {
        while (q.next())
            out << QVariantMap{{QStringLiteral("name"), q.value(0).toString()}, {QStringLiteral("schema"), schemaName}};
    } else {
        m_lastError = q.lastError().text();
        emit resultReady();
    }
    return out;
}

bool DatabaseManager::useDatabase(const QString &schema)
{
    if (!m_connected || schema.isEmpty())
        return false;
    const auto *cp = find(m_activeId);
    const QString drv = cp ? cp->driver : QStringLiteral("sqlite");
    if (drv == QLatin1String("mysql")) {
        QSqlQuery q(QSqlDatabase::database(m_connName));
        // USE `schema` — schema comes from SHOW DATABASES (our own list), not free user SQL
        if (!q.exec(QStringLiteral("USE `%1`").arg(schema))) {
            m_lastError = q.lastError().text();
            emit resultReady();
            return false;
        }
    }
    // postgres & sqlite: tables()/browseTable take the schema explicitly
    emit statusMessage(QStringLiteral("DB — using %1").arg(schema));
    return true;
}

void DatabaseManager::browseTable(const QString &schema, const QString &table, int limit, int offset)
{
    if (!m_connected) {
        m_lastError = QStringLiteral("Not connected");
        m_last = {};
        emit resultReady();
        return;
    }
    const auto *cp = find(m_activeId);
    const QString drv = cp ? cp->driver : QStringLiteral("sqlite");
    const QString qTable = DbModel::quoteIdent(table, drv);
    QString sql = QStringLiteral("SELECT * FROM %1").arg(qTable);
    if (drv == QLatin1String("mysql"))
        sql += QStringLiteral(" LIMIT %1 OFFSET %2").arg(qBound(1, limit, 500)).arg(qMax(0, offset));
    else if (drv == QLatin1String("postgres"))
        sql += QStringLiteral(" LIMIT %1 OFFSET %2").arg(qBound(1, limit, 500)).arg(qMax(0, offset));
    else
        sql += QStringLiteral(" LIMIT %1 OFFSET %2").arg(qBound(1, limit, 500)).arg(qMax(0, offset));

    setBusy(true);
    QElapsedTimer t;
    t.start();
    QSqlQuery q(QSqlDatabase::database(m_connName));
    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        m_last = QVariantMap{{QStringLiteral("columns"), QVariantList()},
                             {QStringLiteral("rows"), QVariantList()},
                             {QStringLiteral("rowCount"), 0},
                             {QStringLiteral("error"), m_lastError},
                             {QStringLiteral("durationMs"), t.elapsed()},
                             {QStringLiteral("affected"), 0},
                             {QStringLiteral("sql"), sql}};
        setBusy(false);
        emit resultReady();
        return;
    }
    QVariantList cols;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i)
        cols << rec.fieldName(i);
    QVariantList rows;
    while (q.next()) {
        QVariantMap row;
        for (int i = 0; i < rec.count(); ++i)
            row.insert(rec.fieldName(i), q.value(i));
        rows << row;
    }
    m_lastError.clear();
    m_last = QVariantMap{{QStringLiteral("columns"), cols},
                         {QStringLiteral("rows"), rows},
                         {QStringLiteral("rowCount"), rows.size()},
                         {QStringLiteral("error"), QString()},
                         {QStringLiteral("durationMs"), t.elapsed()},
                         {QStringLiteral("affected"), 0},
                         {QStringLiteral("sql"), sql}};
    setBusy(false);
    emit resultReady();
}

void DatabaseManager::execute(const QString &sql)
{
    if (!m_connected) {
        m_lastError = QStringLiteral("Not connected");
        m_last = {};
        emit resultReady();
        return;
    }
    setBusy(true);
    QElapsedTimer t;
    t.start();
    QSqlQuery q(QSqlDatabase::database(m_connName));
    // Free-form SQL is intentional (phpMyAdmin-like); only the active, user-saved connection is used.
    const bool ok = q.exec(sql);
    if (!ok) {
        m_lastError = q.lastError().text();
        m_last = QVariantMap{{QStringLiteral("columns"), QVariantList()},
                             {QStringLiteral("rows"), QVariantList()},
                             {QStringLiteral("rowCount"), 0},
                             {QStringLiteral("error"), m_lastError},
                             {QStringLiteral("durationMs"), t.elapsed()},
                             {QStringLiteral("affected"), 0},
                             {QStringLiteral("sql"), sql}};
        setBusy(false);
        emit resultReady();
        return;
    }
    QVariantList cols;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i)
        cols << rec.fieldName(i);
    QVariantList rows;
    if (rec.count() > 0) {
        while (q.next()) {
            QVariantMap row;
            for (int i = 0; i < rec.count(); ++i)
                row.insert(rec.fieldName(i), q.value(i));
            rows << row;
        }
    }
    m_lastError.clear();
    m_last = QVariantMap{{QStringLiteral("columns"), cols},
                         {QStringLiteral("rows"), rows},
                         {QStringLiteral("rowCount"), rows.size()},
                         {QStringLiteral("error"), QString()},
                         {QStringLiteral("durationMs"), t.elapsed()},
                         {QStringLiteral("affected"), q.numRowsAffected()},
                         {QStringLiteral("sql"), sql}};
    setBusy(false);
    emit resultReady();
    emit statusMessage(QStringLiteral("DB — %1 row(s) in %2 ms").arg(rows.size()).arg(t.elapsed()));
}

void DatabaseManager::disconnectDb()
{
    closeActive();
    m_last = {};
    m_lastError.clear();
    emit resultReady();
    emit statusMessage(QStringLiteral("DB — disconnected"));
}
