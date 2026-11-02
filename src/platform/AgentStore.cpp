#include "AgentStore.h"

#include "platform/CodingAgentModel.h"
#include "platform/NotchPaths.h"
#include "platform/SecureFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace AgentStore {
namespace {
QString sessionsDir() { return NotchPaths::devDir() + QStringLiteral("/sessions"); }

QJsonObject readObject(const QString &path, qint64 limit)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > limit) return {};
    return SecureFiles::readJsonObject(path);
}
}

QString settingsPath() { return NotchPaths::devDir() + QStringLiteral("/agent.json"); }
QString sessionPath(const QString &id) { return CodingAgentModel::isSafeId(id) ? sessionsDir() + QLatin1Char('/') + id + QStringLiteral(".json") : QString(); }

bool saveSession(const QJsonObject &session)
{
    const QString path = sessionPath(session.value(QStringLiteral("id")).toString());
    if (path.isEmpty()) return false;
    QDir().mkpath(sessionsDir());
    QFile::setPermissions(sessionsDir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(session).toJson(QJsonDocument::Compact));
    if (!f.commit()) return false;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QJsonObject readSession(const QString &id)
{
    const QString path = sessionPath(id);
    return path.isEmpty() ? QJsonObject() : readObject(path, MaxTaskFile);
}

void removeSession(const QString &id)
{
    const QString path = sessionPath(id);
    if (!path.isEmpty()) QFile::remove(path);
}

void saveSettings(QJsonObject settings, const QStringList &order)
{
    QJsonArray ids;
    for (const QString &id : order) ids.append(id);
    settings.remove(QStringLiteral("sessions"));
    settings.insert(QStringLiteral("version"), 2);
    settings.insert(QStringLiteral("sessionOrder"), ids);
    SecureFiles::ownerOnlyPath(NotchPaths::devDir(), QStringLiteral("agent.json"));
    SecureFiles::writePrivateJson(settingsPath(), QJsonDocument(settings));
}

Loaded load()
{
    Loaded out;
    QJsonObject root = readObject(settingsPath(), MaxLegacyFile);
    if (root.contains(QStringLiteral("sessions"))) { // the old all-in-one file: split it
        const QJsonArray all = root.value(QStringLiteral("sessions")).toArray();
        QStringList order;
        const QString backup = settingsPath() + QStringLiteral(".v1.bak");
        if (!QFile::exists(backup) && QFile::copy(settingsPath(), backup))
            QFile::setPermissions(backup, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        for (const QJsonValue &v : all) {
            if (out.sessions.size() >= MaxTasks) break;
            QJsonObject s = v.toObject();
            QString id = s.value(QStringLiteral("id")).toString();
            if (!CodingAgentModel::isSafeId(id)) { // the id of a task becomes a file name: a plain one is made up
                if (root.value(QStringLiteral("sessionId")).toString() == id) root.insert(QStringLiteral("sessionId"), QString());
                id = CodingAgentModel::newId();
                s.insert(QStringLiteral("id"), id);
            }
            if (!saveSession(s)) continue;
            order.append(id);
            out.sessions.append(s);
        }
        out.migrated = true;
        out.settings = root;
        out.settings.remove(QStringLiteral("sessions"));
        saveSettings(out.settings, order); // only now: until it is written, the old file still has everything and the split can run again
        return out;
    }
    out.settings = root;
    for (const QJsonValue &v : root.value(QStringLiteral("sessionOrder")).toArray()) {
        if (out.sessions.size() >= MaxTasks) break;
        const QString path = sessionPath(v.toString());
        if (path.isEmpty()) continue;
        const QJsonObject s = readObject(path, MaxTaskFile);
        if (!s.isEmpty()) out.sessions.append(s);
    }
    return out;
}

} // namespace AgentStore
