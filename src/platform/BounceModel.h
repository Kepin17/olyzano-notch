#pragma once

#include <QString>
#include <QtGlobal>

// "Bounce" in Settings -> Appearance: how far the springs overshoot. A spring overshoots less the more it is damped, so the setting
// multiplies the damping of every spring (Main.qml) by a factor, and `damp` keeps the result in a range that still looks like a spring.
namespace BounceModel {

// off: the damping is lifted to critical (no overshoot at all); soft: 1.7 times; normal: as it always was; bouncy: roughly half.
inline double factor(const QString &level)
{
    return level == QLatin1String("off") ? 0.0 : level == QLatin1String("soft") ? 1.7 : level == QLatin1String("bouncy") ? 0.55 : 1.0;
}

// The damping to use for a spring whose "normal" damping is `base` (0..1 and a little above): 0 = Off.
inline double damp(double base, double factor)
{
    if (factor <= 0.0)
        return qMax(base, 1.0); // critically damped: arrives without passing the target
    return qBound(0.12, base * factor, 1.0);
}

}
