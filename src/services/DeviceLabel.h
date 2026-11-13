#pragma once

#include <QString>

// What a download or a form answer came from, in a few words, from the browser's User-Agent: "iPhone · Safari", "Android · Chrome". Notch cannot
// know a person's name from a link, only the device, so this is what its notifications say. Pure (the relay has the same rules in relay.py).
namespace DeviceLabel {

inline QString from(const QString &userAgent)
{
    const QString ua = userAgent.left(400);
    const auto has = [&ua](const char *s) { return ua.contains(QLatin1String(s)); };
    QString os;
    if (has("iPhone") || has("iPod"))
        os = QStringLiteral("iPhone");
    else if (has("iPad"))
        os = QStringLiteral("iPad");
    else if (has("Android"))
        os = QStringLiteral("Android");
    else if (has("CrOS"))
        os = QStringLiteral("ChromeOS");
    else if (has("Windows"))
        os = QStringLiteral("Windows");
    else if (has("Macintosh") || has("Mac OS X"))
        os = QStringLiteral("Mac");
    else if (has("Linux") || has("X11"))
        os = QStringLiteral("Linux");
    QString browser;
    if (has("Edg/") || has("EdgA/") || has("EdgiOS/"))
        browser = QStringLiteral("Edge");
    else if (has("OPR/") || has("Opera"))
        browser = QStringLiteral("Opera");
    else if (has("Firefox/") || has("FxiOS/"))
        browser = QStringLiteral("Firefox");
    else if (has("CriOS/") || has("Chrome/"))
        browser = QStringLiteral("Chrome");
    else if (has("Safari/") && has("Version/"))
        browser = QStringLiteral("Safari");
    else if (has("curl/"))
        browser = QStringLiteral("curl");
    if (!os.isEmpty() && !browser.isEmpty())
        return os + QStringLiteral(" · ") + browser;
    if (!os.isEmpty())
        return os;
    return browser.isEmpty() ? QStringLiteral("A device") : browser;
}

} // namespace DeviceLabel
