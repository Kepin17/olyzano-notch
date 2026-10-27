#pragma once

#include <QString>
#include <QtGlobal>
#include <algorithm>

// Keeping room at the top of the screen, so that maximised windows start below the notch, like under a taskbar.
// Pure, for the tests. The strip is as tall as the tallest resting capsule (the music one), always, so windows never
// jump while the capsule changes between the clock and a song.
namespace ReserveModel {

constexpr int BaseHeight = 48;
constexpr int PillHeight = 52; // the floating pill's music capsule is a little taller...
constexpr int PillGap = 10;    // ...and hangs this far below the edge (the outline and a hair of air are added)
constexpr int PillAir = 2;

// The edge look is `BaseHeight` tall at most; the floating pill is the gap plus its own height, scaled from its top.
inline int heightFor(double scale, bool pill = false)
{
    const double k = qBound(0.8, scale, 1.3);
    return pill ? qRound(PillGap + PillHeight * k + PillAir) : qRound(BaseHeight * k);
}

struct Plan {
    int notchZone = 0; // Wayland: the exclusive zone of the notch window (zones of several surfaces on one edge add up)
    int appBar = 0;    // Windows: the AppBar strip of the notch window, one per monitor (two would stack)
};

// `barPx`: what the status bar already keeps free at the top (0 when it keeps nothing).
inline Plan plan(const QString &capsuleMode, bool enabled, double scale, int barPx, bool wayland, bool pill = false)
{
    // Only the capsule keeps a strip: a hidden notch is a line, a widget floats, and an always-open panel overlays on purpose.
    const int island = enabled && capsuleMode == QLatin1String("capsule") ? heightFor(scale, pill) : 0;
    Plan p;
    if (wayland)
        p.notchZone = std::max(0, island - barPx);
    else
        p.appBar = std::max(island, barPx);
    return p;
}

}
