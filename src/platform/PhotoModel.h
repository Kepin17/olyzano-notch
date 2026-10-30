#pragma once

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QString>
#include <algorithm>

// The photo booth's pictures, made from the frames the camera took: where each frame goes in each template, the filters, and the
// composition. Pure (QtGui only), so the tests can run it on plain coloured images.
namespace PhotoModel {

enum class Template { Single, Strip, Polaroid, Frame };
enum class Filter { Normal, Mono, Warm, Vivid };

inline Template templateOf(const QString &name)
{
    return name == QLatin1String("strip") ? Template::Strip : name == QLatin1String("polaroid") ? Template::Polaroid : name == QLatin1String("frame") ? Template::Frame : Template::Single;
}
inline Filter filterOf(const QString &name)
{
    return name == QLatin1String("mono") ? Filter::Mono : name == QLatin1String("warm") ? Filter::Warm : name == QLatin1String("vivid") ? Filter::Vivid : Filter::Normal;
}

// How many camera frames a template needs.
inline int shots(Template t) { return t == Template::Strip ? 4 : 1; }

struct Layout {
    QSize canvas;
    QList<QRect> cells; // where each frame goes, in order
    QRect caption;      // where the date and name go (empty: none)
};

inline Layout layout(Template t)
{
    Layout l;
    switch (t) {
    case Template::Single:
        l.canvas = {1280, 960};
        l.cells = {QRect(0, 0, 1280, 960)};
        break;
    case Template::Strip: { // four 4:3 pictures one under the other, like a booth's strip
        const int w = 560, h = 420, gap = 30, margin = 40;
        l.canvas = {w + 2 * margin, margin + 4 * h + 3 * gap + margin + 150};
        for (int i = 0; i < 4; ++i)
            l.cells << QRect(margin, margin + i * (h + gap), w, h);
        l.caption = QRect(0, l.canvas.height() - 150, l.canvas.width(), 150);
        break;
    }
    case Template::Polaroid: // a square picture on a white card with the wider strip below
        l.canvas = {1000, 1200};
        l.cells = {QRect(60, 60, 880, 880)};
        l.caption = QRect(0, 960, 1000, 240);
        break;
    case Template::Frame: // a border in the theme's colour
        l.canvas = {1360, 1100};
        l.cells = {QRect(40, 40, 1280, 960)};
        l.caption = QRect(0, 1000, 1360, 100);
        break;
    }
    return l;
}

// The part of a picture of size `src` that fills a slot of size `slot` without stretching: the middle, cut to the slot's shape.
inline QRect coverRect(const QSize &src, const QSize &slot)
{
    if (src.isEmpty() || slot.isEmpty())
        return {};
    const double want = double(slot.width()) / slot.height(), have = double(src.width()) / src.height();
    if (have > want) { // too wide: cut the sides
        const int w = qRound(src.height() * want);
        return QRect((src.width() - w) / 2, 0, w, src.height());
    }
    const int h = qRound(src.width() / want); // too tall: cut top and bottom
    return QRect(0, (src.height() - h) / 2, src.width(), h);
}

inline QImage applyFilter(const QImage &in, Filter f)
{
    if (f == Filter::Normal)
        return in;
    QImage img = in.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            const double luma = 0.299 * r + 0.587 * g + 0.114 * b;
            double nr = r, ng = g, nb = b;
            if (f == Filter::Mono) {
                nr = ng = nb = luma;
            } else if (f == Filter::Warm) { // toward amber: more red, less blue, a touch brighter
                nr = r * 1.08 + 8;
                ng = g * 1.02 + 3;
                nb = b * 0.86;
            } else { // Vivid: push every colour away from grey, and a little contrast
                nr = luma + (r - luma) * 1.45;
                ng = luma + (g - luma) * 1.45;
                nb = luma + (b - luma) * 1.45;
                nr = (nr - 128) * 1.06 + 128;
                ng = (ng - 128) * 1.06 + 128;
                nb = (nb - 128) * 1.06 + 128;
            }
            line[x] = qRgb(std::clamp(int(nr + 0.5), 0, 255), std::clamp(int(ng + 0.5), 0, 255), std::clamp(int(nb + 0.5), 0, 255));
        }
    }
    return img;
}

// The finished picture. `frames` holds `shots(t)` pictures (fewer: the missing slots stay empty). `mirror` flips each left to right, as the preview
// showed them. `accent` colours the Frame template's border.
inline QImage compose(Template t, Filter f, bool mirror, const QList<QImage> &frames, const QColor &accent, const QDateTime &when)
{
    const Layout l = layout(t);
    QImage out(l.canvas, QImage::Format_RGB32);
    out.fill(t == Template::Frame ? accent : t == Template::Single ? QColor(0, 0, 0) : QColor(250, 247, 240));
    QPainter p(&out);
    p.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing | QPainter::TextAntialiasing);
    for (int i = 0; i < l.cells.size() && i < frames.size(); ++i) {
        QImage shot = frames.at(i);
        if (shot.isNull())
            continue;
        if (mirror)
            shot = shot.mirrored(true, false);
        shot = applyFilter(shot.copy(coverRect(shot.size(), l.cells.at(i).size())), f);
        p.drawImage(l.cells.at(i), shot);
    }
    if (!l.caption.isEmpty()) {
        const QString text = when.toString(QStringLiteral("d MMM yyyy")) + QStringLiteral(" · Olyzano Notch");
        QFont font;
        font.setPixelSize(t == Template::Polaroid ? 54 : t == Template::Strip ? 34 : 38);
        font.setItalic(t == Template::Polaroid);
        font.setWeight(QFont::DemiBold);
        p.setFont(font);
        const bool onAccent = t == Template::Frame;
        const bool dark = onAccent && accent.lightness() < 140;
        p.setPen(onAccent ? (dark ? QColor(255, 255, 255) : QColor(20, 20, 22)) : QColor(70, 66, 60));
        p.drawText(l.caption, Qt::AlignCenter, text);
    }
    p.end();
    return out;
}

}
