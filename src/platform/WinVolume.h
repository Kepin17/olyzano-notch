#pragma once

// The volume of the default speakers (what the Windows volume flyout shows), 0-100. Windows only.
namespace WinVolume {
int speakers();           // -1 when there is no output device
bool muted();
bool setSpeakers(int percent);
void setMuted(bool mute);
}
