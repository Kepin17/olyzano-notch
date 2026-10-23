#include "CleanupService.h"

#include "repositories/FileRepository.h"
#include "services/FileService.h"

#include <QFileInfo>

namespace CleanupService {

void recover(FileRepository &repo, const QString &filesDir)
{
    for (const FileItem &item : repo.withStatus(Status::Copying)) {
        qCInfo(lcNotch) << "Discarding interrupted copy" << item.originalName;
        repo.setStatus(item.id, Status::PendingCleanup);
    }
    for (const FileItem &item : repo.withStatus(Status::Available)) {
        if (!QFileInfo::exists(FileService::storedPath(filesDir, item.storedName))) {
            qCWarning(lcNotch) << "Stored file missing for" << item.originalName;
            repo.setStatus(item.id, Status::Error);
        }
    }
}

QStringList purge(FileRepository &repo, const QString &filesDir, const QDateTime &now, int retentionDays,
                  const QSet<QString> &busy)
{
    QStringList removed;
    for (const FileItem &item : repo.expired(now.addDays(-retentionDays))) {
        if (busy.contains(item.id))
            continue;
        if (repo.setStatus(item.id, Status::PendingCleanup))
            removed << item.id; // hidden from the shelf from here on
    }
    for (const FileItem &item : repo.withStatus(Status::PendingCleanup)) {
        if (FileService::removeManaged(filesDir, item.storedName))
            repo.remove(item.id);
        else
            qCWarning(lcNotch) << "Cleanup will retry" << item.storedName;
    }
    return removed;
}

}
