#pragma once

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>

// Shared helpers for owner-only config / secrets files. Permission hardening lives here
// so a future fix reaches every call site at once instead of N copies of setPermissions.
namespace SecureFiles {

// Create dir if needed, ensure the file exists, tighten to owner-only, return the full path.
inline QString ownerOnlyPath(const QString &dir, const QString &file)
{
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + file;
    if (!QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.close();
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return path;
}

// Atomically write JSON and re-apply owner-only permissions. No-op on open failure.
inline void writePrivateJson(const QString &path, const QJsonDocument &doc)
{
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(doc.toJson(QJsonDocument::Compact));
        f.commit();
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

// Read a JSON object from disk; returns an empty object when the file is missing or unreadable.
inline QJsonObject readJsonObject(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

} // namespace SecureFiles
