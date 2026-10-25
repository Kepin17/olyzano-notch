#pragma once

#include <QColor>
#include <cmath>
#include <QString>

// How a picked color is written for copying, and which text reads on top of it. Pure, for the tests.
namespace ColorFormat {

// kind: "hex" (#3DAEE9), "rgb" (rgb(61, 174, 233)) or "hsl" (hsl(199, 80%, 57%)); the input as it was picked.
inline QString format(const QString &hex, const QString &kind)
{
    const QColor c(hex);
    if (!c.isValid())
        return hex;
    if (kind == QLatin1String("rgb"))
        return QStringLiteral("rgb(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue());
    if (kind == QLatin1String("hsl")) {
        const QColor h = c.toHsl();
        return QStringLiteral("hsl(%1, %2%, %3%)").arg(qMax(0, h.hslHue())).arg(qRound(h.hslSaturationF() * 100)).arg(qRound(h.lightnessF() * 100));
    }
    return c.name(QColor::HexRgb).toUpper();
}

// Black or white, whichever is easier to read over the color (WCAG relative luminance).
inline QString readableOn(const QString &hex)
{
    const QColor c(hex);
    auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    const double l = 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
    return l > 0.179 ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
}

}
