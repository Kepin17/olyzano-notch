#pragma once

#include <QList>
#include <QString>

// Writes a .zip without any external program (Windows has no `zip`, and linking files into a staging
// folder needs admin rights there). Files are stored, not compressed; names are UTF-8. Each file is read
// twice (checksum first, then the copy), so memory stays flat for big files. No Zip64: 4 GiB per archive.
namespace ZipWriter {

struct Entry {
    QString path; // the file on disk
    QString name; // its name inside the archive (unique; "/" separates folders)
};

// Empty string on success, else a user-facing reason. Blocking: run it on a worker thread.
QString write(const QString &outPath, const QList<Entry> &files);

}
