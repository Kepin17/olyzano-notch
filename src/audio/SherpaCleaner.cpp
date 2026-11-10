#include "AudioEngines.h"

#include "SherpaRun.h"
#include "WavFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent>

namespace Audio {
namespace {
QString sherpaId()
{
#ifdef Q_OS_WIN
    return QStringLiteral("sherpa-windows");
#else
    return QStringLiteral("sherpa-linux");
#endif
}
}

bool SherpaCleaner::helperReady() const
{
    return !m_store->path(sherpaId()).isEmpty();
}

bool SherpaCleaner::available(const QString &mode) const
{
    if (!helperReady())
        return false;
    if (mode == Clean::Denoise)
        return !m_store->path(denoiseId()).isEmpty();
    if (mode == Clean::Vocals) {
        const QString vocals = m_store->path(vocalsId());
        return !vocals.isEmpty() && QFileInfo::exists(QDir(m_store->folder(vocalsId())).filePath(QStringLiteral("accompaniment.fp16.onnx")));
    }
    return false;
}

QString SherpaCleaner::clean(const QString &wavIn, const QString &wavOut, const QString &mode, const std::atomic_bool *cancel, const std::function<void(double)> &progress)
{
    if (!available(mode))
        return QObject::tr("The model to clean up the sound is not installed");
    const QString folder = m_store->folder(sherpaId());
    const Wav::Info info = Wav::readInfo(wavIn);
    const double seconds = info.valid() ? info.seconds() : 0;
    const qint64 limit = qint64(20 * 60 * 1000 + seconds * 1000 * 2); // a generous limit: twice as long as the recording, and twenty minutes more
    const int threads = qBound(1, m_threads ? m_threads() : 2, 8);
    QTemporaryDir dir;
    if (!dir.isValid())
        return QObject::tr("No place for the cleaned audio");
    const auto failed = [](const SherpaRun::Result &r, const QString &what) {
        if (r.cancelled)
            return QStringLiteral("cancelled");
        if (!r.started)
            return QObject::tr("%1 could not be started").arg(what);
        if (r.timedOut)
            return QObject::tr("%1 took too long").arg(what);
        return QObject::tr("%1 failed: %2").arg(what, SherpaRun::tail(r.output));
    };
    if (progress)
        progress(0.02);
    if (mode == Clean::Denoise) {
        const QString tmp = dir.filePath(QStringLiteral("denoised.wav"));
        const SherpaRun::Result r = SherpaRun::run(SherpaRun::program(folder, QStringLiteral("sherpa-onnx-offline-denoiser")),
                                                   {QStringLiteral("--speech-denoiser-gtcrn-model=") + m_store->path(denoiseId()), QStringLiteral("--num-threads=%1").arg(threads),
                                                    QStringLiteral("--input-wav=") + wavIn, QStringLiteral("--output-wav=") + tmp},
                                                   folder, cancel, limit);
        if (!r.ok())
            return failed(r, QObject::tr("The noise program"));
        // (its answer can be at another rate: ffmpeg brings it to 16 kHz mono)
        FfmpegPreprocessor pre;
        return pre.convert(tmp, wavOut, {}, cancel);
    }
    // vocals: the recording as the separation model reads it (44.1 kHz stereo), the voice taken out, brought back to 16 kHz mono
    const QString stereo = dir.filePath(QStringLiteral("in44.wav")), vocals = dir.filePath(QStringLiteral("vocals.wav")), rest = dir.filePath(QStringLiteral("rest.wav"));
    const QString ffmpeg = FfmpegPreprocessor::program();
    if (ffmpeg.isEmpty())
        return QObject::tr("ffmpeg is needed to separate the voice from the music");
    const SherpaRun::Result up = SherpaRun::run(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                                                         QStringLiteral("-i"), wavIn, QStringLiteral("-ar"), QStringLiteral("44100"), QStringLiteral("-ac"), QStringLiteral("2"),
                                                         QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), stereo},
                                                {}, cancel, 10 * 60 * 1000);
    if (!up.ok())
        return failed(up, QStringLiteral("ffmpeg"));
    if (progress)
        progress(0.1);
    const QString model = m_store->folder(vocalsId());
    const SherpaRun::Result r = SherpaRun::run(SherpaRun::program(folder, QStringLiteral("sherpa-onnx-offline-source-separation")),
                                               {QStringLiteral("--spleeter-vocals=") + QDir(model).filePath(QStringLiteral("vocals.fp16.onnx")),
                                                QStringLiteral("--spleeter-accompaniment=") + QDir(model).filePath(QStringLiteral("accompaniment.fp16.onnx")),
                                                QStringLiteral("--num-threads=%1").arg(threads), QStringLiteral("--input-wav=") + stereo,
                                                QStringLiteral("--output-vocals-wav=") + vocals, QStringLiteral("--output-accompaniment-wav=") + rest},
                                               folder, cancel, limit);
    if (!r.ok())
        return failed(r, QObject::tr("The separation program"));
    if (progress)
        progress(0.9);
    FfmpegPreprocessor pre;
    return pre.convert(vocals, wavOut, {}, cancel);
}

// ---- punctuation ----

bool SherpaPunctuator::available() const
{
    return !m_store->path(sherpaId()).isEmpty() && !m_store->path(modelId()).isEmpty() && QFileInfo::exists(QDir(m_store->folder(modelId())).filePath(QStringLiteral("bpe.vocab")));
}

QString SherpaPunctuator::punctuate(const QStringList &texts, const std::atomic_bool *cancel, QStringList *out)
{
    if (!available())
        return QObject::tr("The punctuation model is not installed");
    const QString folder = m_store->folder(sherpaId());
    const QString exe = SherpaRun::program(folder, QStringLiteral("sherpa-onnx-online-punctuation"));
    if (!QFileInfo::exists(exe))
        return QObject::tr("The punctuation program is not in the helper that is installed; download it again");
    const QString model = m_store->path(modelId()), vocab = QDir(m_store->folder(modelId())).filePath(QStringLiteral("bpe.vocab"));
    QStringList answers(texts.size());
    QString firstError;
    QMutex lock;
    std::atomic_int failed{0};
    QThreadPool pool;
    pool.setMaxThreadCount(qBound(1, qMin(4, m_threads ? m_threads() : 2), 4));
    for (int i = 0; i < texts.size(); ++i) {
        (void)QtConcurrent::run(&pool, [&, i] {
            // the text is an argument of the program, and it comes from what was said in the recording: one that starts with "-" could be taken for an option of it
            if ((cancel && cancel->load()) || texts[i].isEmpty() || texts[i].startsWith(u'-'))
                return;
            const SherpaRun::Result r = SherpaRun::run(exe, {QStringLiteral("--cnn-bilstm=") + model, QStringLiteral("--bpe-vocab=") + vocab, QStringLiteral("--num-threads=1"), texts[i]}, folder, cancel, 60000);
            const QString text = r.ok() ? SherpaRun::parsePunctuated(r.output) : QString();
            QMutexLocker locker(&lock);
            if (text.isEmpty()) {
                ++failed;
                if (firstError.isEmpty())
                    firstError = r.ok() ? QObject::tr("the program gave no text") : SherpaRun::tail(r.output);
            } else {
                answers[i] = text;
            }
        });
    }
    pool.waitForDone();
    if (cancel && cancel->load())
        return QStringLiteral("cancelled");
    int wanted = 0;
    for (const QString &t : texts)
        wanted += t.isEmpty() || t.startsWith(u'-') ? 0 : 1;
    if (wanted > 0 && failed.load() * 2 > wanted) // most of it failed: the text stays as it was
        return firstError.isEmpty() ? QObject::tr("the program gave no text") : firstError;
    *out = answers; // (a text that failed is "": the pipeline leaves it alone)
    return {};
}

} // namespace Audio
