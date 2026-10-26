## Olyzano Notch 0.12.0

Notch now tells you when a newer version is out.

- **Update check.** Once a day (and whenever you press *Check now*), Notch asks GitHub whether a newer release exists. If there is one you get a short notice in the notch, once per version, and never in the middle of a game, a call or a timer. *Settings → About → Updates* shows what the new version brings, with **Download** (opens the right file for your system in the browser), **Remind me in a week** and **Skip this version**. A dot beside *About* shows while one is waiting.
- **Private and optional.** The check is an anonymous request: GitHub sees your IP address and the version of Notch, nothing else. *Settings → About → Check for updates automatically* turns it off, and then nothing is ever sent.
- Notch does not install anything by itself: download, close Notch, install, open it again.
- **The web version** (`/app`, installable) says "A new version is ready" with a Reload button when it has been updated.

### Downloads
- **Linux:** `notch_0.12.0_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.12.0_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.12.0-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).
