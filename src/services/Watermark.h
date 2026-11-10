#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>

// A watermark across the pages of a PDF ("CONFIDENTIAL DOCUMENT" and the like). Ghostscript writes the PDF again with a small PostScript program that
// draws the text over every chosen page when it is finished: the text stays vector, the pages and their words are the ones they were, and the
// text is transparent so what is under it can still be read. This file makes that program and the arguments; it runs nothing. Pure.
namespace Watermark {

enum class Layout { Diagonal, Horizontal, Tiled };

struct Options {
    QString text = QStringLiteral("CONFIDENTIAL DOCUMENT");
    Layout layout = Layout::Diagonal;
    QString color = QStringLiteral("gray"); // gray | red | blue
    int opacity = 25;                       // percent of paint, 10..60
    QString size = QStringLiteral("auto");  // auto | small | large
    QString pages;                          // "" = all, else a page range ("1-3, 5")
};

inline QStringList presets()
{
    return {QStringLiteral("CONFIDENTIAL DOCUMENT"), QStringLiteral("CONFIDENTIAL"), QStringLiteral("DRAFT"), QStringLiteral("COPY"), QStringLiteral("INTERNAL USE ONLY"), QStringLiteral("DO NOT COPY")};
}

// "" when the text can be written: 1..40 characters, Latin-1 (the letters of the built-in font), nothing that is not printable.
inline QString validate(const QString &text)
{
    const QString t = text.simplified();
    if (t.isEmpty())
        return QStringLiteral("Write the text of the watermark.");
    if (t.size() > 40)
        return QStringLiteral("The text is longer than 40 characters.");
    for (const QChar c : t)
        if (c.unicode() > 0xFF || c.unicode() < 0x20 || (c.unicode() >= 0x7F && c.unicode() < 0xA0))
            return QStringLiteral("Only Latin letters, digits and punctuation can be used (no “%1”).").arg(c);
    return {};
}

// The text as a PostScript string body: \ ( ) escaped, and letters above ASCII as octal codes (the font is read as ISO Latin 1).
inline QString escapePs(const QString &text)
{
    QString out;
    for (const QChar c : text.simplified()) {
        const ushort u = c.unicode();
        if (u == '\\' || u == '(' || u == ')')
            out += QLatin1Char('\\') + c;
        else if (u > 0x7E)
            out += QStringLiteral("\\%1").arg(u, 3, 8, QLatin1Char('0'));
        else
            out += c;
    }
    return out;
}

inline int clampedOpacity(int percent) { return std::clamp(percent, 10, 60); }

inline QString colorPs(const QString &color)
{
    if (color == QLatin1String("red")) return QStringLiteral("0.80 0.10 0.10");
    if (color == QLatin1String("blue")) return QStringLiteral("0.10 0.25 0.70");
    return QStringLiteral("0.45 0.45 0.45");
}

inline double sizeFactor(const QString &size) { return size == QLatin1String("small") ? 0.6 : size == QLatin1String("large") ? 1.3 : 1.0; }

// The program for Ghostscript. `pages`: the numbers (1-based) to mark, empty for all of them. The size is worked out from each page's own size.
inline QString postscript(const Options &o, const QList<int> &pages)
{
    const QString text = escapePs(o.text);
    const QString alpha = QString::number(clampedOpacity(o.opacity) / 100.0, 'f', 2);
    const double f = sizeFactor(o.size);
    QStringList sel;
    for (const int p : pages)
        sel << QString::number(p);
    QString draw;
    // common: paint, font and the width of the text at 100 points
    const QString setup = QStringLiteral("%1 .setfillconstantalpha %2 setrgbcolor /WMfont findfont 100 scalefont setfont (%3) stringwidth pop /sw exch def ").arg(alpha, colorPs(o.color), text);
    // a size such that the string is `len` points long, then its centre on the origin
    const auto centred = [&](const QString &len) {
        return QStringLiteral("/fs 100 %1 mul sw div def /WMfont findfont fs scalefont setfont (%2) stringwidth pop 2 div neg fs 0.35 mul neg moveto (%2) show ").arg(len, text);
    };
    switch (o.layout) {
    case Layout::Diagonal:
        // along the diagonal at 45 degrees: as long as the shorter side times 1.1 still lies inside the page
        draw = setup + QStringLiteral("pw 2 div ph 2 div translate 45 rotate /len pw ph lt { pw } { ph } ifelse %1 mul def ").arg(1.1 * f, 0, 'f', 3) + centred(QStringLiteral("len"));
        break;
    case Layout::Horizontal:
        draw = setup + QStringLiteral("pw 2 div ph 2 div translate /len pw %1 mul def ").arg(std::min(0.95, 0.8 * f), 0, 'f', 3) + centred(QStringLiteral("len"));
        break;
    case Layout::Tiled:
        draw = setup + QStringLiteral("pw 2 div ph 2 div translate 30 rotate /len pw ph lt { pw } { ph } ifelse %1 mul def /fs 100 len mul sw div def /WMfont findfont fs scalefont setfont "
                                      "/dx (%2) stringwidth pop 1.7 mul def /dy fs 4 mul def /half pw dup mul ph dup mul add sqrt def "
                                      "half neg dy half { /yy exch def half neg dx half { yy moveto (%2) show } for } for").arg(0.30 * f, 0, 'f', 3).arg(text);
        break;
    }
    return QStringLiteral("%!PS\n"
                          "/WMfont /Helvetica-Bold findfont dup length dict copy dup /Encoding ISOLatin1Encoding put definefont pop\n"
                          "/sel [%1] def /useSel %2 def\n"
                          "<< /EndPage {\n"
                          "  exch /pc exch def\n"
                          "  2 lt {\n"
                          "    /ok useSel not def\n"
                          "    useSel { sel { pc 1 add eq { /ok true def } if } forall } if\n"
                          "    ok {\n"
                          "      gsave\n"
                          "      currentpagedevice /PageSize get aload pop /ph exch def /pw exch def\n"
                          "      %3\n"
                          "      grestore\n"
                          "    } if\n"
                          "    true\n"
                          "  } { false } ifelse\n"
                          "} bind >> setpagedevice\n")
        .arg(sel.join(QLatin1Char(' ')), sel.isEmpty() ? QStringLiteral("false") : QStringLiteral("true"), draw);
}

// Ghostscript's arguments: -dALLOWPSTRANSPARENCY is what lets the program say how pale the paint is (the safe mode stays on).
inline QStringList gsArgs(const QString &psFile, const QString &in, const QString &out)
{
    return {QStringLiteral("-dALLOWPSTRANSPARENCY"), QStringLiteral("-sDEVICE=pdfwrite"), QStringLiteral("-dCompatibilityLevel=1.5"), QStringLiteral("-dPDFSETTINGS=/prepress"),
            QStringLiteral("-dPassThroughJPEGImages=true"), QStringLiteral("-dNOPAUSE"), QStringLiteral("-dBATCH"), QStringLiteral("-dQUIET"), QStringLiteral("-sOutputFile=") + out, psFile, in};
}

// "confidential document" for the name of the result: "<name> (confidential document).pdf"
inline QString label(const QString &text)
{
    QString t;
    for (const QChar c : text.toLower().simplified())
        t += c.isLetterOrNumber() || c == u' ' ? c : QChar(u' ');
    return t.simplified().left(24).trimmed();
}

inline QString toJson(const Options &o)
{
    const char *layout = o.layout == Layout::Horizontal ? "horizontal" : o.layout == Layout::Tiled ? "tiled" : "diagonal";
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("text"), o.text}, {QStringLiteral("layout"), QString::fromLatin1(layout)}, {QStringLiteral("color"), o.color},
                                                       {QStringLiteral("opacity"), o.opacity}, {QStringLiteral("size"), o.size}, {QStringLiteral("pages"), o.pages}})
                                 .toJson(QJsonDocument::Compact));
}

inline Options fromJson(const QString &json)
{
    Options o;
    const QJsonObject j = QJsonDocument::fromJson(json.toUtf8()).object();
    if (j.contains(QStringLiteral("text")))
        o.text = j.value(QStringLiteral("text")).toString();
    const QString layout = j.value(QStringLiteral("layout")).toString();
    o.layout = layout == QLatin1String("horizontal") ? Layout::Horizontal : layout == QLatin1String("tiled") ? Layout::Tiled : Layout::Diagonal;
    const QString color = j.value(QStringLiteral("color")).toString();
    o.color = color == QLatin1String("red") || color == QLatin1String("blue") ? color : QStringLiteral("gray");
    o.opacity = clampedOpacity(j.contains(QStringLiteral("opacity")) ? j.value(QStringLiteral("opacity")).toInt(25) : 25);
    const QString size = j.value(QStringLiteral("size")).toString();
    o.size = size == QLatin1String("small") || size == QLatin1String("large") ? size : QStringLiteral("auto");
    o.pages = j.value(QStringLiteral("pages")).toString().simplified();
    return o;
}

} // namespace Watermark
