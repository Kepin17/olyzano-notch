#pragma once

#include <QString>
#include <QtGlobal>
#include <algorithm>

// Keeping room at the top of the screen, so that maximised windows start below the notch, like under a taskbar.
// Pure, for the tests. The strip is as tall as the tallest resting capsule (the music one), always, so windows never
// jump while the capsule changes between the clock and a song.
namespace ReserveModel {

constexpr int BaseHeight = 44;

inline int heightFor(double scale) { return qRound(BaseHeight * qBound(0.8, scale, 1.3)); }

struct Plan {
    int notchZone = 0; // Wayland: the exclusive zone of the notch window (zones of several surfaces on one edge add up)
    int appBar = 0;    // Windows: the AppBar strip of the notch window, one per monitor (two would stack)
};

// `barPx`: what the status bar already keeps free at the top (0 when it keeps nothing).
inline Plan plan(const QString &capsuleMode, bool enabled, double scale, int barPx, bool wayland)
{
    // Only the capsule keeps a strip: a hidden notch is a line, a widget floats, and an always-open panel overlays on purpose.
    const int island = enabled && capsuleMode == QLatin1String("capsule") ? heightFor(scale) : 0;
    Plan p;
    if (wayland)
        p.notchZone = std::max(0, island - barPx);
    else
        p.appBar = std::max(island, barPx);
    return p;
}

}
