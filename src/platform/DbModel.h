#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

// Pure data for the database browser (like SshModel): connection rows + driver ↔ Qt name mapping.
namespace DbModel {

struct Connection {
    QString id, name;
    QString driver; // sqlite | mysql | postgres  (UI-facing; maps to QSQLITE / QMYSQL / QPSQL)
    QString host, user, database;
    int port = 0;
    QString sqlitePath;
    bool savePassword = false;
};

inline QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// UI driver id → QSqlDatabase name.
inline QString qtDriver(const QString &driver)
{
    if (driver == QLatin1String("mysql"))
        return QStringLiteral("QMYSQL");
    if (driver == QLatin1String("postgres") || driver == QLatin1String("postgresql"))
        return QStringLiteral("QPSQL");
    return QStringLiteral("QSQLITE");
}

inline QString uiDriver(const QString &qtName)
{
    if (qtName == QLatin1String("QMYSQL"))
        return QStringLiteral("mysql");
    if (qtName == QLatin1String("QPSQL"))
        return QStringLiteral("postgres");
    return QStringLiteral("sqlite");
}

inline int defaultPort(const QString &driver)
{
    if (driver == QLatin1String("mysql"))
        return 3306;
    if (driver == QLatin1String("postgres"))
        return 5432;
    return 0;
}

inline QJsonObject toJson(const Connection &c)
{
    return QJsonObject{{QStringLiteral("id"), c.id},
                       {QStringLiteral("name"), c.name},
                       {QStringLiteral("driver"), c.driver},
                       {QStringLiteral("host"), c.host},
                       {QStringLiteral("port"), c.port},
                       {QStringLiteral("user"), c.user},
                       {QStringLiteral("database"), c.database},
                       {QStringLiteral("sqlitePath"), c.sqlitePath},
                       {QStringLiteral("savePassword"), c.savePassword}};
}

inline Connection fromJson(const QJsonObject &o)
{
    Connection c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.name = o.value(QStringLiteral("name")).toString();
    c.driver = o.value(QStringLiteral("driver")).toString(QStringLiteral("sqlite"));
    c.host = o.value(QStringLiteral("host")).toString();
    c.port = o.value(QStringLiteral("port")).toInt(defaultPort(c.driver));
    c.user = o.value(QStringLiteral("user")).toString();
    c.database = o.value(QStringLiteral("database")).toString();
    c.sqlitePath = o.value(QStringLiteral("sqlitePath")).toString();
    c.savePassword = o.value(QStringLiteral("savePassword")).toBool();
    return c;
}

inline QByteArray listToJson(const QList<Connection> &list)
{
    QJsonArray a;
    for (const Connection &c : list)
        a << toJson(c);
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("connections"), a}}).toJson(QJsonDocument::Indented);
}

inline QList<Connection> listFromJson(const QByteArray &bytes)
{
    QList<Connection> out;
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    for (const QJsonValue &v : root.value(QStringLiteral("connections")).toArray()) {
        Connection c = fromJson(v.toObject());
        if (c.id.isEmpty())
            continue;
        if (c.driver != QLatin1String("mysql") && c.driver != QLatin1String("postgres") && c.driver != QLatin1String("sqlite"))
            c.driver = QStringLiteral("sqlite");
        out.push_back(c);
    }
    return out;
}

inline void applyFields(Connection &c, const QJsonObject &fields)
{
    if (fields.contains(QStringLiteral("name")))
        c.name = fields.value(QStringLiteral("name")).toString().trimmed();
    if (fields.contains(QStringLiteral("driver"))) {
        c.driver = fields.value(QStringLiteral("driver")).toString(QStringLiteral("sqlite"));
        if (fields.contains(QStringLiteral("port")) == false)
            c.port = defaultPort(c.driver);
    }
    if (fields.contains(QStringLiteral("host")))
        c.host = fields.value(QStringLiteral("host")).toString().trimmed();
    if (fields.contains(QStringLiteral("port")))
        c.port = fields.value(QStringLiteral("port")).toInt(defaultPort(c.driver));
    if (fields.contains(QStringLiteral("user")))
        c.user = fields.value(QStringLiteral("user")).toString().trimmed();
    if (fields.contains(QStringLiteral("database")))
        c.database = fields.value(QStringLiteral("database")).toString().trimmed();
    if (fields.contains(QStringLiteral("sqlitePath")))
        c.sqlitePath = fields.value(QStringLiteral("sqlitePath")).toString().trimmed();
    if (fields.contains(QStringLiteral("savePassword")))
        c.savePassword = fields.value(QStringLiteral("savePassword")).toBool();
}

// Quote an identifier for catalog / browse queries (double quotes work on sqlite & postgres; mysql uses backticks).
inline QString quoteIdent(const QString &name, const QString &driver)
{
    if (driver == QLatin1String("mysql"))
        return QLatin1Char('`') + name + QLatin1Char('`');
    return QLatin1Char('"') + name + QLatin1Char('"');
}

} // namespace DbModel
