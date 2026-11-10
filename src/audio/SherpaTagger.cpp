#include "AudioEngines.h"

#include "WavFile.h"
#include "platform/ChildLife.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent>

namespace Audio {
namespace {
QString tail(QString text)
{
    text = text.trimmed();
    const QStringList lines = text.split(u'\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.mid(qMax(0, int(lines.size()) - 3)).join(QStringLiteral(" | ")).left(300);
}
QString sherpaId()
{
#ifdef Q_OS_WIN
    return QStringLiteral("sherpa-windows");
#else
    return QStringLiteral("sherpa-linux");
#endif
}
}

// ---- sound tagging (CED, with the sherpa-onnx tagging program) --------------------------------------

bool SherpaTagger::available() const
{
    const QString folder = m_store->folder(sherpaId());
    return !m_store->path(sherpaId()).isEmpty() && !m_store->path(modelId()).isEmpty()
           && QFileInfo::exists(QDir(m_store->folder(modelId())).filePath(QStringLiteral("class_labels_indices.csv"))) && !folder.isEmpty();
}

QString SherpaTagger::tag(const QString &wavPath, const std::atomic_bool *cancel, const std::function<void(double)> &progress, QList<Sound::Clip> *clips)
{
    if (!available())
        return QObject::tr("The sound model is not installed");
    const QString folder = m_store->folder(sherpaId());
#ifdef Q_OS_WIN
    const QString program = QDir(folder).filePath(QStringLiteral("sherpa-onnx-offline-audio-tagging.exe"));
#else
    const QString program = QDir(folder).filePath(QStringLiteral("sherpa-onnx-offline-audio-tagging"));
#endif
    if (!QFileInfo::exists(program))
        return QObject::tr("The sound program is not in the helper that is installed; download it again");
    const QString model = m_store->path(modelId()), labels = QDir(m_store->folder(modelId())).filePath(QStringLiteral("class_labels_indices.csv"));
    const QVector<float> samples = Wav::readFrames(wavPath, 0);
    if (samples.isEmpty())
        return QObject::tr("The audio could not be read");
    constexpr int Rate = 16000;
    const qint64 clipLen = qint64(Sound::ClipSeconds * Rate);
    QTemporaryDir dir;
    if (!dir.isValid())
        return QObject::tr("No place for the clips");
    struct Job { int index; qint64 from, to; };
    QList<Job> jobs;
    for (qint64 at = 0; at < samples.size(); at += clipLen) {
        const qint64 to = qMin<qint64>(samples.size(), at + clipLen);
        if (to - at >= Rate) // a clip shorter than a second says nothing
            jobs.append({int(jobs.size()), at, to});
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString libs = QDir(folder).absoluteFilePath(QStringLiteral("../lib"));
#ifdef Q_OS_WIN
    env.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(folder) + u';' + QDir::toNativeSeparators(libs) + u';' + env.value(QStringLiteral("PATH")));
#else
    env.insert(QStringLiteral("LD_LIBRARY_PATH"), folder + u':' + libs + u':' + env.value(QStringLiteral("LD_LIBRARY_PATH")));
#endif
    QList<QList<Sound::Tag>> answers(jobs.size());
    QString firstError;
    QMutex lock;
    std::atomic_int done{0}, failed{0};
    QThreadPool pool;
    pool.setMaxThreadCount(qBound(1, qMin(4, m_threads()), 4));
    for (const Job &j : std::as_const(jobs)) {
        (void)QtConcurrent::run(&pool, [&, j] {
            if (cancel && cancel->load())
                return;
            QVector<qint16> pcm(int(j.to - j.from));
            for (qint64 i = j.from; i < j.to; ++i)
                pcm[int(i - j.from)] = qint16(std::clamp(samples[int(i)], -1.0f, 1.0f) * 32767.0f);
            const QString file = dir.filePath(QStringLiteral("clip-%1.wav").arg(j.index));
            QString why = Wav::writePcm16(file, pcm, 1, Rate);
            QByteArray output;
            if (why.isEmpty()) {
                QProcess p;
                p.setProcessChannelMode(QProcess::MergedChannels);
                p.setProcessEnvironment(env);
                p.setWorkingDirectory(folder);
                ChildLife::dieWithParent(p);
                p.start(program, {QStringLiteral("--ced-model=") + model, QStringLiteral("--labels=") + labels, QStringLiteral("--top-k=5"), QStringLiteral("--num-threads=1"), file});
                if (!p.waitForStarted(10000)) {
                    why = QObject::tr("The sound program could not be started");
                } else {
                    QElapsedTimer clock;
                    clock.start();
                    while (p.state() != QProcess::NotRunning) {
                        if ((cancel && cancel->load()) || clock.elapsed() > 90000) {
                            p.kill();
                            p.waitForFinished(2000);
                            break;
                        }
                        if (p.waitForReadyRead(200))
                            output += p.readAll();
                    }
                    output += p.readAll();
                    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
                        why = QObject::tr("The sound program failed: %1").arg(tail(QString::fromUtf8(output)));
                }
            }
            QFile::remove(file);
            const QList<Sound::Tag> tags = why.isEmpty() ? Sound::parseTags(QString::fromUtf8(output)) : QList<Sound::Tag>();
            if (why.isEmpty() && tags.isEmpty())
                why = QObject::tr("The sound program gave no answer: %1").arg(tail(QString::fromUtf8(output)));
            QMutexLocker locker(&lock);
            answers[j.index] = tags;
            if (!why.isEmpty()) {
                ++failed;
                if (firstError.isEmpty())
                    firstError = why;
            }
            ++done;
        });
    }
    while (!pool.waitForDone(250)) {
        if (progress)
            progress(jobs.isEmpty() ? 1.0 : double(done.load()) / jobs.size());
    }
    if (cancel && cancel->load())
        return QStringLiteral("cancelled");
    if (jobs.isEmpty() || failed.load() * 2 > jobs.size()) // most of it failed: no answer is better than a made-up one
        return firstError.isEmpty() ? QObject::tr("The sound program found nothing") : firstError;
    clips->clear();
    for (const Job &j : std::as_const(jobs))
        clips->append(Sound::classify(answers[j.index], double(j.from) / Rate, double(j.to) / Rate)); // a clip that failed is "plain"
    return {};
}

} // namespace Audio
