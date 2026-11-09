#pragma once

#include <QByteArray>
#include <QList>
#include <QVector>

#include <cmath>

// Cuts a live stream (16 kHz mono 16-bit, one side of a meeting) into utterances for the speech recognizer: an utterance ends after a
// short silence (or, when someone talks on and on, at a pause after 12 s, at the latest at 20 s). Pure; the noise floor follows the room,
// so a fan or a quiet microphone does not make one endless "speech".
namespace Audio {

struct Utterance {
    int side = 0;                 // 0: the microphone (You), 1: the sound of the computer (Others)
    qint64 startMs = 0, endMs = 0;
    QVector<float> samples;
};

class LiveSegmenter
{
public:
    static constexpr int Rate = 16000;
    static constexpr int Frame = 480;       // 30 ms
    static constexpr int EndSilenceMs = 500;
    static constexpr int MinSpeechMs = 300;
    static constexpr int SoftMaxMs = 8000, HardMaxMs = 20000;

    explicit LiveSegmenter(int side = 0) : m_side(side) {}

    // `endMs`: the recording clock when the chunk ended. A gap before it (nothing arrived) counts as silence.
    QList<Utterance> feed(const QByteArray &s16le, qint64 endMs)
    {
        QList<Utterance> out;
        const int n = int(s16le.size() / 2);
        const qint64 startMs = endMs - qint64(n) * 1000 / Rate;
        if (m_posMs >= 0 && startMs - m_posMs > 60) {
            const int gap = int((startMs - m_posMs) * Rate / 1000);
            for (int i = 0; i < gap; ++i)
                push(0.0f, out);
        }
        const auto *p = reinterpret_cast<const qint16 *>(s16le.constData());
        for (int i = 0; i < n; ++i)
            push(float(p[i]) / 32768.0f, out);
        m_posMs = endMs;
        return out;
    }

    // The stream ended: what is being said is closed as an utterance.
    QList<Utterance> flush()
    {
        QList<Utterance> out;
        if (m_inSpeech)
            close(out, m_silent * Frame * 1000 / Rate);
        m_frame.clear();
        return out;
    }

    double level() const { return m_level; }
    // What is being said right now (not closed yet), for a first reading of it while it goes on; empty when nobody speaks.
    const QVector<float> &current() const { return m_cur; }
    bool speaking() const { return m_inSpeech; }
    qint64 currentStartMs() const { return m_curStartMs; }

private:
    void push(float v, QList<Utterance> &out)
    {
        m_frame.append(v);
        ++m_total;
        if (m_frame.size() == Frame) {
            frame(out);
            m_frame.clear();
        }
    }

    void frame(QList<Utterance> &out)
    {
        double e = 0;
        for (const float v : std::as_const(m_frame))
            e += double(v) * v;
        const double db = 10.0 * std::log10(e / Frame + 1e-10);
        m_level = qBound(0.0, (db + 60.0) / 50.0, 1.0);
        const double threshold = qMax(-48.0, m_floor + 10.0);
        const bool loud = db > threshold;
        if (!loud && !m_inSpeech)
            m_floor = qMin(-35.0, m_floor + 0.05 * (db - m_floor)); // the room, learnt from what is not speech
        const qint64 frameEnd = m_total * 1000 / Rate;

        if (!m_inSpeech) {
            m_pre.append(m_frame);
            if (m_pre.size() > 6 * Frame)
                m_pre.remove(0, m_pre.size() - 6 * Frame);
            m_run = loud ? m_run + 1 : 0;
            if (m_run >= 2) { // 60 ms of sound: someone speaks
                m_inSpeech = true;
                m_cur = m_pre;
                m_curStartMs = frameEnd - qint64(m_cur.size()) * 1000 / Rate;
                m_speechFrames = m_run;
                m_silent = 0;
                m_pre.clear();
                m_run = 0;
            }
            return;
        }
        m_cur.append(m_frame);
        if (loud) {
            ++m_speechFrames;
            m_silent = 0;
        } else {
            ++m_silent;
        }
        const int lengthMs = int(qint64(m_cur.size()) * 1000 / Rate);
        if (m_silent * Frame * 1000 / Rate >= EndSilenceMs || lengthMs >= HardMaxMs || (lengthMs >= SoftMaxMs && m_silent >= 5))
            close(out, m_silent * Frame * 1000 / Rate);
    }

    void close(QList<Utterance> &out, int trailingMs)
    {
        // keep 200 ms of the silence after the words
        const int cut = qMax(0, trailingMs - 200);
        const int drop = int(qint64(cut) * Rate / 1000);
        if (drop > 0 && drop < m_cur.size())
            m_cur.resize(m_cur.size() - drop);
        if (m_speechFrames * Frame * 1000 / Rate >= MinSpeechMs) {
            Utterance u;
            u.side = m_side;
            u.startMs = m_curStartMs;
            u.endMs = m_curStartMs + qint64(m_cur.size()) * 1000 / Rate;
            u.samples = m_cur;
            out.append(u);
        }
        m_cur.clear();
        m_inSpeech = false;
        m_silent = 0;
        m_speechFrames = 0;
        m_run = 0;
    }

    int m_side;
    QVector<float> m_frame, m_pre, m_cur;
    double m_floor = -55.0, m_level = 0;
    bool m_inSpeech = false;
    int m_run = 0, m_silent = 0, m_speechFrames = 0;
    qint64 m_total = 0, m_curStartMs = 0, m_posMs = -1;
};

// Too much waiting for the recognizer: the oldest utterances are left out of the live view (the recording has them all). Returns how many.
inline int dropOverflow(QList<Utterance> &queue, double maxSeconds)
{
    double total = 0;
    for (const Utterance &u : std::as_const(queue))
        total += double(u.samples.size()) / LiveSegmenter::Rate;
    int dropped = 0;
    while (total > maxSeconds && queue.size() > 1) {
        total -= double(queue.first().samples.size()) / LiveSegmenter::Rate;
        queue.removeFirst();
        ++dropped;
    }
    return dropped;
}

} // namespace Audio
