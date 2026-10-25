## Notch 0.7.1

A fix for the idle clock introduced in 0.7.0.

- **The idle clock no longer covers your windows.** It hides while the window in front reaches the top of its screen (a maximized browser's tabs, a title bar, a full-screen video) and comes back when you switch to the desktop or a smaller window. Other monitors keep their clock.
- **Clicks go through it.** While it shows, whatever is underneath can still be clicked; rest the pointer on the very top edge to open Notch, as before.
- Slightly slimmer.
- **No more orange microphone dot** beside the notch: apps often keep the microphone open, and the idle clock's mic icon already turns orange. The green camera dot stays.

Everything new in 0.7 (app dock Trash and badges, live activities, timer and Pomodoro, stretch reminder, phone alerts, privacy dots, six new themes) is listed in the [0.7.0 release](https://github.com/Kepin17/olyzano-notch/releases/tag/v0.7.0).

### Downloads
- **Linux:** `notch_0.7.1_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.7.1_amd64.deb`, then restart Notch.
- **Windows:** `Notch-0.7.1-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).
