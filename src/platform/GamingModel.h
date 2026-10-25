#pragma once

// When the notch keeps out of the way: switched on by hand, or by itself while a full-screen app or game is in
// front (a setting). Waking it up (the tray icon) ends the manual mode, and ends the automatic one for as long
// as that same full-screen window lasts. Pure, for the tests.
struct GamingModel {
    bool manual = false;
    bool autoDetect = false;
    bool fullScreen = false;
    bool suspended = false; // woken up while the full-screen window is still there

    bool active() const { return manual || (autoDetect && fullScreen && !suspended); }

    void setFullScreen(bool on)
    {
        fullScreen = on;
        if (!on)
            suspended = false; // the next game starts quiet again
    }
    void wake()
    {
        manual = false;
        if (autoDetect && fullScreen)
            suspended = true;
    }
};
