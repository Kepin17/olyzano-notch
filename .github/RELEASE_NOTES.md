## Notch 0.8.0

Windows catches up: everything that was Linux-only works there now, except the dock's blur and unread badges.

- **Now playing on Windows.** YouTube in any browser, Spotify, Media Player: anything in the volume flyout gets the
  equalizer button and the player card (cover, seek bar, previous / play / next, switch between players). The bars
  follow the real sound from the speakers' loopback; no microphone permission is asked.
- **Locked categories on Windows.** Notch encrypts each file itself (AES-256-GCM, a key protected by your password).
  While unlocked the files sit decrypted in your Temp folder, kept out of Windows Search; locking encrypts what changed
  and deletes them. On Linux nothing changes (gocryptfs).
- **Microphone and camera on Windows.** The green camera dot, the apps using the camera or the microphone, and the idle
  clock's mute state and toggle, from the same list as Windows' own taskbar microphone icon.
- **Bluetooth alerts on Windows.** A device connecting or disconnecting peeks out of the notch, with its battery when it
  reports one, and a warning when that battery runs low.
- **Colour picker on Windows.** The screen freezes with a magnifier and the hex value at the pointer; click to pick,
  Esc to cancel.
- **KDE Connect on Windows.** With [KDE Connect for Windows](https://kdeconnect.kde.org/download.html) installed and
  paired: Send to device, files from the phone, incoming calls and phone notifications in the notch.

### Downloads
- **Linux:** `notch_0.8.0_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.8.0_amd64.deb`, then restart Notch.
- **Windows:** `Notch-0.8.0-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).
