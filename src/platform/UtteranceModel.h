#pragma once

#include "platform/VoiceModel.h"

#include <QtGlobal>

// One spoken command in a stream of microphone chunks, without any device or timer so tests can drive it: it waits for a
// voice, follows it, and ends when the speaker has been quiet for a moment (or has talked for too long). A click is not
// a command, and nobody saying anything gives up after a while.
namespace UtteranceModel {

enum class State { Waiting, Speaking, Ended, NoSpeech };

struct Limits {
    qint64 quietMs = 900;     // this long without a voice ends the command
    qint64 maxMs = 12000;     // a command never runs longer than this
    qint64 minSpeechMs = 300; // less than this of voice is a click or a cough
    qint64 waitMs = 6000;     // no voice at all for this long: nobody is talking
};

class Tracker {
public:
    Limits limits;
    int sensitivity = 1;

    State state() const { return m_state; }
    bool done() const { return m_state == State::Ended || m_state == State::NoSpeech; }
    void reset(qint64 nowMs)
    {
        m_detector.reset();
        m_detector.level = sensitivity;
        m_state = State::Waiting;
        m_began = nowMs;
        m_speechStart = m_lastVoice = -1;
    }

    // One chunk's RMS at `nowMs` (the chunks come steadily; the time is the caller's clock).
    State update(double chunkRms, qint64 nowMs)
    {
        if (done())
            return m_state;
        m_detector.level = sensitivity;
        const bool voice = m_detector.update(chunkRms, nowMs);
        if (voice) {
            if (m_speechStart < 0)
                m_speechStart = nowMs;
            m_lastVoice = nowMs;
            m_state = State::Speaking;
        }
        if (m_state == State::Waiting) {
            if (nowMs - m_began >= limits.waitMs)
                m_state = State::NoSpeech;
            return m_state;
        }
        // Speaking, or a voice that has gone quiet again
        if (nowMs - m_speechStart >= limits.maxMs) {
            m_state = State::Ended;
        } else if (!voice && nowMs - m_lastVoice >= limits.quietMs) {
            m_state = m_lastVoice - m_speechStart >= limits.minSpeechMs ? State::Ended : State::NoSpeech;
            if (m_state == State::NoSpeech) { // a click: listen on rather than give up (until the wait runs out)
                m_state = State::Waiting;
                m_speechStart = m_lastVoice = -1;
            }
        }
        return m_state;
    }

private:
    VoiceModel::Detector m_detector;
    State m_state = State::Waiting;
    qint64 m_began = 0, m_speechStart = -1, m_lastVoice = -1;
};

}
