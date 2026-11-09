#pragma once

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>

// Who said what: the segments the speech recognizer found are matched with the turns the diarizer (or the two channels of a recording)
// found. Pure and tested. Times are seconds.
namespace Audio {

struct Segment {
    double start = 0, end = 0;
    QString text, speaker;
    bool approx = false; // the speaker changed inside it and its text was split by length, not by the words said
};

struct Turn {
    double start = 0, end = 0;
    QString speaker;
};

namespace detail {
inline double overlap(double a0, double a1, double b0, double b1) { return std::max(0.0, std::min(a1, b1) - std::max(a0, b0)); }
}

// Each segment gets the speaker who talks longest during it. When someone else talks for a good part of it, it is cut into one piece per
// speaker, the words shared out by how long each spoke (marked approx: the real boundary is somewhere nearby). A segment no turn touches takes
// the nearest turn within 2 s, else the speaker before it.
inline QList<Segment> align(const QList<Segment> &asr, QList<Turn> turns)
{
    std::sort(turns.begin(), turns.end(), [](const Turn &a, const Turn &b) { return a.start < b.start; });
    QList<Segment> out;
    QString last = turns.isEmpty() ? QStringLiteral("SPEAKER_00") : turns.first().speaker;
    for (const Segment &s : asr) {
        const double len = std::max(1e-3, s.end - s.start);
        // the runs of speakers inside the segment, in order
        struct Run { QString speaker; double from, to; };
        QList<Run> runs;
        QHash<QString, double> total;
        for (const Turn &t : std::as_const(turns)) {
            if (t.start > s.end)
                break;
            const double o = detail::overlap(s.start, s.end, t.start, t.end);
            if (o <= 0 && !(s.end - s.start < 1e-3 && t.start <= s.start && t.end >= s.start))
                continue;
            const double from = std::max(s.start, t.start), to = std::min(s.end, t.end);
            if (!runs.isEmpty() && runs.last().speaker == t.speaker)
                runs.last().to = std::max(runs.last().to, to);
            else
                runs.append({t.speaker, from, to});
            total[t.speaker] += o;
        }
        Segment base = s;
        if (runs.isEmpty()) {
            double best = 2.0;
            QString closest = last;
            for (const Turn &t : std::as_const(turns)) {
                const double gap = t.end < s.start ? s.start - t.end : t.start > s.end ? t.start - s.end : 0;
                if (gap < best) {
                    best = gap;
                    closest = t.speaker;
                }
            }
            base.speaker = closest;
            last = closest;
            out.append(base);
            continue;
        }
        QString top;
        double topT = -1;
        for (auto it = total.cbegin(); it != total.cend(); ++it)
            if (it.value() > topT || (qFuzzyCompare(it.value(), topT) && it.key() < top)) {
                topT = it.value();
                top = it.key();
            }
        // short interjections (< 0.4 s) do not split a segment
        QList<Run> solid;
        for (const Run &r : std::as_const(runs)) {
            if (r.to - r.from < 0.4 && !solid.isEmpty())
                solid.last().to = std::max(solid.last().to, r.to);
            else if (!solid.isEmpty() && solid.last().speaker == r.speaker)
                solid.last().to = std::max(solid.last().to, r.to);
            else
                solid.append(r);
        }
        const QStringList words = s.text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (solid.size() <= 1 || topT >= 0.8 * len || words.size() < solid.size()) {
            base.speaker = top;
            last = top;
            out.append(base);
            continue;
        }
        // split: words in proportion to the time each run covers
        double covered = 0;
        for (const Run &r : std::as_const(solid))
            covered += r.to - r.from;
        int used = 0;
        for (int i = 0; i < solid.size(); ++i) {
            const bool lastRun = i == solid.size() - 1;
            int n = lastRun ? int(words.size()) - used : int(std::lround(words.size() * (solid[i].to - solid[i].from) / std::max(covered, 1e-6)));
            n = lastRun ? n : std::clamp(n, 1, int(words.size()) - used - (int(solid.size()) - 1 - i));
            if (n <= 0)
                continue;
            Segment piece;
            piece.start = i == 0 ? s.start : solid[i].from;
            piece.end = lastRun ? s.end : solid[i].to;
            piece.text = words.mid(used, n).join(u' ');
            piece.speaker = solid[i].speaker;
            piece.approx = true;
            used += n;
            out.append(piece);
            last = solid[i].speaker;
        }
    }
    return out;
}

// For a stereo recording from Notch (microphone left, the computer's sound right): the stretches where the microphone is clearly louder than
// the computer's sound are the person at the microphone ("You"). Headphones keep the remote voices out of the microphone; with speakers they
// leak in, which is why a channel must beat the other by `dominance` times.
inline QList<Turn> micTurns(const QVector<float> &left, const QVector<float> &right, int rate, const QString &label, double dominance = 1.6,
                            double mergeSeconds = 0.7, double minSeconds = 0.3)
{
    QList<Turn> out;
    const int frame = std::max(1, rate / 50); // 20 ms
    const int n = int(std::min(left.size(), right.size()) / frame);
    auto energy = [&](const QVector<float> &x, int f) {
        double e = 0;
        for (int i = 0; i < frame; ++i) {
            const double v = x[f * frame + i];
            e += v * v;
        }
        return e / frame;
    };
    QVector<double> el(n), er(n);
    QVector<double> sorted;
    for (int f = 0; f < n; ++f) {
        el[f] = energy(left, f);
        er[f] = energy(right, f);
        sorted.append(el[f]);
    }
    if (n == 0)
        return out;
    std::sort(sorted.begin(), sorted.end());
    const double floorE = sorted[sorted.size() / 10], peakE = sorted[sorted.size() * 95 / 100];
    const double gate = std::max(floorE * 4, peakE * 0.02);
    int start = -1, quiet = 0;
    const int mergeFrames = std::max(1, int(mergeSeconds * 50));
    auto close = [&](int endFrame) {
        if ((endFrame - start) / 50.0 >= minSeconds)
            out.append({start / 50.0, endFrame / 50.0, label});
        start = -1;
    };
    for (int f = 0; f < n; ++f) {
        const bool mine = el[f] > gate && el[f] > dominance * er[f];
        if (mine) {
            if (start < 0)
                start = f;
            quiet = 0;
        } else if (start >= 0 && ++quiet > mergeFrames) {
            close(f - quiet + 1);
            quiet = 0;
        }
    }
    if (start >= 0)
        close(n - quiet);
    return out;
}

// A voice that is heard for only a moment (a cough, a laugh, a bit of noise taken for another person) is not a speaker: its turns go to the
// nearest turn of a real one, and the speakers are numbered again in the order they first talk (SPEAKER_00, SPEAKER_01 ...). A speaker is
// "real" when heard for at least `minSeconds` and `minShare` of all the speech. With nobody real, the turns are left as they are.
inline QList<Turn> mergeTinySpeakers(QList<Turn> turns, double minSeconds = 3.0, double minShare = 0.05)
{
    std::sort(turns.begin(), turns.end(), [](const Turn &a, const Turn &b) { return a.start < b.start; });
    QHash<QString, double> total;
    double all = 0;
    for (const Turn &t : std::as_const(turns)) {
        total[t.speaker] += t.end - t.start;
        all += t.end - t.start;
    }
    const double limit = std::max(minSeconds, minShare * all);
    QSet<QString> real;
    for (auto it = total.cbegin(); it != total.cend(); ++it)
        if (it.value() >= limit)
            real.insert(it.key());
    if (real.isEmpty() || real.size() == total.size()) {
        // nothing to merge; still numbered in order, so a label means the same thing everywhere
    } else {
        for (int i = 0; i < turns.size(); ++i) {
            if (real.contains(turns[i].speaker))
                continue;
            double best = 1e18;
            QString to;
            for (int j = 0; j < turns.size(); ++j) {
                if (!real.contains(turns[j].speaker))
                    continue;
                const double gap = turns[j].end < turns[i].start ? turns[i].start - turns[j].end : turns[j].start > turns[i].end ? turns[j].start - turns[i].end : 0;
                if (gap < best) {
                    best = gap;
                    to = turns[j].speaker;
                }
            }
            turns[i].speaker = to;
        }
    }
    QStringList order;
    for (const Turn &t : std::as_const(turns))
        if (!order.contains(t.speaker))
            order << t.speaker;
    for (Turn &t : turns)
        t.speaker = QStringLiteral("SPEAKER_%1").arg(order.indexOf(t.speaker), 2, 10, QLatin1Char('0'));
    return turns;
}

} // namespace Audio
