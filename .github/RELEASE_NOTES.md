## Olyzano Notch 0.16.1

Choose which monitors show Notch and how each one looks, clearer *Keep* on the shelf, hide Notch in full-screen apps without Gaming mode, a crash fixed, and Chrome (or any program you open from Notch) no longer counted as Notch's memory.

### New
- **Monitors** (*Settings → General → Monitors*): Notch shows the monitors that are connected, laid out the way they stand, and a click on one switches Notch off or on there: the notch, the status bar and the dock together. A monitor you plug in appears at once, and at least one monitor always keeps Notch.

- **Settings for each monitor** (same card): click a monitor to choose it, then give it its own capsule size, capsule mode (capsule or hidden), dock and status bar, or leave each one *As general*. A big capsule and a dock on the main screen, a small capsule and nothing else on the second one.

- **Hide Notch when an app is full screen** (*Settings → General*): the notch, the status bar and the dock step aside while an app or game fills the screen, and come back when it does not. Unlike Gaming mode (the gamepad button) it keeps sounds and reminders on and leaves the tray menu working: *Show Olyzano Notch over this app* (or a click on the tray icon) brings Notch up over the app until it leaves full screen. Off by default.

### Improved
- **Keep is easier to see on the shelf:** every file shows the days it has left (`1.2 MB · 5d`) or *Kept*; the sort menu has **Expiring first** (kept files last); keeping or releasing many files at once runs in steps and no longer stalls the panel. The Settings note for *Keep files for* says kept files are never removed.
- **The chosen option is easier to read** in every switch of choices (Timer / Pomodoro / Stopwatch, Mirror / Photo booth, and the rest): it is now the solid accent colour with text that contrasts with it, instead of a pale tint with accent-coloured text.

### Fixed
- **A crash** when icons were drawn (in the icon theme, `KIconLoader`): the icons of files, dock apps and tray items are now made on the main thread, where the theme is safe to use.
- **Programs started from Notch** (the dock, the app search, the voice assistant) now run in a systemd scope of their own, the way KDE starts applications. A browser opened from the dock no longer shows up as *Olyzano Notch, 2.6 GiB* in System Monitor, and it no longer keeps Notch's unit "running" after Notch quits or crashes.
- **Helpers that are left behind:** `pactl subscribe` and `parec` now end together with Notch, so a crash leaves no stray process (and no second "Olyzano Notch" in System Monitor).

### Downloads
- **Linux:** `notch_0.16.1_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.16.1_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.16.1-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools, ffmpeg, LibreOffice): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).

### Is the Windows warning safe to click through?
Windows SmartScreen says *"Windows protected your PC"* because the installer is not code-signed yet and Notch is new to Microsoft, not because anything was found in it. Click **More info → Run anyway**. To check the file yourself, compare its SHA-256 with `SHA256SUMS-windows.txt` (and `SHA256SUMS-linux.txt` for the `.deb`) on this page: in PowerShell, `Get-FileHash .\Notch-0.16.1-windows-x64-setup.exe`. The code is open and built by GitHub Actions from this repository.
