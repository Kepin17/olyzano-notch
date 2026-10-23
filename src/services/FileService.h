#pragma once

#include <QList>
#include <QString>
#include <QUrl>

// Filesystem operations on managed storage. Thread-safe (no shared state);
// copyInto is meant to run on a worker thread.
namespace FileService {

struct CopyResult {
    bool ok = false;
    qint64 bytes = 0;
    QString error;
};

inline const QString TmpName = QStringLiteral(".notch-tmp");

// Empty string when the path is a readable regular file, else a user-facing reason.
QString validateSource(const QString &path);

// Streams src into itemDir/.notch-tmp, fsyncs, then atomically renames to itemDir/name.
// On failure nothing is left behind.
CopyResult copyInto(const QString &src, const QString &itemDir, const QString &name);

// A name usable as one path component: no '/', no NUL, no leading dots, never empty.
QString safeFileName(const QString &name, const QString &fallback = QStringLiteral("file"));

struct FolderEntry {
    QString path;
    QString category; // name of the file's immediate parent folder
};
// Regular files under root (recursive, hidden files/folders skipped, symlinked folders not
// followed), sorted by path. Stops after maxFiles and sets *truncated.
QList<FolderEntry> expandFolder(const QString &root, int maxFiles, bool *truncated = nullptr);

// File name for a download: last URL path segment, or "download", plus an extension
// derived from mimeType when the name has none.
QString downloadName(const QUrl &url, const QString &mimeType);

// First <img src> in dropped HTML (browsers put it there when the image is wrapped in a link),
// resolved against base. Invalid QUrl when there is none or it isn't http(s)/data.
QUrl imageFromHtml(const QString &html, const QUrl &base);

// Contents of a freedesktop Type=Link entry that opens url in the browser.
QByteArray linkFile(const QUrl &url);

// Atomically writes bytes to itemDir/name (QSaveFile), creating itemDir.
bool writeFile(const QString &itemDir, const QString &name, const QByteArray &bytes);

// Extension for pasted text: from the editor's language when known (VS Code puts it on the
// clipboard), "json" for valid JSON objects/arrays, else "txt".
QString textExtension(const QString &text, const QString &editorLanguage = {});

// A name not yet taken in dir: "name.ext", else "name (1).ext", "name (2).ext", …
QString uniqueName(const QString &dir, const QString &name);

// Browsers' in-progress download names (.crdownload, .part, …): not a finished file yet.
bool isPartialDownload(const QString &name);

// storedName must be "<uuid>/<file name>". Anything else is refused.
bool isValidStoredName(const QString &storedName);
QString storedPath(const QString &filesDir, const QString &storedName);

// Deletes one managed item (file, leftover tmp, its uuid dir). Never follows symlinks,
// never recurses, never touches anything outside filesDir. True when the file is gone.
bool removeManaged(const QString &filesDir, const QString &storedName);

}
