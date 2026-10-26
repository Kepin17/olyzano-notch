#pragma once

// Discord's call state as the island shows it, worked out from Discord's own audio session on the microphone
// (WinAudioSessions): no Discord account, app registration or network is involved.
enum class CallState { Off, InCall, Talking };

struct CallTracker {
    CallState state = CallState::Off;
    int quiet = 0; // polls in a row without a voice

    // `active`: Discord has a live microphone session; `peak`: how loud it is now (0..1).
    // Talking holds for a few quiet polls so the icon doesn't flicker between words.
    CallState update(bool active, double peak)
    {
        if (!active) {
            quiet = 0;
            return state = CallState::Off;
        }
        if (peak > 0.04) {
            quiet = 0;
            return state = CallState::Talking;
        }
        if (state == CallState::Talking && ++quiet < 5)
            return state;
        quiet = 0;
        return state = CallState::InCall;
    }
};
