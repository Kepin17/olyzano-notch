#include "WhisperEngine.h" // not Whisper.h: on Windows (case-insensitive) "whisper.h" below would find it instead of the library's

#include <QRegularExpression>
#include <QThread>
#include <vector>

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
