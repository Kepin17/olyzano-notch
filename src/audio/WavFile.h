#pragma once

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>
#include <QVector>
#include <QtEndian>

#include <algorithm>
#include <cmath>

// WAV files for Audio Intelligence: what ffmpeg makes (16 kHz mono PCM) and what the recorder writes (stereo: the microphone on the
// left, the sound of the computer on the right). Pure, so it is tested without audio hardware. Only 16-bit PCM and 32-bit float.
namespace Wav {

struct Info {
    int channels = 0;
    int rate = 0;
    int bits = 0;
    bool isFloat = false;
    qint64 dataOffset = 0;
    qint64 dataBytes = 0;
    qint64 frames() const { return channels > 0 && bits > 0 ? dataBytes / (channels * (bits / 8)) : 0; }
    double seconds() const { return rate > 0 ? double(frames()) / rate : 0; }
    bool valid() const { return channels > 0 && rate > 0 && (bits == 16 || (bits == 32 && isFloat)); }
};

// The header of a RIFF/WAVE file: walks its chunks (not "44 bytes in"), accepts WAVE_FORMAT_EXTENSIBLE, copes with a data chunk that
// says 0 or too much (a file that was cut off while recording): the size is then what is really there.
inline Info readInfo(QFile &f)
{
    Info info;
    f.seek(0);
    const QByteArray head = f.read(12);
    if (head.size() < 12 || head.left(4) != "RIFF" || head.mid(8, 4) != "WAVE")
        return {};
    int tag = 0;
    while (!f.atEnd()) {
        const QByteArray ch = f.read(8);
        if (ch.size() < 8)
            break;
        const QByteArray id = ch.left(4);
        const quint32 size = qFromLittleEndian<quint32>(ch.constData() + 4);
        if (id == "fmt ") {
            const QByteArray fmt = f.read(qMin<qint64>(size, 40));
            if (fmt.size() < 16)
                return {};
            tag = qFromLittleEndian<quint16>(fmt.constData());
            info.channels = qFromLittleEndian<quint16>(fmt.constData() + 2);
            info.rate = int(qFromLittleEndian<quint32>(fmt.constData() + 4));
            info.bits = qFromLittleEndian<quint16>(fmt.constData() + 14);
            if (tag == 0xFFFE && fmt.size() >= 26) // extensible: the real format is in the sub-format
                tag = qFromLittleEndian<quint16>(fmt.constData() + 24);
            info.isFloat = tag == 3;
            f.seek(f.pos() + (size - fmt.size()) + (size & 1));
        } else if (id == "data") {
            info.dataOffset = f.pos();
            const qint64 left = f.size() - f.pos();
            info.dataBytes = (size == 0 || qint64(size) > left) ? left : qint64(size);
            break;
        } else {
            f.seek(f.pos() + size + (size & 1));
        }
    }
    if (tag != 1 && tag != 3)
        return {};
    return info.valid() && info.dataOffset > 0 ? info : Info();
}

inline Info readInfo(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? readInfo(f) : Info();
}

// `frames` frames from `start`, one channel as floats in [-1, 1] (-1: the channels averaged). Empty when the file cannot be read.
inline QVector<float> readFrames(const QString &path, int channel = -1, qint64 start = 0, qint64 frames = -1)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const Info info = readInfo(f);
    if (!info.valid() || channel >= info.channels)
        return {};
    const qint64 total = info.frames();
    start = qBound<qint64>(0, start, total);
    if (frames < 0 || start + frames > total)
        frames = total - start;
    const int bytes = info.bits / 8, stride = info.channels * bytes;
    QVector<float> out;
    out.reserve(int(qMin<qint64>(frames, Q_INT64_C(200000000))));
    f.seek(info.dataOffset + start * stride);
    constexpr qint64 Block = 1 << 16;
    for (qint64 done = 0; done < frames;) {
        const qint64 n = qMin(Block, frames - done);
        const QByteArray raw = f.read(n * stride);
        const qint64 got = raw.size() / stride;
        if (got <= 0)
            break;
        for (qint64 i = 0; i < got; ++i) {
            float sum = 0;
            for (int c = 0; c < info.channels; ++c) {
                if (channel >= 0 && c != channel)
                    continue;
                const char *p = raw.constData() + i * stride + c * bytes;
                sum += info.isFloat ? [&] { float v; memcpy(&v, p, 4); return v; }() : float(qFromLittleEndian<qint16>(p)) / 32768.0f;
            }
            out.append(channel >= 0 ? sum : sum / info.channels);
        }
        done += got;
    }
    return out;
}

inline QByteArray header(int channels, int rate, qint64 dataBytes)
{
    QByteArray h;
    h.reserve(44);
    auto u16 = [&h](quint16 v) { h.append(char(v & 0xFF)).append(char(v >> 8)); };
    auto u32 = [&](quint32 v) { u16(v & 0xFFFF); u16(v >> 16); };
    h.append("RIFF");
    u32(quint32(qMin<qint64>(36 + dataBytes, 0xFFFFFFFFLL)));
    h.append("WAVEfmt ");
    u32(16);
    u16(1); // PCM
    u16(quint16(channels));
    u32(quint32(rate));
    u32(quint32(rate * channels * 2));
    u16(quint16(channels * 2));
    u16(16);
    h.append("data");
    u32(quint32(qMin<qint64>(dataBytes, 0xFFFFFFFFLL)));
    return h;
}

// Interleaved 16-bit samples -> a WAV file. "" on success, else why not.
inline QString writePcm16(const QString &path, const QVector<qint16> &interleaved, int channels, int rate)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("cannot write %1").arg(path);
    const QByteArray data(reinterpret_cast<const char *>(interleaved.constData()), int(interleaved.size() * 2));
    if (f.write(header(channels, rate, data.size())) < 0 || f.write(data) != data.size())
        return QStringLiteral("cannot write %1").arg(path);
    return {};
}

// Two raw mono s16le files (the microphone and the computer's sound, which never end on exactly the same sample) -> one stereo WAV,
// left = the first, right = the second; the shorter one is padded with silence. "" on success.
inline QString interleaveRaw(const QString &leftPath, const QString &rightPath, const QString &out, int rate)
{
    QFile l(leftPath), r(rightPath), o(out);
    if (!l.open(QIODevice::ReadOnly) || !r.open(QIODevice::ReadOnly))
        return QStringLiteral("cannot read the recorded audio");
    if (!o.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("cannot write %1").arg(out);
    const qint64 frames = qMax(l.size(), r.size()) / 2;
    if (o.write(header(2, rate, frames * 4)) < 0)
        return QStringLiteral("cannot write %1").arg(out);
    constexpr qint64 Block = 1 << 15; // frames
    for (qint64 done = 0; done < frames; done += Block) {
        const qint64 n = qMin(Block, frames - done);
        QByteArray a = l.read(n * 2), b = r.read(n * 2);
        a.resize(int(n * 2)); // read() may give less at the end: the rest is zero (resize fills with garbage, so clear it)
        b.resize(int(n * 2));
        QByteArray mix(int(n * 4), 0);
        const qint64 ga = qMin<qint64>(l.size() - done * 2, n * 2), gb = qMin<qint64>(r.size() - done * 2, n * 2);
        for (qint64 i = 0; i < n; ++i) {
            const bool hasA = i * 2 + 1 < ga, hasB = i * 2 + 1 < gb;
            mix[int(i * 4)] = hasA ? a[int(i * 2)] : 0;
            mix[int(i * 4 + 1)] = hasA ? a[int(i * 2 + 1)] : 0;
            mix[int(i * 4 + 2)] = hasB ? b[int(i * 2)] : 0;
            mix[int(i * 4 + 3)] = hasB ? b[int(i * 2 + 1)] : 0;
        }
        if (o.write(mix) != mix.size())
            return QStringLiteral("cannot write %1").arg(out);
    }
    return {};
}

// `bins` loudness peaks (0..1) across the whole signal, for a waveform. The largest bin is 1 unless the signal is silent.
inline QVector<float> peaks(const QVector<float> &mono, int bins)
{
    QVector<float> out(qMax(1, bins), 0.0f);
    if (mono.isEmpty())
        return out;
    const double per = double(mono.size()) / out.size();
    float top = 0;
    for (int b = 0; b < out.size(); ++b) {
        const qint64 from = qint64(b * per), to = qMin<qint64>(mono.size(), qint64((b + 1) * per) + 1);
        float m = 0;
        for (qint64 i = from; i < to; ++i)
            m = std::max(m, std::fabs(mono[int(i)]));
        out[b] = m;
        top = std::max(top, m);
    }
    if (top > 0)
        for (float &v : out)
            v /= top;
    return out;
}

} // namespace Wav
