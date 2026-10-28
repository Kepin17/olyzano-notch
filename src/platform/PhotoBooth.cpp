#include "PhotoBooth.h"

#include "app/NotchController.h"
#include "platform/PhotoModel.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>

PhotoBooth::PhotoBooth(NotchController *notch, QObject *parent) : QObject(parent), m_notch(notch) {}

int PhotoBooth::shots(const QString &templateName) const { return PhotoModel::shots(PhotoModel::templateOf(templateName)); }

QString PhotoBooth::folder()
{
    if (m_folder.isEmpty())
        m_folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/notch-booth-") + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(m_folder);
    return m_folder;
}

QString PhotoBooth::compose(const QString &templateName, const QString &filterName, bool mirror, const QStringList &frames, const QColor &accent)
{
    QList<QImage> images;
    for (const QString &f : frames)
        images << QImage(QString(f).remove(QStringLiteral("file://")));
    const PhotoModel::Template t = PhotoModel::templateOf(templateName);
    if (images.isEmpty() || images.size() < PhotoModel::shots(t))
        return {};
    const QImage out = PhotoModel::compose(t, PhotoModel::filterOf(filterName), mirror, images, accent, QDateTime::currentDateTime());
    const QString path = folder() + QStringLiteral("/booth-%1.jpg").arg(QDateTime::currentMSecsSinceEpoch());
    return out.save(path, "JPEG", 92) ? path : QString();
}

QString PhotoBooth::keep(const QString &path)
{
    QFile f(QString(path).remove(QStringLiteral("file://")));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QString name = QStringLiteral("Photo booth %1.jpg").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss")));
    return m_notch->keepBytesId(name, QStringLiteral("image/jpeg"), f.readAll(), tr("Camera"));
}

void PhotoBooth::cleanup()
{
    if (!m_folder.isEmpty())
        QDir(m_folder).removeRecursively();
}
