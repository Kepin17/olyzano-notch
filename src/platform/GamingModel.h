#pragma once

// Gaming mode: Notch and the dock keep away only while a full-screen app or game is in front. Minimise it, or
// run it in a window, and Notch is there again. Pure, for the tests.
struct GamingModel {
    bool enabled = false;    // the switch
    bool fullScreen = false; // the window in front is full screen

    bool active() const { return enabled && fullScreen; }
};
