#include "MattingController.h"

#include "NotchController.h"
#include "services/Matting.h"
#include "services/OnnxRuntime.h"
#include "audio/SystemMemory.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QThread>
#include <QUrl>
#include <QtConcurrent>

namespace {

QString localFile(const QString &ref)
{
    const QUrl u(ref);
    return u.isLocalFile() ? u.toLocalFile() : ref;
}
}

MattingController::MattingController(NotchController *notch, AudioController *audio, QObject *parent) : QObject(parent), m_notch(notch), m_audio(audio)
{
    m_pool.setMaxThreadCount(1);
    connect(audio, &AudioController::modelsChanged, this, &MattingController::changed);
}

MattingController::~MattingController()
{
    m_cancel = true;
    m_pool.waitForDone(5000);
}

bool MattingController::supported() const { return OnnxRuntime::supported(); }

// the runtime of its own helper, else the one that came with the speaker program (the same library)
QString MattingController::libraryPath() const
{
    ModelStore *store = m_audio->modelStore();
#ifdef Q_OS_WIN
    const QString own = store->path(QStringLiteral("onnxruntime-windows")), sherpa = QStringLiteral("sherpa-windows"), name = QStringLiteral("onnxruntime.dll");
#else
    const QString own = store->path(QStringLiteral("onnxruntime-linux")), sherpa = QStringLiteral("sherpa-linux"), name = QStringLiteral("libonnxruntime.so");
#endif
    if (!own.isEmpty())
        return own;
    if (!store->path(sherpa).isEmpty()) {
        QDirIterator it(store->folder(sherpa), {name}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext())
            return it.next();
    }
    return {};
}

// the best model that is installed
QString MattingController::model() const
{
    for (const QString &id : {QStringLiteral("matting-isnet"), QStringLiteral("matting-u2netp")})
        if (!m_audio->modelStore()->path(id).isEmpty())
            return id;
    return {};
}

bool MattingController::ready() const { return supported() && !libraryPath().isEmpty() && !model().isEmpty(); }

void MattingController::cancel() { m_cancel = true; }

void MattingController::clearError()
{
    m_error.clear();
    emit changed();
}

void MattingController::cutout(const QString &ref, int featherPx) { start(ref, featherPx, false); }
void MattingController::removeBackground(const QString &ref, int featherPx) { start(ref, featherPx, true); }

bool MattingController::addToShelf(const QString &cutoutFileUrl, const QString &baseName)
{
    QFile f(localFile(cutoutFileUrl));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QString name = (baseName.isEmpty() ? tr("Picture") : baseName) + tr(" (no background).png");
    if (!m_notch->addDocument(name, QStringLiteral("image/png"), f.readAll(), tr("Background removed by the picture model")))
        return false;
    m_notch->announce(tr("Background removed"), name, QStringLiteral("image-x-generic"));
    return true;
}

void MattingController::start(const QString &ref, int featherPx, bool toShelf)
{
    if (m_busy)
        return;
    const auto fail = [this](const QString &why) { m_error = why; emit changed(); emit cutoutReady(QString(), why); };
    if (!supported()) {
        fail(tr("This version of Notch cannot run the background-removal model yet."));
        return;
    }
    const QString library = libraryPath(), modelFile = m_audio->modelStore()->path(model());
    if (library.isEmpty() || modelFile.isEmpty()) {
        fail(tr("Download the background-removal model first: Settings → AI → Removing backgrounds."));
        return;
    }
    QString path, base;
    const QVariantMap info = m_notch->documentInfo(ref);
    if (info.value(QStringLiteral("ok")).toBool()) {
        path = info.value(QStringLiteral("path")).toString();
        base = info.value(QStringLiteral("base")).toString();
    } else {
        path = localFile(ref);
        base = QFileInfo(path).completeBaseName();
    }
    if (!QFileInfo(path).isFile()) {
        fail(tr("The picture cannot be found."));
        return;
    }
    const Matting::Profile profile = Matting::profileFor(model());
    const qint64 free = Audio::availableMemory();
    if (free > 0 && free < profile.memory) {
        fail(tr("There is not enough free memory for this model (it needs about %1).").arg(QLocale().formattedDataSize(profile.memory)));
        return;
    }
    const QString dir = m_notch->scratchPath(false) + QStringLiteral("/preview");
    const QString out = dir + QStringLiteral("/cutout-") + QString::number(++m_serial) + QStringLiteral(".png");
    m_busy = true;
    m_cancel = false;
    m_error.clear();
    m_status = tr("Taking the background off…");
    emit changed();
    (void)QtConcurrent::run(&m_pool, [this, path, base, library, modelFile, profile, out, dir, featherPx, toShelf] {
        QString error;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull())
            error = tr("The picture cannot be read.");
        QImage result;
        if (error.isEmpty()) {
            const Matting::Runner run = OnnxRuntime::makeRunner(library, modelFile, &error);
            if (run) {
                Matting::Options options;
                options.featherPx = featherPx;
                error = Matting::cutout(image, profile, run, options, &result);
            }
        }
        if (error.isEmpty() && m_cancel)
            error = QStringLiteral("cancelled");
        if (error.isEmpty()) {
            QDir().mkpath(dir);
            if (!result.save(out, "PNG"))
                error = tr("The result cannot be saved.");
        }
        QMetaObject::invokeMethod(this, [this, error, out, base, toShelf] {
            m_busy = false;
            m_status.clear();
            if (error == QLatin1String("cancelled")) {
                emit cutoutReady(QString(), QString());
            } else if (!error.isEmpty()) {
                m_error = error;
                emit cutoutReady(QString(), error);
            } else {
                const QString url = QUrl::fromLocalFile(out).toString();
                if (toShelf)
                    addToShelf(url, base);
                emit cutoutReady(url, QString());
            }
            emit changed();
        }, Qt::QueuedConnection);
    });
}
