#pragma once

#include "platform/NotchPaths.h"
#include "platform/SecureFiles.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// secrets.json under the Dev hub root: { "<section>": { "<id>": "<password>" } }.
// Owner-only file; passwords never go on argv. One store for SSH and Database passwords
// so the security rules live in a single place.
namespace SecretsStore {

// ~/.config/notch/dev/secrets.json (created owner-only on first access)
inline QString secretsPath()
{
    return SecureFiles::ownerOnlyPath(NotchPaths::devDir(), QStringLiteral("secrets.json"));
}

inline QString get(const QString &section, const QString &id)
{
    return SecureFiles::readJsonObject(secretsPath()).value(section).toObject().value(id).toString();
}

// Empty password removes the entry. Atomic write + owner-only permissions.
inline void set(const QString &section, const QString &id, const QString &password)
{
    QJsonObject root = SecureFiles::readJsonObject(secretsPath());
    QJsonObject sec = root.value(section).toObject();
    if (password.isEmpty())
        sec.remove(id);
    else
        sec.insert(id, password);
    root.insert(section, sec);
    SecureFiles::writePrivateJson(secretsPath(), QJsonDocument(root));
}

} // namespace SecretsStore
