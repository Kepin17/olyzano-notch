#pragma once

#include <QByteArray>
#include <QtGlobal>
#include <cmath>

// Is someone speaking into the microphone? From short chunks of the sound, without any device or timer so tests can
// drive it. A fixed loudness line fails on a hot or noisy microphone (the room itself is above it, so the icon would
// stay on), so the line follows the room: a slow estimate of the background level, and a voice is a level clearly above
// it, twice in a row (a click is not a voice), held for a moment so it does not flicker between words.
namespace VoiceModel {

struct Sensitivity { double times, plus, least; };

// 0 low, 1 normal, 2 high: how far above the background a level must be (the RMS of 16-bit samples, 0..32768).
inline Sensitivity sensitivity(int level)
{
    switch (level) {
    case 0: return {3.5, 900, 2200};
    case 2: return {1.8, 350, 900};
    default: return {2.5, 600, 1500};
    }
}

// The RMS of 16-bit little-endian mono samples.
inline double rms(const QByteArray &s16le)
{
    const int n = s16le.size() / 2;
    if (n <= 0)
        return 0;
    const auto *p = reinterpret_cast<const qint16 *>(s16le.constData());
    double sum = 0;
    for (int i = 0; i < n; ++i)
        sum += double(p[i]) * p[i];
    return std::sqrt(sum / n);
}

class Detector {
public:
    int level = 1;           // sensitivity()
    qint64 holdMs = 300;

    double floor() const { return m_floor; }
    double threshold() const
    {
        const Sensitivity s = sensitivity(level);
        return std::fmax(s.least, m_floor * s.times + s.plus);
    }
    void reset() { m_floor = 0; m_run = 0; m_lastVoice = -1; m_started = false; }

    // One chunk's level at `nowMs`; true while a voice is on.
    bool update(double chunkRms, qint64 nowMs)
    {
        if (!m_started) {
            m_floor = chunkRms;
            m_started = true;
        }
        const bool above = chunkRms > threshold();
        if (chunkRms < m_floor)
            m_floor = chunkRms;                               // the room got quieter: follow at once
        else if (!above)
            m_floor += (chunkRms - m_floor) * 0.02;           // the room got louder: follow slowly; a voice never raises it
        m_run = above ? m_run + 1 : 0;
        if (m_run >= 2)
            m_lastVoice = nowMs;
        return m_lastVoice >= 0 && nowMs - m_lastVoice < holdMs;
    }

private:
    double m_floor = 0;
    int m_run = 0;
    qint64 m_lastVoice = -1;
    bool m_started = false;
};

}
