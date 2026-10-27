#pragma once

#include <QtGlobal>

// Glass (Settings -> Appearance): the island and the surfaces on it turn see-through. With the system's blur behind them
// (KWin) they can go quite thin; without it the wallpaper shows through as it is, so they stay mostly solid and the text
// stays readable.
namespace GlassModel {

constexpr double IslandMinBlur = 0.55;  // the island's least opaque, with a blurred background
constexpr double IslandMinPlain = 0.78; // and without one
constexpr double SurfaceMinBlur = 0.45; // the cards, wells and buttons on the island
constexpr double SurfaceMinPlain = 0.70;

inline double strengthOf(int percent) { return qBound(0, percent, 100) / 100.0; }

// Opacity (0..1) of the island's own fill: 0.96 at strength 0, down to the minimum at 100.
inline double islandAlpha(int percent, bool blur)
{
    const double lo = blur ? IslandMinBlur : IslandMinPlain;
    return 0.96 - (0.96 - lo) * strengthOf(percent);
}

// Opacity of a surface on the island: opaque at 0, down to the minimum at 100.
inline double surfaceAlpha(int percent, bool blur)
{
    const double lo = blur ? SurfaceMinBlur : SurfaceMinPlain;
    return 1.0 - (1.0 - lo) * strengthOf(percent);
}


// The look of the glass itself (see GlassLayer.qml), a blend of three: macOS's bright rim along the top edge, HyperOS's calm
// soft gradient, One UI's lifted, lighter frost. All grow with the strength; the light styles need more to show on white.

// Opacity of the white sheen at the top of a surface; it fades to nothing by the middle.
inline double sheenTop(int percent, bool dark) { return dark ? 0.03 + 0.10 * strengthOf(percent) : 0.12 + 0.28 * strengthOf(percent); }

// Opacity of the bright rim over the top edge and corners (it fades out down the sides).
inline double rimTop(int percent, bool dark) { return dark ? 0.12 + 0.20 * strengthOf(percent) : 0.55 + 0.30 * strengthOf(percent); }

// How far the island's colour is lifted towards white so a see-through island is frosted, not muddy (dark looks only).
inline double lift(int percent, bool dark) { return dark ? 0.05 * strengthOf(percent) : 0.0; }

}
