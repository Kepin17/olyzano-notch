#pragma once

#include <QList>
#include <QString>

// One MPRIS player as seen by the selection rule.
struct PlayerState {
    QString service;    // org.mpris.MediaPlayer2.<name>
    QString status;     // "Playing" | "Paused" | "Stopped"
    qint64 lastActive;  // monotonic stamp of the last change to Playing or Paused
};

// Browsers show up twice when Plasma's browser integration is installed: keep its entry
// (better metadata) and drop the browser's own.
inline bool isBrowserDuplicate(const QString &service, const QList<PlayerState> &all)
{
    static const QString pbi = QStringLiteral("org.mpris.MediaPlayer2.plasma-browser-integration");
    if (service == pbi)
        return false;
    const bool browser = service.startsWith(QLatin1String("org.mpris.MediaPlayer2.chromium"))
                         || service.startsWith(QLatin1String("org.mpris.MediaPlayer2.chrome"))
                         || service.startsWith(QLatin1String("org.mpris.MediaPlayer2.firefox"));
    if (!browser)
        return false;
    for (const PlayerState &p : all)
        if (p.service == pbi)
            return true;
    return false;
}

// Players worth showing, in a stable order (for the switch button).
inline QList<PlayerState> visiblePlayers(const QList<PlayerState> &all)
{
    QList<PlayerState> out;
    for (const PlayerState &p : all)
        if ((p.status == QLatin1String("Playing") || p.status == QLatin1String("Paused"))
            && !isBrowserDuplicate(p.service, all))
            out << p;
    std::sort(out.begin(), out.end(), [](const PlayerState &a, const PlayerState &b) { return a.service < b.service; });
    return out;
}

// The player to show: most recently active Playing one, else most recently active Paused one,
// else none (empty). A manual choice wins while it is still visible.
inline QString pickPlayer(const QList<PlayerState> &all, const QString &manual = {})
{
    const QList<PlayerState> players = visiblePlayers(all);
    const PlayerState *best = nullptr;
    for (const PlayerState &p : players) {
        if (p.service == manual)
            return manual;
        const bool playing = p.status == QLatin1String("Playing");
        const bool bestPlaying = best && best->status == QLatin1String("Playing");
        if (!best || (playing && !bestPlaying) || (playing == bestPlaying && p.lastActive > best->lastActive))
            best = &p;
    }
    return best ? best->service : QString();
}
