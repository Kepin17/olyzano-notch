#pragma once

#include <QList>
#include <QVector>

#include <algorithm>
#include <cmath>

// Voice activity by energy, pure and fast: where there is speech, and how to cut a long recording into windows for the speech
// recognizer that end in silence, not in the middle of a word. (A neural VAD can replace it later behind the same functions.)
namespace Vad {

struct Region {
    qint64 start = 0, end = 0; // samples [start, end)
    qint64 length() const { return end - start; }
};

// Frames of `frameMs`, loud ones (above a threshold that follows the noise floor of this very recording) form regions; gaps shorter than
// `mergeMs` close up, regions shorter than `minMs` go away, and each region is padded by `padMs`.
inline QList<Region> speech(const QVector<float> &x, int rate, int frameMs = 30, int mergeMs = 500, int minMs = 200, int padMs = 150)
{
    QList<Region> out;
    const int frame = qMax(1, rate * frameMs / 1000);
    const int frames = int(x.size() / frame);
    if (frames <= 0)
        return out;
    QVector<float> db(frames);
    for (int f = 0; f < frames; ++f) {
        double e = 0;
        for (int i = 0; i < frame; ++i) {
            const double v = x[f * frame + i];
            e += v * v;
        }
        db[f] = float(10.0 * std::log10(e / frame + 1e-10));
    }
    QVector<float> sorted = db;
    std::sort(sorted.begin(), sorted.end());
    const float floorDb = sorted[sorted.size() / 10];                  // quiet: the 10th percentile
    const float peakDb = sorted[sorted.size() * 95 / 100];             // loud: the 95th
    if (peakDb - floorDb < 6.0f) { // no real difference between loud and quiet: all silence, or one steady sound (taken as speech)
        if (peakDb > -35.0f)
            out.append({0, qint64(frames) * frame});
        return out;
    }
    const float threshold = qMax(floorDb + 0.35f * (peakDb - floorDb), -50.0f);
    const int mergeFrames = qMax(1, mergeMs / frameMs), minFrames = qMax(1, minMs / frameMs);
    int start = -1, quiet = 0;
    QList<QPair<int, int>> raw;
    for (int f = 0; f < frames; ++f) {
        if (db[f] >= threshold) {
            if (start < 0)
                start = f;
            quiet = 0;
        } else if (start >= 0 && ++quiet > mergeFrames) {
            raw.append({start, f - quiet + 1});
            start = -1;
            quiet = 0;
        }
    }
    if (start >= 0)
        raw.append({start, frames - quiet});
    const qint64 pad = qint64(rate) * padMs / 1000;
    for (const auto &r : raw) {
        if (r.second - r.first < minFrames)
            continue;
        out.append({qMax<qint64>(0, qint64(r.first) * frame - pad), qMin<qint64>(x.size(), qint64(r.second) * frame + pad)});
    }
    // padding may make neighbours touch
    for (int i = 1; i < out.size();) {
        if (out[i].start <= out[i - 1].end) {
            out[i - 1].end = qMax(out[i - 1].end, out[i].end);
            out.removeAt(i);
        } else {
            ++i;
        }
    }
    return out;
}

// Windows of at most `maxSeconds` that hold the speech: neighbouring regions are packed together while they fit; a region that is longer
// than the window is cut where the signal is quietest near the end of the window. Together the windows cover all of the speech and no
// window is longer than the limit.
inline QList<Region> windows(const QVector<float> &x, int rate, double maxSeconds = 25.0)
{
    QList<Region> out;
    const qint64 maxLen = qint64(maxSeconds * rate);
    const QList<Region> regions = speech(x, rate);
    auto quietest = [&](qint64 from, qint64 to) { // the quietest 50 ms between from and to
        const qint64 step = qMax<qint64>(1, rate / 20);
        qint64 best = to;
        double bestE = 1e30;
        for (qint64 p = from; p + step <= to; p += step) {
            double e = 0;
            for (qint64 i = p; i < p + step; ++i)
                e += double(x[int(i)]) * x[int(i)];
            if (e < bestE) {
                bestE = e;
                best = p + step / 2;
            }
        }
        return best;
    };
    Region cur{-1, -1};
    auto flush = [&] {
        if (cur.start >= 0)
            out.append(cur);
        cur = {-1, -1};
    };
    for (Region r : regions) {
        while (r.length() > maxLen) { // one long stretch of speech: cut it near the end of a window
            flush();
            const qint64 cut = quietest(r.start + maxLen * 7 / 10, r.start + maxLen);
            out.append({r.start, cut});
            r.start = cut;
        }
        if (cur.start < 0) {
            cur = r;
        } else if (r.end - cur.start <= maxLen) {
            cur.end = r.end;
        } else {
            flush();
            cur = r;
        }
    }
    flush();
    return out;
}

} // namespace Vad
