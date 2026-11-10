#pragma once

#include "Aligner.h"
#include "Vad.h"

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>

// Music and other sounds in a recording. A sound-tagging model (CED, AudioSet's 527 kinds of sound) says what each 10 second clip holds; this
// file turns those answers into events (music, a crowd, applause), takes music and noise out of what the speech recognizer is given (it makes
// words up out of them), and writes the events as lines of the transcript that belong to no speaker. The model itself is behind an interface
// (AudioPipeline.h). Pure.
namespace Audio {
namespace Sound {

inline const QString Speaker = QStringLiteral("SOUND"); // the "speaker" of a line that is a sound, not a voice

enum class Kind {
    Plain,           // nothing to say: leave it to the recognizer
    SpeechOverMusic, // somebody talks over music: still transcribed, and marked
    Music,           // music and no talking
    Ambient          // another sound and no talking: a crowd, applause, rain
};

struct Tag {
    QString label; // the AudioSet name, e.g. "Music", "Male speech, man speaking"
    double prob = 0;
};

struct Clip {
    double start = 0, end = 0;
    Kind kind = Kind::Plain;
    QString label; // for Ambient: what it is
};

struct Event {
    double start = 0, end = 0;
    Kind kind = Kind::Music;
    QString label;
    double length() const { return end - start; }
};

constexpr double Threshold = 0.30;   // a kind of sound is "there" at this probability
constexpr double MinSeconds = 5.0;   // shorter events are not told
constexpr double ClipSeconds = 10.0; // what the model reads at once

inline bool containsAny(const QString &lower, std::initializer_list<const char *> words)
{
    for (const char *w : words)
        if (lower.contains(QLatin1String(w)))
            return true;
    return false;
}

inline bool isSpeech(const QString &lower) { return containsAny(lower, {"speech", "conversation", "narration", "monologue", "whispering", "babbling"}); }
inline bool isMusic(const QString &lower)
{
    return !isSpeech(lower) && containsAny(lower, {"music", "singing", "song", "choir", "guitar", "piano", "drum", "violin", "orchestra", "saxophone", "trumpet", "flute", "harp",
                                                   "synthesizer", "rapping", "jingle", "melody", "hip hop", "jazz", "rock", "pop "});
}
// what the model says about a room, not a sound of its own (the first part of the label: "Inside, small room" -> "inside")
inline bool isGeneric(const QString &lower)
{
    static const QStringList generic = {"silence", "inside", "outside", "noise", "white noise", "pink noise", "static", "environmental noise", "background noise", "sound effect", "hum", "mains hum"};
    return generic.contains(lower.section(QLatin1Char(','), 0, 0).trimmed());
}

// What one clip holds, from the model's most likely kinds of sound.
inline Clip classify(const QList<Tag> &top, double start, double end)
{
    double speech = 0, music = 0, ambient = 0;
    QString ambientLabel;
    for (const Tag &t : top) {
        const QString l = t.label.toLower();
        if (isSpeech(l))
            speech = std::max(speech, t.prob);
        else if (isMusic(l))
            music = std::max(music, t.prob);
        else if (!isGeneric(l) && t.prob > ambient) {
            ambient = t.prob;
            ambientLabel = t.label;
        }
    }
    Clip c;
    c.start = start;
    c.end = end;
    if (speech >= Threshold)
        c.kind = music >= Threshold ? Kind::SpeechOverMusic : Kind::Plain;
    else if (music >= Threshold)
        c.kind = Kind::Music;
    else if (ambient >= Threshold) {
        c.kind = Kind::Ambient;
        c.label = ambientLabel.section(QLatin1Char(','), 0, 0).trimmed();
    }
    return c;
}

// Clips (in time order) -> events. A plain clip between two clips of the same sound is made like them, runs of the same kind (the same sound,
// for Ambient) become one event, and an event shorter than MinSeconds is left out.
inline QList<Event> merge(QList<Clip> clips)
{
    const auto same = [](const Clip &a, const Clip &b) { return a.kind == b.kind && (a.kind != Kind::Ambient || a.label.compare(b.label, Qt::CaseInsensitive) == 0); };
    for (int i = 1; i + 1 < clips.size(); ++i)
        if (clips[i].kind == Kind::Plain && clips[i - 1].kind != Kind::Plain && same(clips[i - 1], clips[i + 1])) // a pause inside music, not music inside speech
            clips[i] = Clip{clips[i].start, clips[i].end, clips[i - 1].kind, clips[i - 1].label};
    QList<Event> out;
    for (int i = 0; i < clips.size();) {
        int j = i + 1;
        while (j < clips.size() && same(clips[i], clips[j]) && clips[j].start - clips[j - 1].end < 0.5)
            ++j;
        if (clips[i].kind != Kind::Plain) {
            const Event e{clips[i].start, clips[j - 1].end, clips[i].kind, clips[i].label};
            if (e.length() >= MinSeconds)
                out.append(e);
        }
        i = j;
    }
    return out;
}

// The regions the recognizer is given, without the stretches that are music or another sound and nothing else (samples at `rate`).
inline QList<Vad::Region> subtract(const QList<Vad::Region> &regions, const QList<Event> &events, int rate)
{
    QList<Vad::Region> out;
    const qint64 minLen = rate / 3;
    for (const Vad::Region &r : regions) {
        QList<Vad::Region> pieces{r};
        for (const Event &e : events) {
            if (e.kind == Kind::SpeechOverMusic)
                continue; // it is spoken: the recognizer hears it
            const qint64 a = qint64(e.start * rate), b = qint64(e.end * rate);
            QList<Vad::Region> next;
            for (const Vad::Region &p : std::as_const(pieces)) {
                if (b <= p.start || a >= p.end) {
                    next.append(p);
                    continue;
                }
                if (a > p.start)
                    next.append({p.start, a});
                if (b < p.end)
                    next.append({b, p.end});
            }
            pieces = next;
        }
        for (const Vad::Region &p : std::as_const(pieces))
            if (p.length() >= minLen)
                out.append(p);
    }
    return out;
}

inline QString translate(const QString &label, bool indonesian)
{
    if (!indonesian)
        return label.toLower();
    static const QHash<QString, QString> id = {{"applause", "tepuk tangan"}, {"clapping", "tepuk tangan"}, {"crowd", "keramaian"}, {"laughter", "tawa"}, {"cheering", "sorak"},
        {"rain", "hujan"}, {"wind", "angin"}, {"thunder", "petir"}, {"traffic noise", "lalu lintas"}, {"vehicle", "kendaraan"}, {"car", "mobil"}, {"engine", "mesin"},
        {"typing", "mengetik"}, {"keyboard", "papan ketik"}, {"dog", "anjing"}, {"bark", "gonggongan"}, {"cat", "kucing"}, {"bird", "burung"}, {"water", "air"},
        {"door", "pintu"}, {"telephone", "telepon"}, {"alarm", "alarm"}, {"siren", "sirene"}, {"cough", "batuk"}, {"baby cry", "tangis bayi"}, {"footsteps", "langkah kaki"}};
    const QString key = label.toLower();
    for (auto it = id.cbegin(); it != id.cend(); ++it)
        if (key.contains(it.key()))
            return it.value();
    return key;
}

// The line of the transcript for an event.
inline QString label(const Event &e, const QString &language)
{
    const bool id = language.isEmpty() || language == QLatin1String("auto") || language.startsWith(QLatin1String("id")); // Indonesian unless the recording is known to be another language
    switch (e.kind) {
    case Kind::Music: return id ? QStringLiteral("♪ Musik") : QStringLiteral("♪ Music");
    case Kind::SpeechOverMusic: return id ? QStringLiteral("♪ Musik latar") : QStringLiteral("♪ Background music");
    case Kind::Ambient: return (id ? QStringLiteral("Suara sekitar: ") : QStringLiteral("Ambient sound: ")) + translate(e.label, id);
    case Kind::Plain: break;
    }
    return {};
}

inline Segment segment(const Event &e, const QString &language) { return Segment{e.start, e.end, label(e, language), Speaker, false}; }

inline bool isSound(const Segment &s) { return s.speaker == Speaker; }

inline QList<Segment> withoutSounds(const QList<Segment> &in)
{
    QList<Segment> out;
    for (const Segment &s : in)
        if (!isSound(s))
            out.append(s);
    return out;
}

// What the recognizer writes for music and other sounds: "[Music]", "(musik)", "♪♪", "[Applause]". Music or another sound with its kind, else
// Plain ("[BLANK_AUDIO]", "(silence)" and the like are nothing).
inline Event fromMark(const QString &text, double start, double end)
{
    static const QRegularExpression mark(QStringLiteral("^[\\s\\[(*♪♫-]*([^\\])*]{0,40}?)[\\s\\]*)♪♫-]*$"));
    const QRegularExpressionMatch m = mark.match(text.trimmed());
    Event e{start, end, Kind::Plain, {}};
    if (!m.hasMatch())
        return e;
    const QString inner = m.captured(1).trimmed().toLower();
    {
        bool notes = false, other = false;
        for (const QChar c : text) {
            if (c == QChar(0x266A) || c == QChar(0x266B))
                notes = true;
            else if (!c.isSpace() && c != QLatin1Char('*') && c != QLatin1Char('-'))
                other = true;
        }
        if (notes && !other) { // only notes: "♪♪"
            e.kind = Kind::Music;
            return e;
        }
    }
    if (inner.isEmpty())
        return e;
    if (containsAny(inner, {"music", "musik", "lagu", "song", "singing", "instrumental"}))
        e.kind = Kind::Music;
    else if (containsAny(inner, {"applause", "tepuk", "clapping"})) {
        e.kind = Kind::Ambient;
        e.label = QStringLiteral("applause");
    } else if (containsAny(inner, {"laughter", "laughs", "tawa"})) {
        e.kind = Kind::Ambient;
        e.label = QStringLiteral("laughter");
    } else if (containsAny(inner, {"cheering", "crowd", "keramaian"})) {
        e.kind = Kind::Ambient;
        e.label = QStringLiteral("crowd");
    }
    return e;
}

// "0: AudioEvent(name=\"Music\", index=137, prob=0.8123)" lines of the tagging program -> tags.
inline QList<Tag> parseTags(const QString &output)
{
    static const QRegularExpression event(QStringLiteral("AudioEvent\\(name=\"([^\"]*)\",\\s*index=\\d+,\\s*prob=([0-9.eE+-]+)\\)"));
    static const QRegularExpression plain(QStringLiteral("^\\s*([0-9]*\\.[0-9]+(?:[eE][+-]?[0-9]+)?)\\s+(\\S.*)$")); // "0.8123 <tab> Music"
    QList<Tag> tags;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (const auto m = event.match(line); m.hasMatch()) {
            tags.append({m.captured(1), m.captured(2).toDouble()});
        } else if (const auto p = plain.match(line); p.hasMatch()) {
            tags.append({p.captured(2).trimmed(), p.captured(1).toDouble()});
        }
    }
    return tags;
}

} // namespace Sound
} // namespace Audio
