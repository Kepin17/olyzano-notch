#include "OcrPacks.h"

#include "platform/ProcessRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

namespace OcrPacks {

namespace {
QMutex lock;
QString cfgPacks, cfgWork;
QStringList cfgWanted;
}

QString systemDir()
{
    const auto res = ProcessRunner::run(QStringLiteral("tesseract"), {QStringLiteral("--list-langs")}, 10000);
    if (!res.ok)
        return {};
    static const QRegularExpression re(QStringLiteral("in \\\"([^\\\"]+)\\\""));
    const auto m = re.match(QString::fromUtf8(res.out + res.err));
    return m.hasMatch() ? QDir::fromNativeSeparators(m.captured(1)) : QString();
}

QStringList systemLanguages()
{
    const auto res = ProcessRunner::run(QStringLiteral("tesseract"), {QStringLiteral("--list-langs")}, 10000);
    QStringList out;
    if (res.ok)
        for (const QString &l : QString::fromUtf8(res.out).split(u'\n', Qt::SkipEmptyParts))
            if (!l.startsWith(QLatin1String("List of")) && l.trimmed() != QLatin1String("osd"))
                out << l.trimmed();
    return out;
}

namespace {
// a link to the file, or a copy where links are not allowed (Windows without the right to make them)
bool bring(const QString &from, const QString &to)
{
    if (QFile::exists(to)) {
        if (QFileInfo(to).isSymLink() ? QFileInfo(to).symLinkTarget() == from : QFileInfo(to).size() == QFileInfo(from).size())
            return true;
        QFile::remove(to);
    }
    return QFile::link(from, to) && QFileInfo(to).exists() ? true : QFile::copy(from, to);
}
}

Setup prepare(const QString &packsDir, const QString &sysDir, const QStringList &wanted, const QString &workDir)
{
    Setup s;
    const QString dir = workDir + QStringLiteral("/tessdata");
    QDir().mkpath(dir);
    QStringList have;
    for (const QString &base : {packsDir, sysDir}) { // Notch's own downloads first, then the system's
        if (base.isEmpty())
            continue;
        for (const QFileInfo &fi : QDir(base).entryInfoList({QStringLiteral("*.traineddata")}, QDir::Files)) {
            const QString code = fi.completeBaseName();
            if (!have.contains(code) && bring(fi.absoluteFilePath(), dir + u'/' + fi.fileName()))
                have << code;
        }
    }
    QStringList use;
    for (const QString &c : wanted) {
        if (have.contains(c)) {
            if (!use.contains(c))
                use << c;
        } else if (!s.missing.contains(c)) {
            s.missing << c;
        }
    }
    if (have.contains(QStringLiteral("eng")) && !use.contains(QStringLiteral("eng")))
        use << QStringLiteral("eng");
    if (use.isEmpty() && !have.isEmpty())
        use << have.first();
    s.tessdataDir = have.isEmpty() ? QString() : dir;
    s.languages = use.join(u'+');
    return s;
}

void configure(const QString &packsDir, const QString &workDir)
{
    QMutexLocker l(&lock);
    cfgPacks = packsDir;
    cfgWork = workDir;
}

void setWanted(const QStringList &tessCodes)
{
    QMutexLocker l(&lock);
    cfgWanted = tessCodes;
}

QStringList wanted()
{
    QMutexLocker l(&lock);
    return cfgWanted;
}

Setup current(const QString &first)
{
    QString packs, work;
    QStringList want;
    {
        QMutexLocker l(&lock);
        packs = cfgPacks;
        work = cfgWork;
        want = cfgWanted;
    }
    if (!first.isEmpty()) {
        want.removeAll(first);
        want.prepend(first);
    }
    if (want.isEmpty())
        want << QStringLiteral("eng");
    if (work.isEmpty())
        return {{}, QStringLiteral("eng"), {}};
    return prepare(packs, systemDir(), want, work);
}

} // namespace OcrPacks
