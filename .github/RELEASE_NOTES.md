## Olyzano Notch 0.12.3

A round of polish: a modern music player and capsule, room for the notch like a taskbar, and fixes for Windows.

- **New music player.** The cover is big, on a blurred copy of itself, with the colour of the cover on the progress line, the sound wave and the play button. The cover pops and the text slides in when the song changes; a thin progress line shows a thumb when you point at it; one big play button, and the small buttons tidied into a quiet row.
- **A modern music capsule.** The closed notch shows a small cover, the title and the artist on two lines (long ones glide sideways instead of being cut off), the sound wave in the cover's colour and a thin progress line. Point at it for the app and the time.
- **Room for the notch (Windows and KDE Plasma on Wayland).** *Settings → General → Keep other windows clear of the capsule* keeps a strip free along the top, so maximised windows start below the capsule, like under a taskbar. Off by default. On Windows the status bar's "keep windows out from under it" works now too.
- **The microphone icon no longer stays green.** The voice detector follows the loudness of the room instead of a fixed line, and there is a *Voice sensitivity* choice (Linux).
- **Windows:** dragging the Widget is smooth and no longer vanishes; in gaming mode the notch stays on screen (it just cannot be opened); a card in *Settings → About* and a note in the installer explain how to make RivaTuner / MSI Afterburner ignore Notch (set *Application detection level* to None for `notch.exe`).
- **Lyrics are on by default** and the Lyrics card is gone from Settings: the Music page's *Lyrics* button turns them on and off. Notch looks the song up on lrclib.net (only the artist, title, album and length are sent); press the button to stop it.
- Also from 0.12.2: the contributors card in *Settings → About* always shows.

### Downloads
- **Linux:** `notch_0.12.3_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.12.3_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.12.3-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).

### Is the Windows warning safe to click through?
Windows SmartScreen says *"Windows protected your PC"* because the installer is not code-signed yet and Notch is new to Microsoft, not because anything was found in it. Click **More info → Run anyway**. To check the file yourself, compare its SHA-256 with `SHA256SUMS-windows.txt` (and `SHA256SUMS-linux.txt` for the `.deb`) on this page: in PowerShell, `Get-FileHash .\Notch-0.12.3-windows-x64-setup.exe`. The code is open and built by GitHub Actions from this repository.
