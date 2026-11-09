#include "AudioPipeline.h"

#include "AudioJson.h"
#include "Vad.h"
#include "WavFile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

namespace Audio {
namespace {
constexpr int Rate = 16000;

struct Stage {
    const char *id, *label;
    double weight;
};
const Stage Stages[] = {{"preprocess", "Preparing the audio", 0.05}, {"vad", "Finding the speech", 0.03}, {"asr", "Writing the transcript", 0.62},
                        {"diarize", "Telling the speakers apart", 0.15}, {"align", "Putting it together", 0.01}, {"summarize", "Writing the notes", 0.14}};

QString fingerprint(const QList<Segment> &segments)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    for (const Segment &s : segments) {
        h.addData(s.text.toUtf8());
        h.addData(s.speaker.toUtf8());
    }
    return QString::fromLatin1(h.result().toHex());
}

QString monoMix(const QString &stereoPath, const QString &out)
{
    const QVector<float> mix = Wav::readFrames(stereoPath, -1);
    if (mix.isEmpty())
        return QStringLiteral("the recording could not be read");
    QVector<qint16> pcm(mix.size());
    for (qsizetype i = 0; i < mix.size(); ++i)
        pcm[i] = qint16(std::clamp(mix[i], -1.0f, 1.0f) * 32767.0f);
    return Wav::writePcm16(out, pcm, 1, Rate);
}

QString channelFile(const QString &stereoPath, int channel, const QString &out)
{
    const QVector<float> c = Wav::readFrames(stereoPath, channel);
    if (c.isEmpty())
        return QStringLiteral("the recording could not be read");
    QVector<qint16> pcm(c.size());
    for (qsizetype i = 0; i < c.size(); ++i)
        pcm[i] = qint16(std::clamp(c[i], -1.0f, 1.0f) * 32767.0f);
    return Wav::writePcm16(out, pcm, 1, Rate);
}
}

QStringList AudioPipeline::stageIds()
{
    QStringList out;
    for (const Stage &s : Stages)
        out << QString::fromLatin1(s.id);
    return out;
}

QString AudioPipeline::stageLabel(const QString &id)
{
    for (const Stage &s : Stages)
        if (id == QLatin1String(s.id))
            return QString::fromLatin1(s.label);
    return id;
}

double AudioPipeline::overall(const QString &stage, double fraction)
{
    double before = 0;
    for (const Stage &s : Stages) {
        if (stage == QLatin1String(s.id))
            return std::clamp(before + s.weight * std::clamp(fraction, 0.0, 1.0), 0.0, 1.0);
        before += s.weight;
    }
    return 0;
}

QList<Segment> AudioPipeline::clean(const QList<Segment> &in)
{
    static const QRegularExpression mark(QStringLiteral("^\\s*[\\[(][^\\])]{0,40}[\\])]\\s*$"));
    QList<Segment> kept;
    for (Segment s : in) {
        s.text = s.text.simplified();
        if (s.text.isEmpty() || mark.match(s.text).hasMatch())
            continue;
        bool word = false;
        for (const QChar c : s.text)
            word = word || c.isLetterOrNumber();
        if (word)
            kept.append(s);
    }
    QList<Segment> out;
    for (int i = 0; i < kept.size();) {
        int j = i + 1;
        const QString key = normalize(kept[i].text);
        while (j < kept.size() && normalize(kept[j].text) == key)
            ++j;
        if (j - i >= 3) { // the same phrase over and over: what the recognizer makes up in silence
            Segment s = kept[i];
            s.end = kept[j - 1].end;
            out.append(s);
        } else {
            for (int k = i; k < j; ++k)
                out.append(kept[k]);
        }
        i = j;
    }
    return out;
}

// "Write the notes" for r.segments. false when it failed or was stopped (r says which); true otherwise, with or without notes (a warning says why none).
bool AudioPipeline::summarizeStage(PipelineResult &r, const QString &workDir, const PipelineOptions &options, const std::atomic_bool &cancel, const PipelineHooks &hooks)
{
    const auto report = [&](const char *stage, double f) {
        if (hooks.report)
            hooks.report(QString::fromLatin1(stage), f);
    };
    if (cancel.load()) {
        r.cancelled = true;
        r.error = QStringLiteral("cancelled");
        return false;
    }
    if (!options.summarize)
        return true;
    const QString file = workDir + QStringLiteral("/summary.json");
    const QString hash = QStringLiteral("v2|") + fingerprint(r.segments) + u'|' + templateId(options.tmpl) + u'|' + options.language; // v2: the richer notes
    const QJsonObject saved = Json::load(file);
    QStringList names;
    for (const Segment &s : std::as_const(r.segments))
        if (!names.contains(s.speaker))
            names << s.speaker;
    if (saved.value(QStringLiteral("hash")).toString() == hash && saved.contains(QStringLiteral("summary"))) {
        r.summary = Json::summary(saved.value(QStringLiteral("summary")).toObject());
        r.hasSummary = true;
    } else if (!m_summarizer || !m_summarizer->available()) {
        r.warnings << QStringLiteral("There are no notes: the language model is not installed.");
    } else {
        report("summarize", 0);
        Summary s;
        const QString err = m_summarizer->summarize(r.segments, names, options.tmpl, options.language, [&](double f) { report("summarize", f); }, &cancel, &s);
        if (cancel.load() || err == QLatin1String("cancelled")) {
            r.cancelled = true;
            r.error = QStringLiteral("cancelled");
            return false;
        }
        if (!err.isEmpty()) { // the transcript stays; only the notes are missing
            r.failedStage = QStringLiteral("summarize");
            r.error = err;
            return false;
        }
        r.summary = s;
        r.hasSummary = true;
        Json::save(file, {{QStringLiteral("hash"), hash}, {QStringLiteral("summary"), Json::summary(s)}});
    }
    if (r.hasSummary) {
        r.doneStages << QStringLiteral("summarize");
        if (hooks.summary)
            hooks.summary(r.summary);
        report("summarize", 1);
    }
    return true;
}

PipelineResult AudioPipeline::notes(const QList<Segment> &segments, const QString &workDir, const PipelineOptions &options, const std::atomic_bool &cancel,
                                    const PipelineHooks &hooks)
{
    PipelineResult r;
    r.segments = segments;
    r.language = options.language;
    QDir().mkpath(workDir);
    r.ok = summarizeStage(r, workDir, options, cancel, hooks);
    return r;
}

PipelineResult AudioPipeline::run(const QString &source, const QString &workDir, const PipelineOptions &options, const std::atomic_bool &cancel,
                                  const PipelineHooks &hooks)
{
    PipelineResult r;
    const auto report = [&](const char *stage, double f) {
        if (hooks.report)
            hooks.report(QString::fromLatin1(stage), f);
    };
    const auto fail = [&](const char *stage, const QString &why) {
        r.failedStage = QString::fromLatin1(stage);
        r.error = why;
        return r;
    };
    const auto stopped = [&] {
        r.cancelled = true;
        r.error = QStringLiteral("cancelled");
        return r;
    };
    if (!QDir().mkpath(workDir))
        return fail("preprocess", QStringLiteral("cannot create the folder of this recording"));
    const QString work = workDir + QStringLiteral("/work.wav"), stereoFile = workDir + QStringLiteral("/stereo.wav");

    // ---- preprocess: 16 kHz mono (and, for a recording from Notch, the two channels kept) ----
    {
        const QFileInfo src(source);
        const QString fp = QStringLiteral("%1|%2|%3").arg(src.size()).arg(src.lastModified().toMSecsSinceEpoch()).arg(options.stereo);
        const Wav::Info have = Wav::readInfo(work);
        const bool reuse = Json::load(workDir + QStringLiteral("/preprocess.json")).value(QStringLiteral("fp")).toString() == fp && have.valid() && have.rate == Rate
                           && have.channels == 1 && (!options.stereo || Wav::readInfo(stereoFile).valid());
        if (!reuse) {
            report("preprocess", 0);
            QString err;
            const Wav::Info si = Wav::readInfo(source);
            QFile::remove(work);
            if (options.stereo) {
                if (!si.valid() || si.rate != Rate || si.channels != 2 || si.bits != 16)
                    return fail("preprocess", QStringLiteral("this recording is not a 16 kHz stereo file"));
                QFile::remove(stereoFile);
                if (!QFile::copy(source, stereoFile))
                    return fail("preprocess", QStringLiteral("cannot copy the recording"));
                err = monoMix(stereoFile, work);
            } else if (si.valid() && si.rate == Rate && si.channels == 1 && si.bits == 16) {
                if (!QFile::copy(source, work))
                    return fail("preprocess", QStringLiteral("cannot copy the audio"));
            } else {
                if (!m_pre)
                    return fail("preprocess", QStringLiteral("ffmpeg is needed to read this file"));
                err = m_pre->convert(source, work, [&](double f) { report("preprocess", f); }, &cancel);
            }
            if (cancel.load())
                return stopped();
            if (!err.isEmpty())
                return fail("preprocess", err);
            if (const QString e = Json::save(workDir + QStringLiteral("/preprocess.json"), {{QStringLiteral("fp"), fp}}); !e.isEmpty())
                return fail("preprocess", e);
        }
        const Wav::Info info = Wav::readInfo(work);
        if (!info.valid() || info.rate != Rate || info.channels != 1)
            return fail("preprocess", QStringLiteral("the audio could not be read"));
        r.duration = info.seconds();
        if (r.duration < 0.5)
            return fail("preprocess", QStringLiteral("this recording is too short"));
        if (r.duration > options.maxSeconds)
            return fail("preprocess", QStringLiteral("this recording is longer than %1 minutes").arg(int(options.maxSeconds / 60)));
        r.doneStages << QStringLiteral("preprocess");
        report("preprocess", 1);
    }
    if (cancel.load())
        return stopped();

    // ---- vad: where the speech is, cut into windows the recognizer takes ----
    QVector<float> samples; // loaded when a stage needs them, kept for the next
    const auto load = [&] {
        if (samples.isEmpty())
            samples = Wav::readFrames(work, 0);
        return !samples.isEmpty();
    };
    QList<Vad::Region> windows;
    {
        const QJsonObject saved = Json::load(workDir + QStringLiteral("/vad.json"));
        if (saved.value(QStringLiteral("window")).toInt() == options.windowSeconds && saved.contains(QStringLiteral("windows"))
            && qAbs(saved.value(QStringLiteral("duration")).toDouble() - r.duration) < 0.01) {
            for (const QJsonValue &v : saved.value(QStringLiteral("windows")).toArray())
                windows.append({qint64(v.toArray().at(0).toDouble()), qint64(v.toArray().at(1).toDouble())});
        } else {
            report("vad", 0);
            if (!load())
                return fail("vad", QStringLiteral("the audio could not be read"));
            windows = Vad::windows(samples, Rate, options.windowSeconds);
            QJsonArray a;
            for (const Vad::Region &w : windows)
                a.append(QJsonArray{double(w.start), double(w.end)});
            if (const QString e = Json::save(workDir + QStringLiteral("/vad.json"),
                                             {{QStringLiteral("window"), options.windowSeconds}, {QStringLiteral("duration"), r.duration}, {QStringLiteral("windows"), a}});
                !e.isEmpty())
                return fail("vad", e);
        }
        if (windows.isEmpty())
            return fail("vad", QStringLiteral("no speech was found in this recording"));
        r.doneStages << QStringLiteral("vad");
        report("vad", 1);
    }
    if (cancel.load())
        return stopped();

    // ---- asr ----
    QList<Segment> asr;
    {
        const QString doneFile = workDir + QStringLiteral("/asr.json"), partialFile = workDir + QStringLiteral("/asr.partial.json");
        const QJsonObject done = Json::load(doneFile);
        const auto sameSettings = [&](const QJsonObject &o) {
            return o.value(QStringLiteral("quality")).toString() == options.quality && o.value(QStringLiteral("language")).toString() == options.language
                   && o.value(QStringLiteral("windows")).toInt() == windows.size();
        };
        if (done.value(QStringLiteral("complete")).toBool() && sameSettings(done)) {
            asr = Json::segments(done.value(QStringLiteral("segments")).toArray());
            r.language = done.value(QStringLiteral("detected")).toString();
        } else {
            if (!m_asr)
                return fail("asr", QStringLiteral("no speech recognizer"));
            if (const QString e = m_asr->prepare(options.quality); !e.isEmpty())
                return fail("asr", e);
            int from = 0;
            QHash<QString, int> votes;
            const QJsonObject partial = Json::load(partialFile);
            if (sameSettings(partial)) { // continue where it stopped
                from = std::clamp(partial.value(QStringLiteral("done")).toInt(), 0, int(windows.size()));
                asr = Json::segments(partial.value(QStringLiteral("segments")).toArray());
                const QJsonObject v = partial.value(QStringLiteral("votes")).toObject();
                for (auto it = v.begin(); it != v.end(); ++it)
                    votes[it.key()] = it.value().toInt();
            }
            if (!load()) {
                m_asr->release();
                return fail("asr", QStringLiteral("the audio could not be read"));
            }
            const auto persist = [&](int doneWindows) {
                QJsonObject votesJson;
                for (auto it = votes.cbegin(); it != votes.cend(); ++it)
                    votesJson.insert(it.key(), it.value());
                Json::save(partialFile, {{QStringLiteral("quality"), options.quality}, {QStringLiteral("language"), options.language},
                                         {QStringLiteral("windows"), int(windows.size())}, {QStringLiteral("done"), doneWindows},
                                         {QStringLiteral("segments"), Json::segments(asr)}, {QStringLiteral("votes"), votesJson}});
            };
            report("asr", double(from) / windows.size());
            for (int i = from; i < windows.size(); ++i) {
                if (cancel.load()) {
                    persist(i);
                    m_asr->release();
                    return stopped();
                }
                const Vad::Region &w = windows[i];
                QList<Segment> part;
                QString lang;
                const QString err = m_asr->transcribe(samples.mid(int(w.start), int(w.length())), options.language, &cancel, &part, &lang);
                if (cancel.load()) {
                    persist(i);
                    m_asr->release();
                    return stopped();
                }
                if (!err.isEmpty()) {
                    persist(i); // what is done stays; "try again" goes on from this window
                    m_asr->release();
                    return fail("asr", err);
                }
                const double offset = double(w.start) / Rate;
                for (Segment s : std::as_const(part)) {
                    s.start += offset;
                    s.end += offset;
                    asr.append(s);
                }
                if (!lang.isEmpty())
                    votes[lang] += int(w.length());
                if ((i + 1) % 5 == 0)
                    persist(i + 1);
                report("asr", double(i + 1) / windows.size());
            }
            m_asr->release();
            asr = clean(asr);
            QString detected = options.language != QLatin1String("auto") ? options.language : QString();
            if (detected.isEmpty()) {
                int best = 0;
                for (auto it = votes.cbegin(); it != votes.cend(); ++it)
                    if (it.value() > best) {
                        best = it.value();
                        detected = it.key();
                    }
            }
            r.language = detected;
            if (const QString e = Json::save(doneFile, {{QStringLiteral("complete"), true}, {QStringLiteral("quality"), options.quality}, {QStringLiteral("language"), options.language},
                                                         {QStringLiteral("windows"), int(windows.size())}, {QStringLiteral("detected"), detected},
                                                         {QStringLiteral("segments"), Json::segments(asr)}});
                !e.isEmpty())
                return fail("asr", e);
            QFile::remove(partialFile);
        }
        samples.clear(); // the next stages do not need them
        samples.squeeze();
        if (asr.isEmpty())
            return fail("asr", QStringLiteral("no words were recognized in this recording"));
        r.doneStages << QStringLiteral("asr");
        report("asr", 1);
    }
    if (cancel.load())
        return stopped();

    // ---- diarize ----
    QList<Turn> remoteTurns, localTurns; // localTurns: the microphone's channel of a recording from Notch (several people can sit at one microphone)
    if (options.diarize) {
        const QString file = workDir + QStringLiteral("/diarization.json");
        const QJsonObject saved = Json::load(file);
        const int wanted = options.stereo ? 0 : options.speakers; // a stereo recording is split by channel: the number is not asked of it
        if (saved.contains(QStringLiteral("turns")) && saved.value(QStringLiteral("stereo")).toBool() == options.stereo && saved.value(QStringLiteral("speakers")).toInt() == wanted
            && saved.value(QStringLiteral("v")).toInt() == 2) {
            remoteTurns = Json::turns(saved.value(QStringLiteral("turns")).toArray());
            localTurns = Json::turns(saved.value(QStringLiteral("localTurns")).toArray());
        } else if (!m_diarizer || !m_diarizer->available()) {
            r.warnings << QStringLiteral("The speakers were not told apart: the speaker model is not installed.");
        } else {
            report("diarize", 0);
            QString err;
            if (options.stereo) { // the computer's sound (right) and the microphone (left), each on its own
                const QString right = workDir + QStringLiteral("/right.wav"), left = workDir + QStringLiteral("/left.wav");
                err = channelFile(stereoFile, 1, right);
                if (err.isEmpty())
                    err = m_diarizer->diarize(right, &cancel, &remoteTurns, 0);
                if (err.isEmpty() && !cancel.load()) {
                    report("diarize", 0.5);
                    err = channelFile(stereoFile, 0, left);
                    if (err.isEmpty() && m_diarizer->diarize(left, &cancel, &localTurns, 0).isEmpty()) {
                        // the microphone alone not being told apart is not a failure: it is "You"
                    } else {
                        localTurns.clear();
                    }
                }
            } else {
                err = m_diarizer->diarize(work, &cancel, &remoteTurns, wanted);
            }
            if (cancel.load())
                return stopped();
            if (err.isEmpty()) { // a moment of noise taken for another person is not one
                remoteTurns = mergeTinySpeakers(remoteTurns);
                localTurns = mergeTinySpeakers(localTurns);
            }
            if (!err.isEmpty()) {
                remoteTurns.clear();
                localTurns.clear();
                r.warnings << QStringLiteral("The speakers could not be told apart: %1").arg(err);
            } else if (const QString e = Json::save(file, {{QStringLiteral("v"), 2}, {QStringLiteral("stereo"), options.stereo}, {QStringLiteral("speakers"), wanted},
                                                           {QStringLiteral("turns"), Json::turns(remoteTurns)}, {QStringLiteral("localTurns"), Json::turns(localTurns)}});
                       !e.isEmpty()) {
                r.warnings << e;
            }
        }
        r.doneStages << QStringLiteral("diarize");
        report("diarize", 1);
    }

    // ---- align ----
    {
        QList<Turn> turns;
        if (options.stereo) {
            const QVector<float> left = Wav::readFrames(stereoFile, 0), right = Wav::readFrames(stereoFile, 1);
            // the far side: one voice is "Others", several are "Others 1", "Others 2"
            QStringList remoteNames;
            for (const Turn &t : std::as_const(remoteTurns))
                if (!remoteNames.contains(t.speaker))
                    remoteNames << t.speaker;
            for (Turn t : std::as_const(remoteTurns)) {
                if (remoteNames.size() > 1)
                    t.speaker = QStringLiteral("%1 %2").arg(options.remoteLabel).arg(remoteNames.indexOf(t.speaker) + 1);
                else
                    t.speaker = options.remoteLabel;
                turns.append(t);
            }
            if (remoteTurns.isEmpty()) { // nobody separated the others: they are one voice, where the computer's sound is
                for (const Vad::Region &g : Vad::speech(right, Rate))
                    turns.append({double(g.start) / Rate, double(g.end) / Rate, options.remoteLabel});
            }
            // the near side: where the microphone is clearly louder than the computer's sound
            const QList<Turn> mic = micTurns(left, right, Rate, options.youLabel);
            if (localTurns.isEmpty()) {
                turns += mic;
            } else {
                // each voice at the microphone, cut to where the microphone is the louder; the one who talks most is "You", the rest are others in the room
                QHash<QString, double> spoke;
                QList<Turn> clipped;
                for (const Turn &t : std::as_const(localTurns))
                    for (const Turn &m : mic) {
                        const double from = std::max(t.start, m.start), to = std::min(t.end, m.end);
                        if (to - from >= 0.2) {
                            clipped.append({from, to, t.speaker});
                            spoke[t.speaker] += to - from;
                        }
                    }
                QStringList order = spoke.keys();
                std::sort(order.begin(), order.end(), [&](const QString &a, const QString &b) { return spoke[a] != spoke[b] ? spoke[a] > spoke[b] : a < b; });
                for (Turn t : std::as_const(clipped)) {
                    const int k = order.indexOf(t.speaker);
                    t.speaker = k <= 0 ? options.youLabel : QStringLiteral("%1 %2").arg(options.localLabel).arg(k + 1);
                    turns.append(t);
                }
                // where the microphone is loud but no voice was found: still the person at the microphone
                for (const Turn &m : mic) {
                    double covered = 0;
                    for (const Turn &c : std::as_const(clipped))
                        covered += std::max(0.0, std::min(m.end, c.end) - std::max(m.start, c.start));
                    if (covered < 0.3 * (m.end - m.start))
                        turns.append(m);
                }
            }
        } else {
            turns = remoteTurns;
        }
        r.segments = align(asr, turns);
        r.doneStages << QStringLiteral("align");
        if (hooks.transcript)
            hooks.transcript(r.segments, r.language, r.duration);
    }
    if (cancel.load())
        return stopped();

    // ---- summarize ----
    if (!summarizeStage(r, workDir, options, cancel, hooks))
        return r;
    r.ok = true;
    return r;
}

} // namespace Audio
