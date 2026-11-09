#pragma once

#include <QtGlobal>

namespace Audio {

// A source that delivers nothing while it is silent (what Windows does for the sound of the computer) would, written chunk after chunk,
// squeeze every pause out of the recording and shift what follows earlier. So before a chunk is written, the file is brought up to where the
// clock says it should be: this many samples of silence go in first (0 when the file is on time, or no more than `toleranceMs` behind).
inline qint64 silenceBefore(qint64 samplesWritten, qint64 chunkSamples, qint64 elapsedMs, int rate, int toleranceMs = 300)
{
    const qint64 expectedEnd = elapsedMs * rate / 1000;       // where the chunk's last sample falls on the clock
    const qint64 behind = expectedEnd - (samplesWritten + chunkSamples);
    return behind > qint64(toleranceMs) * rate / 1000 ? behind : 0;
}

// the loudest sample of 16-bit little-endian PCM, 0..1
inline double peakOf(const char *s16le, int bytes)
{
    qint16 top = 0;
    for (int i = 0; i + 1 < bytes; i += 2) {
        const qint16 v = qint16(quint8(s16le[i]) | (quint8(s16le[i + 1]) << 8));
        top = qMax<qint16>(top, qint16(v < 0 ? (v == -32768 ? 32767 : -v) : v));
    }
    return top / 32768.0;
}

} // namespace Audio
