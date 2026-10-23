#include "FileService.h"

#include "models/FileItem.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>

#include <filesystem>
#include <unistd.h>

namespace FileService {

QString validateSource(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return QStringLiteral("file not found");
    if (fi.isDir())
        return QStringLiteral("folders are not supported");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::path(path.toStdString()), ec))
        return QStringLiteral("not a regular file");
    if (!fi.isReadable())
        return QStringLiteral("permission denied");
    return {};
}

static QString writeError(const QFile &f)
{
    return f.error() == QFileDevice::ResourceError ? QStringLiteral("disk full")
                                                   : QStringLiteral("write failed: ") + f.errorString();
}

CopyResult copyInto(const QString &src, const QString &itemDir, const QString &name)
{
    CopyResult r;
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("cannot read source: ") + in.errorString();
        return r;
    }
    if (!QDir().mkpath(itemDir)) {
        r.error = QStringLiteral("cannot create storage directory");
        return r;
    }
    const QString tmp = itemDir + u'/' + TmpName;
    QFile out(tmp);
    auto fail = [&](const QString &msg) {
        out.close();
        QFile::remove(tmp);
        QDir().rmdir(itemDir);
        r.ok = false;
        r.error = msg;
        return r;
    };

    const QStorageInfo storage(itemDir);
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < in.size())
        return fail(QStringLiteral("not enough disk space"));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(QStringLiteral("cannot create file: ") + out.errorString());

    QByteArray buf(1 << 20, Qt::Uninitialized);
    for (;;) {
        const qint64 n = in.read(buf.data(), buf.size());
        if (n < 0)
            return fail(QStringLiteral("read failed: ") + in.errorString());
        if (n == 0)
            break;
        if (out.write(buf.constData(), n) != n)
            return fail(writeError(out));
        r.bytes += n;
    }
    if (!out.flush() || ::fsync(out.handle()) != 0)
        return fail(writeError(out));
    out.close();
    if (QFileInfo(tmp).size() != r.bytes)
        return fail(QStringLiteral("copy verification failed"));
    if (!QFile::rename(tmp, itemDir + u'/' + name))
        return fail(QStringLiteral("cannot finalize copied file"));
    r.ok = true;
    return r;
}

QString safeFileName(const QString &name, const QString &fallback)
{
    QString s = name;
    s.replace(u'/', u'_').remove(QChar(u'\0')).remove(u'\n').remove(u'\r');
    while (s.startsWith(u'.'))
        s.remove(0, 1);
    s = s.trimmed().left(200);
    return s.isEmpty() || s == TmpName ? fallback : s;
}

QList<FolderEntry> expandFolder(const QString &root, int maxFiles, bool *truncated)
{
    QList<FolderEntry> out;
    if (truncated)
        *truncated = false;
    // Without QDir::Hidden the iterator neither lists hidden files nor descends into hidden folders.
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo fi = it.nextFileInfo();
        if (out.size() >= maxFiles) {
            if (truncated)
                *truncated = true;
            break;
        }
        out << FolderEntry{fi.absoluteFilePath(), fi.dir().dirName()};
    }
    std::sort(out.begin(), out.end(), [](const FolderEntry &a, const FolderEntry &b) { return a.path < b.path; });
    return out;
}

QString downloadName(const QUrl &url, const QString &mimeType)
{
    // A data: URL's "path" is its payload, never a name.
    const QString fromUrl = url.scheme() == QLatin1String("data") ? QString() : url.fileName();
    QString name = safeFileName(fromUrl, QStringLiteral("download"));
    const QMimeType mt = QMimeDatabase().mimeTypeForName(mimeType);
    if (QFileInfo(name).suffix().isEmpty() && mt.isValid() && !mt.preferredSuffix().isEmpty())
        name += u'.' + mt.preferredSuffix();
    return name;
}

QUrl imageFromHtml(const QString &html, const QUrl &base)
{
    static const QRegularExpression re(QStringLiteral(R"(<img\b[^>]*?\bsrc\s*=\s*(["'])(.*?)\1)"),
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(html);
    if (!m.hasMatch())
        return {};
    QString src = m.captured(2).trimmed();
    src.replace(QLatin1String("&amp;"), QLatin1String("&"));
    const QUrl url = base.resolved(QUrl(src));
    const QString scheme = url.scheme();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("data") ? url : QUrl();
}

QByteArray linkFile(const QUrl &url)
{
    QString title = url.toDisplayString(QUrl::RemoveScheme).remove(u'\n').remove(u'\r');
    while (title.startsWith(u'/'))
        title.remove(0, 1);
    return QStringLiteral("[Desktop Entry]\nType=Link\nName=%1\nURL=%2\nIcon=text-html\n")
        .arg(title, QString::fromUtf8(url.toEncoded()))
        .toUtf8();
}

bool writeFile(const QString &itemDir, const QString &name, const QByteArray &bytes)
{
    if (!QDir().mkpath(itemDir))
        return false;
    QSaveFile f(itemDir + u'/' + name);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

QString textExtension(const QString &text, const QString &editorLanguage)
{
    static const QHash<QString, QString> byLanguage = {
        {QStringLiteral("c"), QStringLiteral("c")},           {QStringLiteral("cpp"), QStringLiteral("cpp")},
        {QStringLiteral("csharp"), QStringLiteral("cs")},     {QStringLiteral("css"), QStringLiteral("css")},
        {QStringLiteral("go"), QStringLiteral("go")},         {QStringLiteral("html"), QStringLiteral("html")},
        {QStringLiteral("java"), QStringLiteral("java")},     {QStringLiteral("javascript"), QStringLiteral("js")},
        {QStringLiteral("javascriptreact"), QStringLiteral("jsx")}, {QStringLiteral("json"), QStringLiteral("json")},
        {QStringLiteral("kotlin"), QStringLiteral("kt")},     {QStringLiteral("markdown"), QStringLiteral("md")},
        {QStringLiteral("php"), QStringLiteral("php")},       {QStringLiteral("python"), QStringLiteral("py")},
        {QStringLiteral("qml"), QStringLiteral("qml")},       {QStringLiteral("ruby"), QStringLiteral("rb")},
        {QStringLiteral("rust"), QStringLiteral("rs")},       {QStringLiteral("shellscript"), QStringLiteral("sh")},
        {QStringLiteral("sql"), QStringLiteral("sql")},       {QStringLiteral("swift"), QStringLiteral("swift")},
        {QStringLiteral("typescript"), QStringLiteral("ts")}, {QStringLiteral("typescriptreact"), QStringLiteral("tsx")},
        {QStringLiteral("xml"), QStringLiteral("xml")},       {QStringLiteral("yaml"), QStringLiteral("yaml")},
        {QStringLiteral("dart"), QStringLiteral("dart")},     {QStringLiteral("cmake"), QStringLiteral("cmake")},
    };
    if (const QString ext = byLanguage.value(editorLanguage.toLower()); !ext.isEmpty())
        return ext;
    const QString trimmed = text.trimmed();
    if ((trimmed.startsWith(u'{') || trimmed.startsWith(u'[')) && !QJsonDocument::fromJson(trimmed.toUtf8()).isNull())
        return QStringLiteral("json");
    return QStringLiteral("txt");
}

QString uniqueName(const QString &dir, const QString &name)
{
    if (!QFileInfo::exists(dir + u'/' + name))
        return name;
    const QFileInfo fi(name);
    const QString base = fi.completeBaseName(), suffix = fi.suffix();
    for (int i = 1;; ++i) {
        const QString candidate = suffix.isEmpty() ? QStringLiteral("%1 (%2)").arg(base).arg(i)
                                                   : QStringLiteral("%1 (%2).%3").arg(base).arg(i).arg(suffix);
        if (!QFileInfo::exists(dir + u'/' + candidate))
            return candidate;
    }
}

bool isPartialDownload(const QString &name)
{
    static const QStringList partial = {QStringLiteral("crdownload"), QStringLiteral("part"), QStringLiteral("tmp"),
                                        QStringLiteral("download"), QStringLiteral("opdownload"), QStringLiteral("partial")};
    return name.startsWith(u'.') || partial.contains(QFileInfo(name).suffix().toLower());
}

bool isValidStoredName(const QString &storedName)
{
    static const QRegularExpression re(QStringLiteral(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/([^/]+)$"));
    const auto m = re.match(storedName);
    if (!m.hasMatch())
        return false;
    const QString file = m.captured(1);
    return file != QLatin1String(".") && file != QLatin1String("..") && file != TmpName;
}

QString storedPath(const QString &filesDir, const QString &storedName)
{
    return filesDir + u'/' + storedName;
}

bool removeManaged(const QString &filesDir, const QString &storedName)
{
    if (!isValidStoredName(storedName)) {
        qCWarning(lcNotch) << "Refusing to delete invalid stored name" << storedName;
        return false;
    }
    const QString id = storedName.section(u'/', 0, 0);
    const QString dirPath = filesDir + u'/' + id;
    const QFileInfo dir(dirPath);
    if (dir.isSymLink())
        return QFile::remove(dirPath); // unlinks the link itself, never its target
    if (!dir.exists())
        return true;
    if (!dir.isDir() || dir.canonicalPath() != QDir(filesDir).canonicalPath()) {
        qCWarning(lcNotch) << "Refusing to delete outside managed storage:" << dirPath;
        return false;
    }
    for (const QString &name : {storedName.section(u'/', 1), TmpName}) {
        const QString path = dirPath + u'/' + name;
        const QFileInfo fi(path);
        if ((fi.exists() || fi.isSymLink()) && !QFile::remove(path)) {
            qCWarning(lcNotch) << "Failed to delete" << path;
            return false;
        }
    }
    if (!QDir(filesDir).rmdir(id))
        qCWarning(lcNotch) << "Item directory not empty, left in place:" << dirPath;
    return true;
}

}
