#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// Where Dev → Agent keeps its data. One small `dev/agent.json` (providers, folders, settings, the order of the tasks) and one file
// per task under `dev/sessions/`, so a change to one task writes that task only, not the whole history. An `agent.json` from
// before (all tasks inside it) is split on the first start and kept once as `agent.json.v1.bak`. No Qt Quick, no QObject.
namespace AgentStore {

constexpr qint64 MaxTaskFile = 32 * 1024 * 1024;     // a task file larger than this is left alone (and not loaded)
constexpr qint64 MaxLegacyFile = 256 * 1024 * 1024;  // so is an old all-in-one file
constexpr int MaxTasks = 500;

struct Loaded {
    QJsonObject settings;          // everything of agent.json except the tasks
    QList<QJsonObject> sessions;   // the tasks, in order, as JSON (the caller reads them with sessionFromJson)
    bool migrated = false;         // came from the old all-in-one file
};

QString settingsPath();
QString sessionPath(const QString &id);   // empty for an id that is not a plain name

Loaded load();
void saveSettings(QJsonObject settings, const QStringList &order);   // atomic, owner-only
bool saveSession(const QJsonObject &session);                         // atomic, owner-only; false for an unsafe id
QJsonObject readSession(const QString &id);   // one task again, empty when its file is gone or too large
void removeSession(const QString &id);

} // namespace AgentStore
