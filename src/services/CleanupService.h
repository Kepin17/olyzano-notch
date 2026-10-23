#pragma once

#include <QDateTime>
#include <QSet>
#include <QStringList>

class FileRepository;

// Deletion always goes: mark pending_cleanup -> delete file -> delete row.
// A crash at any point leaves a pending_cleanup row that the next purge finishes.
namespace CleanupService {

// Startup only, before any copy starts: drop interrupted copies, flag missing files.
void recover(FileRepository &repo, const QString &filesDir);

// Finishes pending deletions and expires items added retentionDays or more ago.
// Items in `busy` (e.g. being dragged) are skipped. Returns ids removed from the shelf.
QStringList purge(FileRepository &repo, const QString &filesDir, const QDateTime &now, int retentionDays,
                  const QSet<QString> &busy = {});

}
