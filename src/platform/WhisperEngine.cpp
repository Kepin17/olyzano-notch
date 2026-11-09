#include "WhisperEngine.h" // not Whisper.h: on Windows (case-insensitive) "whisper.h" below would find it instead of the library's

#include <QRegularExpression>
#include <QThread>
#include <vector>

#ifdef NOTCH_HAVE_WHISPER
#include "ggml-backend.h"
#include "whisper.h"
#endif

struct Whisper::Impl {
#ifdef NOTCH_HAVE_WHISPER
    whisper_context *ctx = nullptr;
#endif
    QString backend = QStringLiteral("CPU");
    bool fellBack = false;
};

QList<Whisper::GpuDevice> Whisper::gpus()
{
    QList<GpuDevice> out;
#ifdef NOTCH_HAVE_WHISPER
    int cnt = 0; // whisper.cpp counts only the devices that are a GPU, in this order
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        const auto type = ggml_backend_dev_type(dev);
        if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU)
            continue;
        GpuDevice g;
        g.index = cnt++;
        g.name = QString::fromUtf8(ggml_backend_dev_description(dev));
        g.integrated = type == GGML_BACKEND_DEVICE_TYPE_IGPU;
        size_t free = 0, total = 0;
        ggml_backend_dev_memory(dev, &free, &total);
        g.freeBytes = qint64(free);
        g.totalBytes = qint64(total);
        if (g.name.contains(QLatin1String("llvmpipe"), Qt::CaseInsensitive) || g.name.contains(QLatin1String("software"), Qt::CaseInsensitive))
            continue; // a processor pretending to be a card
        out.append(g);
    }
#endif
    return out;
}

bool Whisper::gpusBuilt()
{
#ifdef NOTCH_GPU_BUILT
    return true;
#else
    return false;
#endif
}

QString Whisper::backend() const { return d->backend; }
bool Whisper::fellBack() const { return d->fellBack; }

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

bool Whisper::load(const QString &modelPath, bool gpu)
{
#ifdef NOTCH_HAVE_WHISPER
    unload();
    d->backend = QStringLiteral("CPU");
    d->fellBack = false;
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    QString gpuName;
    if (gpu) {
        const QList<GpuDevice> list = gpus();
        const GpuDevice *best = nullptr; // a card of its own first, then the one with the most room
        for (const GpuDevice &g : list)
            if (!best || (best->integrated && !g.integrated) || (best->integrated == g.integrated && g.freeBytes > best->freeBytes))
                best = &g;
        if (best) {
            cp.use_gpu = true;
            cp.gpu_device = best->index;
            gpuName = best->name;
        } else {
            d->fellBack = true; // asked for, but this build or this computer has none
        }
    }
    d->ctx = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cp);
    if (!d->ctx && cp.use_gpu) { // the card would not take it: the processor will
        cp.use_gpu = false;
        d->fellBack = true;
        d->ctx = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cp);
    }
    if (d->ctx && cp.use_gpu)
        d->backend = QStringLiteral("GPU: ") + gpuName;
    return d->ctx != nullptr;
#else
    Q_UNUSED(modelPath)
    Q_UNUSED(gpu)
    return false;
#endif
}

QString Whisper::transcribeSegments(const QVector<float> &samples, const QString &language, int threads, const std::atomic_bool *cancel, QList<Piece> *out,
                                    QString *detectedLanguage, const QStringList &candidates, bool fast)
{
#ifdef NOTCH_HAVE_WHISPER
    if (!d->ctx)
        return QStringLiteral("the speech model is not loaded");
    if (samples.size() < 1600)
        return {};
    threads = qBound(1, threads, 16);
    // quiet recordings: bring the loudest peak up (at most 10 times), no padding so the times stay true
    float peak = 0;
    for (const float v : samples)
        peak = qMax(peak, qAbs(v));
    const float gain = peak > 0.004f ? qMin(10.0f, 0.9f / peak) : 1.0f;
    QVector<float> pcm = samples;
    if (gain > 1.01f)
        for (float &v : pcm)
            v = qBound(-1.0f, v * gain, 1.0f);

    QByteArray lang = language.isEmpty() ? QByteArray("auto") : language.toUtf8();
    if (lang == "auto") {
        lang = "id";
        if (whisper_pcm_to_mel(d->ctx, pcm.constData(), int(pcm.size()), threads) == 0) {
            std::vector<float> probs(size_t(whisper_lang_max_id()) + 1, 0.0f);
            if (whisper_lang_auto_detect(d->ctx, 0, threads, probs.data()) >= 0) {
                const auto p = [&](const char *code) { const int id = whisper_lang_id(code); return id >= 0 ? probs[size_t(id)] : 0.0f; };
                if (!candidates.isEmpty()) { // the likeliest of the languages in use (Malay counts for Indonesian when that is in use)
                    float best = -1;
                    for (const QString &c : candidates) {
                        const QByteArray code = c.toLatin1();
                        float prob = p(code.constData());
                        if (c == QLatin1String("id") && !candidates.contains(QLatin1String("ms")))
                            prob += p("ms");
                        if (prob > best) {
                            best = prob;
                            lang = code;
                        }
                    }
                    if (best < 0.05f) { // none of them: say what it is
                        int top = 0;
                        for (size_t k = 1; k < probs.size(); ++k)
                            if (probs[k] > probs[size_t(top)])
                                top = int(k);
                        lang = whisper_lang_str(top);
                    }
                } else if (const float en = p("en"), id = p("id") + p("ms"); en + id >= 0.5f) { // one of the two languages this is for
                    lang = en > id ? "en" : "id";
                } else {               // neither: let the model say what it is
                    int best = 0;
                    for (size_t k = 1; k < probs.size(); ++k)
                        if (probs[k] > probs[size_t(best)])
                            best = int(k);
                    lang = whisper_lang_str(best);
                }
            }
        }
    }
    if (detectedLanguage)
        *detectedLanguage = QString::fromLatin1(lang);

    // Beam search reads speech more accurately than greedy decoding (fewer wrong words, fewer repeats) for about twice the time; and when
    // a window comes out unsure the model tries again with a little randomness (temperature fallback), which fixes most garbled ones.
    whisper_full_params wp = whisper_full_default_params(fast ? WHISPER_SAMPLING_GREEDY : WHISPER_SAMPLING_BEAM_SEARCH);
    wp.beam_search.beam_size = 5;
    wp.temperature = 0.0f;
    wp.temperature_inc = fast ? 0.0f : 0.2f;
    wp.entropy_thold = 2.4f;
    wp.logprob_thold = -1.0f;
    wp.no_speech_thold = 0.6f;
    wp.print_progress = false;
    wp.print_realtime = false;
    wp.print_special = false;
    wp.print_timestamps = false;
    wp.no_timestamps = false;
    wp.single_segment = false;
    wp.translate = false;
    wp.no_context = true; // each window stands alone: nothing carried over to drift into repeating itself
    wp.suppress_blank = true;
    wp.max_tokens = 0;
    wp.n_threads = threads;
    wp.language = lang.constData();
    wp.detect_language = false;
    // A sentence in the language, with its punctuation and capitals, makes the model write that way (and spell names as people do).
    const QByteArray style = lang == "id" ? QByteArray("Halo, selamat pagi. Terima kasih sudah hadir. Baik, kita mulai rapatnya.")
                           : lang == "en" ? QByteArray("Hello, good morning. Thank you all for coming. Okay, let's get started.") : QByteArray();
    if (!style.isEmpty())
        wp.initial_prompt = style.constData();
    wp.abort_callback = [](void *user) { return user && static_cast<const std::atomic_bool *>(user)->load(); };
    wp.abort_callback_user_data = const_cast<std::atomic_bool *>(cancel);
    const int rc = whisper_full(d->ctx, wp, pcm.constData(), int(pcm.size()));
    if (cancel && cancel->load())
        return QStringLiteral("cancelled");
    if (rc != 0)
        return QStringLiteral("the speech model failed (code %1)").arg(rc);
    for (int i = 0, n = whisper_full_n_segments(d->ctx); i < n; ++i) {
        Piece p;
        p.start = double(whisper_full_get_segment_t0(d->ctx, i)) / 100.0; // centiseconds
        p.end = double(whisper_full_get_segment_t1(d->ctx, i)) / 100.0;
        p.text = QString::fromUtf8(whisper_full_get_segment_text(d->ctx, i)).trimmed();
        p.noSpeech = whisper_full_get_segment_no_speech_prob(d->ctx, i);
        out->append(p);
    }
    return {};
#else
    Q_UNUSED(samples) Q_UNUSED(language) Q_UNUSED(threads) Q_UNUSED(cancel) Q_UNUSED(out) Q_UNUSED(detectedLanguage)
    return QStringLiteral("this build has no speech recognition");
#endif
}

Whisper::Transcript Whisper::hear(const QVector<float> &samples, const QString &language, const QString &vocabulary)
{
    Transcript out;
#ifdef NOTCH_HAVE_WHISPER
    if (!d->ctx || samples.size() < 1600) // less than 0.1 s
        return out;
    const int threads = qBound(1, QThread::idealThreadCount() - 1, 4);

    // A quiet microphone is a poor recording for the model: bring the loudest peak to 0.9 (at most 25 times louder) and
    // add a little silence on both sides, which the model likes.
    float peak = 0;
    for (const float v : samples)
        peak = qMax(peak, qAbs(v));
    const float gain = peak > 0.004f ? qMin(25.0f, 0.9f / peak) : 1.0f;
    constexpr int Pad = 16000 * 4 / 10;
    QVector<float> pcm(Pad, 0.0f);
    pcm.reserve(samples.size() + 2 * Pad);
    for (const float v : samples)
        pcm.append(qBound(-1.0f, v * gain, 1.0f));
    pcm.append(QVector<float>(Pad, 0.0f));

    // "auto" here means Indonesian or English, not any of a hundred languages: a short command is often taken for Malay
    // or something else, and the words come out wrong. Malay counts for Indonesian.
    QByteArray lang = language.isEmpty() ? QByteArray("auto") : language.toUtf8();
    if (lang == "auto") {
        lang = "id";
        if (whisper_pcm_to_mel(d->ctx, pcm.constData(), int(pcm.size()), threads) == 0) {
            std::vector<float> probs(size_t(whisper_lang_max_id()) + 1, 0.0f);
            if (whisper_lang_auto_detect(d->ctx, 0, threads, probs.data()) >= 0) {
                const auto p = [&](const char *code) { const int id = whisper_lang_id(code); return id >= 0 ? probs[size_t(id)] : 0.0f; };
                lang = p("en") > p("id") + p("ms") ? "en" : "id";
            }
        }
    }

    // Beam search is slower than greedy but wrong less often; the sentence is short anyway. The prompt tells the model
    // which words to expect (names of things to open, search words), which settles the spelling.
    whisper_full_params wp = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    wp.beam_search.beam_size = 5;
    wp.print_progress = false;
    wp.print_realtime = false;
    wp.print_special = false;
    wp.print_timestamps = false;
    wp.no_timestamps = true;
    wp.single_segment = true;
    wp.translate = false;
    wp.no_context = true;
    wp.suppress_blank = true;
    wp.temperature = 0.0f;
    wp.temperature_inc = 0.0f; // no retries with more randomness: a command is short and a retry costs seconds
    wp.max_tokens = 64;
    wp.n_threads = threads;
    wp.language = lang.constData();
    wp.detect_language = false; // true would only detect the language and return
    QByteArray prompt = lang == "en" ? QByteArray("Open the browser. Search for cheap graphics cards on YouTube. Set the volume to 40. Timer ten minutes. Add task buy milk.")
                                     : QByteArray("Buka YouTube. Cari info VGA yang murah. Cari channel di YouTube. Volume empat puluh. Timer sepuluh menit. Tambah tugas beli susu.");
    if (!vocabulary.isEmpty())
        prompt += (lang == "en" ? " Apps: " : " Aplikasi: ") + vocabulary.left(260).toUtf8() + ".";
    wp.initial_prompt = prompt.constData();
    if (whisper_full(d->ctx, wp, pcm.constData(), int(pcm.size())) != 0)
        return out;
    QString text;
    double sum = 0;
    int counted = 0;
    const whisper_token eot = whisper_token_eot(d->ctx);
    for (int i = 0, n = whisper_full_n_segments(d->ctx); i < n; ++i) {
        text += QString::fromUtf8(whisper_full_get_segment_text(d->ctx, i));
        for (int t = 0, nt = whisper_full_n_tokens(d->ctx, i); t < nt; ++t) {
            const whisper_token_data td = whisper_full_get_token_data(d->ctx, i, t);
            if (td.id >= eot) // the special tokens (timestamps, language) are not words
                continue;
            sum += td.p;
            ++counted;
            if (td.p < 0.45) {
                const QString piece = QString::fromUtf8(whisper_full_get_token_text(d->ctx, i, t)).trimmed();
                if (!piece.isEmpty())
                    out.doubtful << piece;
            }
        }
    }
    out.confidence = counted > 0 ? sum / counted : 0.0;
    out.language = QString::fromLatin1(lang);
    // Noise comes back as "(humming)", "[BLANK_AUDIO]" or a note: that is no speech
    text.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]|\\([^)]*\\)|[\u266a\u266b]")));
    // With nothing to say the model may repeat its own prompt: that is no speech either
    const QString simple = text.simplified();
    if (simple.isEmpty() || (QString::fromUtf8(prompt).contains(simple, Qt::CaseInsensitive) && simple.size() > 12))
        return Transcript{};
    out.text = simple;
    return out;
#else
    Q_UNUSED(samples)
    Q_UNUSED(language)
    Q_UNUSED(vocabulary)
    return out;
#endif
}
