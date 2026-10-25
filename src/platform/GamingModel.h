#pragma once

// When the notch keeps out of the way: switched on by hand, or by itself while a full-screen app or game is in
// front (a setting). Waking it up (the tray icon) only works while nothing is full screen: over a full-screen
// app the panel can't be opened at all. Pure, for the tests.
struct GamingModel {
    bool manual = false;
    bool autoDetect = false;
    bool fullScreen = false;

    bool active() const { return manual || (autoDetect && fullScreen); }
    bool canWake() const { return !fullScreen; }

    void setFullScreen(bool on) { fullScreen = on; }
    // Leaves the manual mode. Not while a full-screen window is in front: nothing opens over it.
    void wake()
    {
        if (canWake())
            manual = false;
    }
};
