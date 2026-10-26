#pragma once

#include <QString>
#include <QStringList>

// Windows media sessions name their app by AppUserModelID; these turn one into what the card shows,
// and into the process names whose window "raise" looks for.
//   "Chrome" -> "Chrome", "Spotify.exe" -> "Spotify", "308046B0AF4A39CB" -> "Firefox",
//   "Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic" -> "Media Player"
namespace MediaNames {

struct Known {
    const char *key; // lower case: an .exe base name, or a packaged app's last name part
    const char *name;
    const char *process;
};

inline const Known *known(const QString &key)
{
    static const Known table[] = {
        {"chrome", "Chrome", "chrome"},
        {"msedge", "Edge", "msedge"},
        {"308046b0af4a39cb", "Firefox", "firefox"}, // Firefox's AUMID is a hash of its install path
        {"firefox", "Firefox", "firefox"},
        {"brave", "Brave", "brave"},
        {"opera", "Opera", "opera"},
        {"vivaldi", "Vivaldi", "vivaldi"},
        {"spotify", "Spotify", "spotify"},
        {"spotifymusic", "Spotify", "spotify"},
        {"zunemusic", "Media Player", "microsoft.media.player"},
        {"zunevideo", "Movies & TV", "video.ui"},
        {"vlc", "VLC", "vlc"},
        {"discord", "Discord", "discord"},
        {"discordptb", "Discord", "discordptb"},
        {"discordcanary", "Discord", "discordcanary"},
        {"applemusicwin", "Apple Music", "applemusic"},
    };
    for (const Known &k : table)
        if (key == QLatin1String(k.key))
            return &k;
    return nullptr;
}

// The part of an AUMID that names the app, lower case.
inline QString keyOf(const QString &aumid)
{
    QString key = aumid;
    if (key.contains(u'!')) // packaged: PackageFamilyName!AppId
        key = key.section(u'!', -1);
    key = key.section(u'\\', -1);
    if (key.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        key.chop(4);
    else if (key.contains(u'.'))
        key = key.section(u'.', -1); // Microsoft.ZuneMusic, SpotifyAB.SpotifyMusic
    return key.toLower();
}

inline QString displayName(const QString &aumid)
{
    const QString key = keyOf(aumid);
    if (const Known *k = known(key))
        return QString::fromLatin1(k->name);
    if (key.isEmpty())
        return aumid;
    return key.left(1).toUpper() + key.mid(1);
}

inline QString processName(const QString &aumid)
{
    const QString key = keyOf(aumid);
    const Known *k = known(key);
    return k ? QString::fromLatin1(k->process) : key;
}

}
