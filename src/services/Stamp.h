#pragma once

#include "services/Watermark.h"

#include <QByteArray>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPainter>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>

// A stamp (APPROVED, PAID…) or a picture of a signature put on chosen pages of a PDF, at a place and size the person picked. Like the watermark,
// Ghostscript writes the PDF again with a PostScript program that draws it when the page is finished. A text stamp stays vector; a picture is drawn with
// its white (and transparent) background masked out, so the words under it can be read. This is a picture on the page, not a cryptographic signature. Pure.
namespace Stamp {

struct Options {
    bool picture = false;                      // a picture (signature) instead of a text stamp
    QString text = QStringLiteral("APPROVED");
    QString color = QStringLiteral("green");   // green | red | blue | black
    double x = 0.75, y = 0.85;                 // the centre, as a part of the page width and height; (0, 0) is the top left
    double width = 0.30;                       // as a part of the page width
    double rotation = 8;                       // degrees, counter-clockwise (a text stamp is tilted a little, a picture is not)
    double opacity = 1.0;                      // 0.2 .. 1
    QString pages = QStringLiteral("last");    // last | first | all | a range such as "1-3, 5"
    QString image;                             // the file of the picture
};

inline QStringList presets() { return {QStringLiteral("APPROVED"), QStringLiteral("PAID"), QStringLiteral("RECEIVED"), QStringLiteral("REJECTED"), QStringLiteral("DRAFT"), QStringLiteral("VOID")}; }

// "" when the stamp text can be written: 1..24 Latin-1 characters.
inline QString validate(const QString &text)
{
    if (text.simplified().size() > 24)
        return QStringLiteral("The text is longer than 24 characters.");
    return Watermark::validate(text);
}

inline double clamped(double v, double lo, double hi) { return std::clamp(v, lo, hi); }
inline double clampedWidth(double w) { return clamped(w, 0.05, 0.60); }
inline double clampedPos(double v) { return clamped(v, 0.0, 1.0); }
inline double clampedRotation(double v) { return clamped(v, -45.0, 45.0); }
inline double clampedOpacity(double v) { return clamped(v, 0.2, 1.0); }

inline QString colorPs(const QString &color)
{
    if (color == QLatin1String("red")) return QStringLiteral("0.75 0.10 0.10");
    if (color == QLatin1String("blue")) return QStringLiteral("0.10 0.25 0.70");
    if (color == QLatin1String("black")) return QStringLiteral("0.05 0.05 0.05");
    return QStringLiteral("0.10 0.50 0.20");
}

// A picture made ready for the program: at most 500 pixels wide, and what is to be left out is one colour the program is told to skip (MaskColor).
// A picture with transparent parts (a cut-out, a PNG signature) has them painted in a colour its opaque part does not use, so white inside a logo stays;
// a picture without transparency is drawn on white and what is almost white becomes pure white, which is skipped. Rows of 8-bit RGB as hexadecimal
// text, one string per row (a PostScript string holds at most 64 KB).
struct Picture {
    int width = 0, height = 0;
    QStringList rows;
    bool keyed = false;                                                     // transparent parts were painted in the key colour
    int keyR = 255, keyG = 255, keyB = 255;                                 // the colour left out when `keyed`
    QString maskColor() const { return keyed ? QStringLiteral("%1 %1 %2 %2 %3 %3").arg(keyR).arg(keyG).arg(keyB) : QStringLiteral("250 255 250 255 250 255"); }
    bool isEmpty() const { return rows.isEmpty(); }
};

inline Picture preparePicture(const QImage &source)
{
    Picture p;
    if (source.isNull())
        return p;
    QImage img = (source.width() > 500 ? source.scaledToWidth(500, Qt::SmoothTransformation) : source).convertToFormat(QImage::Format_ARGB32);
    bool transparent = false;
    for (int y = 0; y < img.height() && !transparent; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(line[x]) < 250) { transparent = true; break; }
    }
    QImage flat(img.size(), QImage::Format_RGB888);
    flat.fill(Qt::white);
    {
        QPainter painter(&flat);
        painter.drawImage(0, 0, img);
    }
    if (transparent) {
        // a colour that no opaque pixel has
        const int candidates[][3] = {{255, 0, 255}, {0, 255, 0}, {0, 255, 255}, {255, 255, 0}, {1, 254, 3}, {253, 2, 251}};
        QSet<quint32> used;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            const uchar *rgb = flat.constScanLine(y);
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(line[x]) >= 128)
                    used.insert((quint32(rgb[x * 3]) << 16) | (quint32(rgb[x * 3 + 1]) << 8) | rgb[x * 3 + 2]);
        }
        for (const auto &k : candidates)
            if (!used.contains((quint32(k[0]) << 16) | (quint32(k[1]) << 8) | quint32(k[2]))) {
                p.keyR = k[0]; p.keyG = k[1]; p.keyB = k[2];
                p.keyed = true;
                break;
            }
        if (p.keyed)
            for (int y = 0; y < flat.height(); ++y) {
                const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                uchar *rgb = flat.scanLine(y);
                for (int x = 0; x < flat.width(); ++x)
                    if (qAlpha(line[x]) < 128) { rgb[x * 3] = uchar(p.keyR); rgb[x * 3 + 1] = uchar(p.keyG); rgb[x * 3 + 2] = uchar(p.keyB); }
            }
    }
    for (int y = 0; y < flat.height(); ++y) {
        uchar *line = flat.scanLine(y);
        if (!p.keyed)
            for (int x = 0; x < flat.width(); ++x) {
                uchar *px = line + x * 3;
                if (0.299 * px[0] + 0.587 * px[1] + 0.114 * px[2] > 235.0)
                    px[0] = px[1] = px[2] = 255;
            }
        p.rows << QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(line), flat.width() * 3).toHex());
    }
    p.width = flat.width();
    p.height = flat.height();
    return p;
}

// The picture as the person will see it placed: the same preparation, with what the program skips left out (transparent).
inline QImage previewImage(const QImage &source)
{
    const Picture p = preparePicture(source);
    QImage out(p.width, p.height, QImage::Format_ARGB32);
    for (int y = 0; y < p.height; ++y) {
        const QByteArray row = QByteArray::fromHex(p.rows.at(y).toLatin1());
        for (int x = 0; x < p.width; ++x) {
            const uchar *px = reinterpret_cast<const uchar *>(row.constData()) + x * 3;
            const bool skipped = p.keyed ? (px[0] == p.keyR && px[1] == p.keyG && px[2] == p.keyB) : (px[0] >= 250 && px[1] >= 250 && px[2] >= 250);
            out.setPixelColor(x, y, skipped ? QColor(0, 0, 0, 0) : QColor(px[0], px[1], px[2]));
        }
    }
    return out;
}

// The program for Ghostscript. `pages`: 1-based numbers to mark, empty for all of them.
inline QString postscript(const Options &o, const QList<int> &pages, const Picture &picture = {})
{
    QStringList sel;
    for (const int p : pages)
        sel << QString::number(p);
    const auto num = [](double v) { return QString::number(v, 'f', 4); };
    QString rows;
    QString draw;
    // the centre in points, and the width asked for
    const QString alpha = clampedOpacity(o.opacity) < 0.999 ? QStringLiteral("%1 .setfillconstantalpha %1 .setstrokeconstantalpha ").arg(num(clampedOpacity(o.opacity))) : QString();
    const QString place = QStringLiteral("/cx pw %1 mul def /cy ph 1 %2 sub mul def /wpt pw %3 mul def ").arg(num(clampedPos(o.x)), num(clampedPos(o.y)), num(clampedWidth(o.width)));
    if (o.picture && !picture.isEmpty()) {
        rows = QStringLiteral("/rows [\n");
        for (const QString &row : picture.rows)
            rows += QLatin1Char('<') + row + QStringLiteral(">\n");
        rows += QStringLiteral("] def\n/rowi 0 def\n");
        draw = place + alpha + QStringLiteral("/hpt wpt %1 mul %2 div def cx cy translate %4 rotate wpt hpt scale -0.5 -0.5 translate /DeviceRGB setcolorspace /rowi 0 store "
                                      "<< /ImageType 4 /Width %2 /Height %1 /BitsPerComponent 8 /Decode [0 1 0 1 0 1] /ImageMatrix [%2 0 0 -%1 0 %1] "
                                      "/MaskColor [%3] /DataSource { rows rowi get /rowi rowi 1 add store } >> image")
                   .arg(picture.height)
                   .arg(picture.width)
                   .arg(picture.maskColor(), num(clampedRotation(o.rotation)));
    } else {
        const QString text = Watermark::escapePs(o.text);
        draw = place + alpha + QStringLiteral("%1 setrgbcolor cx cy translate %3 rotate /WMfont findfont 100 scalefont setfont (%2) stringwidth pop /sw exch def "
                                      "/fs wpt sw 100 div 0.8 add div def /WMfont findfont fs scalefont setfont "
                                      "/bw sw fs mul 100 div fs 0.8 mul add def /bh fs 1.32 mul def fs 0.07 mul setlinewidth "
                                      "bw 2 div neg bh 2 div neg bw bh rectstroke "
                                      "bw 2 div neg fs 0.4 mul add fs 0.36 mul neg moveto (%2) show")
                   .arg(colorPs(o.color), text, num(clampedRotation(o.rotation)));
    }
    return QStringLiteral("%!PS\n"
                          "/WMfont /Helvetica-Bold findfont dup length dict copy dup /Encoding ISOLatin1Encoding put definefont pop\n"
                          "%1"
                          "/sel [%2] def /useSel %3 def\n"
                          "<< /EndPage {\n"
                          "  exch /pc exch def\n"
                          "  2 lt {\n"
                          "    /ok useSel not def\n"
                          "    useSel { sel { pc 1 add eq { /ok true def } if } forall } if\n"
                          "    ok {\n"
                          "      gsave\n"
                          "      currentpagedevice /PageSize get aload pop /ph exch def /pw exch def\n"
                          "      %4\n"
                          "      grestore\n"
                          "    } if\n"
                          "    true\n"
                          "  } { false } ifelse\n"
                          "} bind >> setpagedevice\n")
        .arg(rows, sel.join(QLatin1Char(' ')), sel.isEmpty() ? QStringLiteral("false") : QStringLiteral("true"), draw);
}

// Ghostscript's arguments: as the watermark's, and colour images kept lossless (a JPEG would blur the key colour the program skips).
inline QStringList gsArgs(const QString &psFile, const QString &in, const QString &out)
{
    QStringList args = Watermark::gsArgs(psFile, in, out);
    args.insert(1, QStringLiteral("-dColorImageFilter=/FlateEncode"));
    args.insert(1, QStringLiteral("-dAutoFilterColorImages=false"));
    return args;
}

// the name of the result: "<name> (approved).pdf" or "<name> (signed).pdf"
inline QString label(const Options &o) { return o.picture ? QStringLiteral("signed") : Watermark::label(o.text); }

inline QString toJson(const Options &o)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("picture"), o.picture}, {QStringLiteral("text"), o.text}, {QStringLiteral("color"), o.color}, {QStringLiteral("x"), o.x},
                                                       {QStringLiteral("y"), o.y}, {QStringLiteral("width"), o.width}, {QStringLiteral("rotation"), o.rotation}, {QStringLiteral("opacity"), o.opacity}, {QStringLiteral("pages"), o.pages}, {QStringLiteral("image"), o.image}})
                                 .toJson(QJsonDocument::Compact));
}

inline Options fromJson(const QString &json)
{
    Options o;
    const QJsonObject j = QJsonDocument::fromJson(json.toUtf8()).object();
    o.picture = j.value(QStringLiteral("picture")).toBool(false);
    if (j.contains(QStringLiteral("text")))
        o.text = j.value(QStringLiteral("text")).toString();
    const QString color = j.value(QStringLiteral("color")).toString();
    o.color = color == QLatin1String("red") || color == QLatin1String("blue") || color == QLatin1String("black") ? color : QStringLiteral("green");
    o.x = clampedPos(j.value(QStringLiteral("x")).toDouble(0.75));
    o.y = clampedPos(j.value(QStringLiteral("y")).toDouble(0.85));
    o.width = clampedWidth(j.value(QStringLiteral("width")).toDouble(0.30));
    o.rotation = clampedRotation(j.value(QStringLiteral("rotation")).toDouble(o.picture ? 0.0 : 8.0));
    o.opacity = clampedOpacity(j.value(QStringLiteral("opacity")).toDouble(1.0));
    const QString pages = j.value(QStringLiteral("pages")).toString().simplified();
    o.pages = pages.isEmpty() ? QStringLiteral("last") : pages;
    o.image = j.value(QStringLiteral("image")).toString();
    return o;
}

} // namespace Stamp
