#pragma once

// Keeping Notch out of the way of an app that fills the screen (full screen, or maximised). Two things can ask for it:
//  - Gaming mode (`enabled`, the gamepad button): Notch, the status bar and the dock step aside, sounds and reminders go quiet,
//    and the tray menu is locked so nothing can open by accident;
//  - "Hide Notch when an app is full screen" (`hideInFullScreen`): only the stepping aside. Sounds still play, and the tray menu
//    still works; "Show Notch" there brings it up over the app (`revealed`) until the full-screen state changes.
// Minimise the app, or run it in a window, and Notch is there again. Pure, for the tests.
struct GamingModel {
    bool enabled = false;           // Gaming mode
    bool hideInFullScreen = false;  // the plain preference
    bool fullScreen = false;        // the window in front is full screen
    bool revealed = false;          // the person brought Notch up over it from the tray

    bool strict() const { return enabled && fullScreen; }                     // Gaming mode is keeping Notch away
    bool hidden() const { return strict() || (hideInFullScreen && fullScreen && !revealed); }
    bool active() const { return hidden(); }                                  // "Notch is out of the way" (what the windows follow)
    bool muting() const { return strict(); }                                  // sounds and reminders held back
};
