## Notch 0.9.0

Notch makes a sound now, and shows what other apps on the computer want to tell you.

- **Sound effects.** Short, soft sounds for a file dropped or pasted, the Trash emptied, a colour picked, a timer or
  Pomodoro done, the stretch reminder and the end of a break, an incoming call (rings up to three times),
  notifications, files sent or received, the charger, a Bluetooth device and a low battery.
  - *Settings → General → Sounds*: volume, a switch per group with ▶ to hear it.
  - *Quiet during calls and full-screen apps* is on by default: nothing plays while an app uses the microphone, and
    only a call rings over a full-screen video.
  - Nothing new to install: Linux plays through `paplay` (PulseAudio or PipeWire), Windows through its own sound API.
- **Notifications from this computer in the notch.** Slack, mail, the browser…: "Notification from Slack · John", never
  the message itself, with a sound. Do not disturb (Plasma) or Focus (Windows) keeps them quiet. Settings lists the apps
  that sent some lately; tap one to stop or start showing it. KDE Connect's are left out, since the phone's notifications
  already show.
  - Linux: read from the session bus, like KDE Connect does; Plasma still shows them as usual.
  - Windows: Notch asks Windows for access. Windows may refuse it to apps not installed as an MSIX package; Settings then
    says it isn't available on this install.
- The *Phone calls & notifications* and *Microphone & camera* switches now show on Windows too (they were hidden there
  in 0.8.0 although the features worked).

### Downloads
- **Linux:** `notch_0.9.0_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.9.0_amd64.deb`, then restart Notch.
- **Windows:** `Notch-0.9.0-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).
