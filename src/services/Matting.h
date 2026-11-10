#pragma once

#include <QImage>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <functional>

// Taking the background off a picture with a saliency model (U2-Net, IS-Net): the picture is scaled to what the model reads and turned into numbers,
// the model answers with a map of how much each point belongs to the main subject, and that map becomes the transparency of the picture.
// This file is everything but the model itself, which is a `Runner` (ONNX Runtime in the program, a made-up one in the tests). Pure.
namespace Matting {

struct Profile {
    QString id;               // the catalog id of the model
    int size = 320;           // the model reads a square of this many pixels
    float mean[3] = {0.485f, 0.456f, 0.406f};
    float stdev[3] = {0.229f, 0.224f, 0.225f};
    bool maxNormalize = true; // divide by the picture's largest value first (U2-Net, as rembg does) instead of by 255
    qint64 memory = 0;        // bytes of RAM it asks for while it runs (rough)
};

// The models offered. U2-Net is fed the way rembg feeds it (divided by the picture's largest value, then (x - mean) / std); IS-Net the way the
// publisher of its ONNX file does (x - 128) / 256 on the 0..255 values.
inline Profile profileFor(const QString &modelId)
{
    Profile p;
    p.id = modelId;
    if (modelId == QLatin1String("matting-isnet")) {
        p.size = 1024;
        p.mean[0] = p.mean[1] = p.mean[2] = 128.0f / 255.0f;
        p.stdev[0] = p.stdev[1] = p.stdev[2] = 256.0f / 255.0f;
        p.maxNormalize = false;
        p.memory = Q_INT64_C(3) << 30;
    } else { // matting-u2netp
        p.memory = Q_INT64_C(1) << 29;
    }
    return p;
}

struct Options {
    int featherPx = 1;        // softens the edge of the cut, 0..3 pixels of the picture
    int maxSide = 4096;       // a larger picture is made smaller first
};

// What a model run is: the numbers in (channels first: 3 × h × w), the map out (oh × ow, any range: it is normalised afterwards). "" error on success.
using Runner = std::function<bool(const QVector<float> &chw, int h, int w, QVector<float> *out, int *oh, int *ow, QString *error)>;

// The picture as numbers for the model: scaled to size × size, divided by its largest value (or by 255), each channel (x - mean) / std.
inline QVector<float> preprocess(const QImage &image, const Profile &p)
{
    const QImage rgb = image.scaled(p.size, p.size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    const int n = p.size * p.size;
    QVector<float> out(3 * n);
    int largest = 255;
    if (p.maxNormalize) {
        largest = 1;
        for (int y = 0; y < p.size; ++y) {
            const uchar *line = rgb.constScanLine(y);
            for (int i = 0; i < p.size * 3; ++i)
                largest = std::max<int>(largest, line[i]);
        }
    }
    for (int y = 0; y < p.size; ++y) {
        const uchar *line = rgb.constScanLine(y);
        for (int x = 0; x < p.size; ++x)
            for (int c = 0; c < 3; ++c)
                out[c * n + y * p.size + x] = (float(line[x * 3 + c]) / float(largest) - p.mean[c]) / p.stdev[c];
    }
    return out;
}

// A box blur of a grey map, `radius` pixels (separable).
inline QImage blurred(const QImage &gray, int radius)
{
    if (radius <= 0)
        return gray;
    const int w = gray.width(), h = gray.height();
    QImage tmp(w, h, QImage::Format_Grayscale8), out(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        const uchar *src = gray.constScanLine(y);
        uchar *dst = tmp.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int sum = 0, count = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int xx = x + k;
                if (xx >= 0 && xx < w) { sum += src[xx]; ++count; }
            }
            dst[x] = uchar(sum / count);
        }
    }
    for (int y = 0; y < h; ++y) {
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int sum = 0, count = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int yy = y + k;
                if (yy >= 0 && yy < h) { sum += tmp.constScanLine(yy)[x]; ++count; }
            }
            dst[x] = uchar(sum / count);
        }
    }
    return out;
}

// The model's map as the transparency of `original` (same size, ARGB): the map is stretched to 0..1, scaled up smoothly, its edge softened.
inline QImage postprocess(const QImage &original, const QVector<float> &map, int oh, int ow, const Options &o = {})
{
    if (map.size() < oh * ow || oh < 1 || ow < 1)
        return {};
    const auto [lo, hi] = std::minmax_element(map.begin(), map.begin() + oh * ow);
    const float range = *hi - *lo;
    QImage matte(ow, oh, QImage::Format_Grayscale8);
    for (int y = 0; y < oh; ++y) {
        uchar *line = matte.scanLine(y);
        for (int x = 0; x < ow; ++x)
            line[x] = range > 1e-6f ? uchar(std::lround(std::clamp((map[y * ow + x] - *lo) / range, 0.0f, 1.0f) * 255.0f)) : uchar(0);
    }
    matte = blurred(matte.scaled(original.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation), std::clamp(o.featherPx, 0, 3));
    QImage out = original.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const uchar *a = matte.constScanLine(y);
        for (int x = 0; x < out.width(); ++x)
            line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), a[x]);
    }
    return out;
}

// The whole of it: "" and *result set on success, else what went wrong. `image` larger than maxSide is made smaller first.
inline QString cutout(const QImage &image, const Profile &profile, const Runner &run, const Options &options, QImage *result)
{
    if (image.isNull() || image.width() < 8 || image.height() < 8)
        return QStringLiteral("the picture is too small or cannot be read");
    QImage source = image;
    const int side = std::max(image.width(), image.height());
    if (side > options.maxSide)
        source = image.scaled(options.maxSide, options.maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QVector<float> map;
    int oh = 0, ow = 0;
    QString error;
    if (!run(preprocess(source, profile), profile.size, profile.size, &map, &oh, &ow, &error))
        return error.isEmpty() ? QStringLiteral("the model could not read the picture") : error;
    *result = postprocess(source, map, oh, ow, options);
    return result->isNull() ? QStringLiteral("the model gave no answer") : QString();
}

} // namespace Matting
