#pragma once

// Low-battery warning levels, highest first.
inline constexpr int LowBatteryLevels[] = {20, 10, 5};

// The warning level a battery just dropped to or below (20, 10 or 5), or 0. Fires once per
// level per drop: 21 -> 19 gives 20, 19 -> 18 gives 0, 12 -> 4 gives 5 (the lowest crossed).
inline int crossedLowBattery(int before, int after)
{
    int crossed = 0;
    for (int level : LowBatteryLevels)
        if (before > level && after <= level)
            crossed = level;
    return crossed;
}
