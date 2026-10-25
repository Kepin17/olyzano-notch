## Olyzano Notch 0.9.1

A fix for sounds that never played, and for images that came out huge when dragged.

- **Sounds were silent while Discord (or a browser) was open.** Those apps keep a microphone stream open all day, and
  *Quiet during calls* took that for a call and muted everything. Now only a video, game or slideshow filling the
  screen quiets the sounds by default. The microphone check is its own switch, *Also quiet while the microphone is in
  use* in *Settings → General → Sounds*, off unless you want it.
- **Notifications from this computer: no more per-app list.** Every app's notifications now show in the notch as soon as they are detected; Do not disturb (Plasma) or Focus (Windows) still keeps them quiet.
- **Dragging an image out of the shelf** showed it at full size under the pointer. It is now a small thumbnail.

### Downloads
- **Linux:** `notch_0.9.1_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.9.1_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.9.1-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).
