#pragma once

#include <QDateTime>
#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(lcNotch)

constexpr int DefaultRetentionDays = 7;

namespace Status {
inline const QString Copying = QStringLiteral("copying");
inline const QString Available = QStringLiteral("available");
inline const QString Error = QStringLiteral("error");
inline const QString PendingCleanup = QStringLiteral("pending_cleanup");
}

struct FileItem {
    QString id;           // UUID, the item's identity
    QString originalName; // display name, also the on-disk file name inside <id>/
    QString storedName;   // "<id>/<originalName>", relative to the files dir
    QString mimeType;
    qint64 sizeBytes = 0;
    QString sourcePath;   // informational only, never opened again
    QDateTime addedAt;    // UTC
    QString status;
    QString categoryId;   // empty = uncategorized
    bool pinned = false;  // "kept": never auto-deleted
};
