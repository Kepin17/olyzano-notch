#pragma once

#include <QList>
#include <QString>

#include <atomic>

// Writes a .zip without any external program (Windows has no `zip`, and linking files into a staging
// folder needs admin rights there). Files are stored, not compressed; names are UTF-8. Each file is read
// once (the checksum goes after the data, in a data descriptor), so a 100 GB folder is not read twice and
// memory stays flat. Zip64 is used where an archive passes 4 GiB or 65535 files.
namespace ZipWriter {

struct Entry {
    QString path; // the file on disk
    QString name; // its name inside the archive (unique; "/" separates folders)
};

// Empty string on success, else a user-facing reason. Blocking: run it on a worker thread.
// `done` (optional) is set to the bytes written so far, for a progress bar; `cancel` (optional) ends it early with the reason
// "cancelled", and the unfinished archive is deleted.
QString write(const QString &outPath, const QList<Entry> &files, std::atomic<qint64> *done = nullptr, const std::atomic_bool *cancel = nullptr);

}
