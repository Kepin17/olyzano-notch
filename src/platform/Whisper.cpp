#include "Whisper.h"

#include <QThread>

#ifdef NOTCH_HAVE_WHISPER
#include "whisper.h"
#endif

struct Whisper::Impl {
#ifdef NOTCH_HAVE_WHISPER
    whisper_context *ctx = nullptr;
#endif
};

Whisper::Whisper() : d(std::make_unique<Impl>()) {}

Whisper::~Whisper() { unload(); }

bool Whisper::compiled()
{
#ifdef NOTCH_HAVE_WHISPER
    return true;
#else
    return false;
#endif
}

bool Whisper::loaded() const
{
#ifdef NOTCH_HAVE_WHISPER
    return d->ctx != nullptr;
#else
    return false;
#endif
}

void Whisper::unload()
{
#ifdef NOTCH_HAVE_WHISPER
    if (d->ctx)
        whisper_free(d->ctx);
    d->ctx = nullptr;
#endif
}

bool Whisper::load(const QString &modelPath)
{
#ifdef NOTCH_HAVE_WHISPER
    unload();
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    d->ctx = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cp);
    return d->ctx != nullptr;
#else
    Q_UNUSED(modelPath)
    return false;
#endif
}

QString Whisper::transcribe(const QVector<float> &samples, const QString &language)
{
#ifdef NOTCH_HAVE_WHISPER
    if (!d->ctx || samples.size() < 1600) // less than 0.1 s
        return {};
    whisper_full_params wp = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wp.print_progress = false;
    wp.print_realtime = false;
    wp.print_special = false;
    wp.print_timestamps = false;
    wp.no_timestamps = true;
    wp.single_segment = true;
    wp.translate = false;
    wp.n_threads = qBound(1, QThread::idealThreadCount() - 1, 4);
    const QByteArray lang = language.isEmpty() ? QByteArray("auto") : language.toUtf8();
    wp.language = lang.constData();
    wp.detect_language = false; // true would only detect the language and return
    if (whisper_full(d->ctx, wp, samples.constData(), int(samples.size())) != 0)
        return {};
    QString text;
    for (int i = 0, n = whisper_full_n_segments(d->ctx); i < n; ++i)
        text += QString::fromUtf8(whisper_full_get_segment_text(d->ctx, i));
    return text.simplified();
#else
    Q_UNUSED(samples)
    Q_UNUSED(language)
    return {};
#endif
}
