#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QUuid>
#include <QVector>

// Pure data + JSON for the SSH manager (like ApiTesterModel): no sockets, no QObject — so tst_core can
// drive round-trips and host/user validation without the controller.
namespace SshModel {

struct Connection {
    QString id, name;
    QString host, user;
    int port = 22;
    QString auth = QStringLiteral("key"); // key | password
    QString keyPath;                      // when auth == "key" (~ is kept and expanded by the controller)
    QString defaultCommand;
    bool savePassword = false;            // password itself lives only in secrets.json
};

inline QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Hosts: letters, digits, dots, dashes, colons (IPv6), brackets. Users: no spaces or shell metacharacters.
inline bool validHost(const QString &host)
{
    static const QRegularExpression re(QStringLiteral(R"(^[A-Za-z0-9._:\[\]-]+$)"));
    return re.match(host.trimmed()).hasMatch() && !host.trimmed().isEmpty();
}

inline bool validUser(const QString &user)
{
    static const QRegularExpression re(QStringLiteral(R"(^[A-Za-z0-9._-]+$)"));
    return re.match(user.trimmed()).hasMatch() && !user.trimmed().isEmpty();
}

inline bool validPort(int port) { return port > 0 && port < 65536; }

inline QJsonObject toJson(const Connection &c)
{
    return QJsonObject{{QStringLiteral("id"), c.id},
                       {QStringLiteral("name"), c.name},
                       {QStringLiteral("host"), c.host},
                       {QStringLiteral("user"), c.user},
                       {QStringLiteral("port"), c.port},
                       {QStringLiteral("auth"), c.auth},
                       {QStringLiteral("keyPath"), c.keyPath},
                       {QStringLiteral("defaultCommand"), c.defaultCommand},
                       {QStringLiteral("savePassword"), c.savePassword}};
}

inline Connection fromJson(const QJsonObject &o)
{
    Connection c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.name = o.value(QStringLiteral("name")).toString();
    c.host = o.value(QStringLiteral("host")).toString();
    c.user = o.value(QStringLiteral("user")).toString();
    c.port = o.value(QStringLiteral("port")).toInt(22);
    c.auth = o.value(QStringLiteral("auth")).toString(QStringLiteral("key"));
    c.keyPath = o.value(QStringLiteral("keyPath")).toString();
    c.defaultCommand = o.value(QStringLiteral("defaultCommand")).toString();
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
        if (c.host.isEmpty() || c.user.isEmpty())
            continue; // incomplete rows would break argv assembly
        if (!validPort(c.port))
            c.port = 22;
        out.push_back(c);
    }
    return out;
}

// Apply a draft map (from the form) onto a connection; only keys present in the map are touched.
inline void applyFields(Connection &c, const QJsonObject &fields)
{
    if (fields.contains(QStringLiteral("name")))
        c.name = fields.value(QStringLiteral("name")).toString().trimmed();
    if (fields.contains(QStringLiteral("host")))
        c.host = fields.value(QStringLiteral("host")).toString().trimmed();
    if (fields.contains(QStringLiteral("user")))
        c.user = fields.value(QStringLiteral("user")).toString().trimmed();
    if (fields.contains(QStringLiteral("port")))
        c.port = fields.value(QStringLiteral("port")).toInt(22);
    if (fields.contains(QStringLiteral("auth")))
        c.auth = fields.value(QStringLiteral("auth")).toString(QStringLiteral("key"));
    if (fields.contains(QStringLiteral("keyPath")))
        c.keyPath = fields.value(QStringLiteral("keyPath")).toString().trimmed();
    if (fields.contains(QStringLiteral("defaultCommand")))
        c.defaultCommand = fields.value(QStringLiteral("defaultCommand")).toString();
    if (fields.contains(QStringLiteral("savePassword")))
        c.savePassword = fields.value(QStringLiteral("savePassword")).toBool();
}

} // namespace SshModel
