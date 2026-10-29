## Olyzano Notch 0.15.0

Dev hub in the action bar, a camera photo booth, notch search, and a faster app.

### New
- **Dev hub** (action bar → *Dev*): **API**, **SSH** and **Database** behind one switcher; the notch stays open while you work.
- **API tester:** HTTP client with collections, folders, environments (`{{baseUrl}}` / `{{relayKey}}`), history, pretty JSON, pre-request and test scripts, Postman Collection v2.1 / Environment import-export, and a **Ports** tab (listener / process / PID, stop, control command).
- **SSH manager:** save hosts, **persistent terminal** in the notch (`ssh -tt`, live shell, `clear`), test connectivity (key/agent preferred), open a system terminal. Passwords never go on the command line.
- **Database browser:** SQLite always; MySQL/MariaDB and PostgreSQL when the Qt SQL driver is installed — databases + tables side by side, browse rows, run SQL with timing.
- **Camera photo booth** (action bar → *Camera*): take frames, pick a template and filter, compose one picture and keep it on the shelf. The privacy dot shows while the camera is on.
- **Search from the notch** (Shift + Space): Spotlight-style field with matching apps underneath — the same search works from the top edge on Windows too.
- Clock colour can follow the theme accent (*Settings → Appearance → Clock follows theme colour*).

### Improved
- **Faster app:** Settings sections and idle pages load only when opened; album art is served from a cover cache instead of re-reading files; menu and button hover/press states are solid theme colours (no system-white flash).
- Share: guide for setting up the sharing server, plus a clearer layout around server URLs.
- The reserved top strip on Windows is filled with a visual bar instead of bare wallpaper; tray menu gains Settings shortcuts.
- The notch and dock re-assert themselves on Windows when another app steals z-order.

### Fixed
- Dev section: menus, modals and the database form no longer show a white system background or an empty panel; the panel is wide enough for the database browser (no cut-off).
- Confirmation toasts stick to the bottom of the open panel and clear themselves (they used to sit in the page flow or never disappear).
- Contrast of right-click menus, auth picker and dialog buttons on dark and light themes.

### Downloads
- **Linux:** `notch_0.15.0_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.15.0_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.15.0-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).

### Is the Windows warning safe to click through?
Windows SmartScreen says *"Windows protected your PC"* because the installer is not code-signed yet and Notch is new to Microsoft, not because anything was found in it. Click **More info → Run anyway**. To check the file yourself, compare its SHA-256 with `SHA256SUMS-windows.txt` (and `SHA256SUMS-linux.txt` for the `.deb`) on this page: in PowerShell, `Get-FileHash .\Notch-0.15.0-windows-x64-setup.exe`. The code is open and built by GitHub Actions from this repository.

---

## Olyzano Notch 0.14.0

Update from inside Notch, a Bounce setting for the animations, and a simpler music player.

- **Update now (Settings → About).** When a newer version is out, one button downloads the installer (Windows) or the package (Linux), checks its SHA-256 against the release's checksum file, installs it and starts the new version. On Windows the installer runs silently; on Linux your system asks for your password. A copy you built yourself or the portable zip still uses *Download*. Nothing is installed unless the checksum matches.
- **Bounce (Settings → Appearance → Animations):** *Off*, *Soft*, *Normal* or *Bouncy*: how far the capsule and panel overshoot before they settle.
- **The music player is simpler.** The *Lyrics* button at the top is gone: the button at the right of the controls now shows or hides the lyrics. Karaoke (your voice through the speakers with the music) is a small microphone button on the lyrics panel.
- **Linux: much less memory** (since 0.13.1): about 133 MB instead of 154 MB at rest, and about 87 MB with the new *Low memory mode* (Settings → General → Performance). Windows is unchanged.

### Downloads
- **Linux:** `notch_0.14.0_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.14.0_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.14.0-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).

### Is the Windows warning safe to click through?
Windows SmartScreen says *"Windows protected your PC"* because the installer is not code-signed yet and Notch is new to Microsoft, not because anything was found in it. Click **More info → Run anyway**. To check the file yourself, compare its SHA-256 with `SHA256SUMS-windows.txt` (and `SHA256SUMS-linux.txt` for the `.deb`) on this page: in PowerShell, `Get-FileHash .\Notch-0.14.0-windows-x64-setup.exe`. The code is open and built by GitHub Actions from this repository.

---

## Olyzano Notch 0.13.1

Linux: much less memory. Nothing else changes; the Windows installer of 0.13.0 is still the current one.

- **About 20 MB less at rest** (the capsule: 154 MB to 133 MB PSS, the memory Notch really owns) from fewer malloc arenas, memory handed back after the panel closes, a smaller worker pool and no helper thread in Mesa.
- **Low memory mode** (*Settings → General → Performance*, Linux, restart to apply): draws with Qt's software renderer instead of the graphics card, about **87 MB** at rest (down from 154 MB), and a little less processor while the panel is open. It also keeps Mesa and LLVM from loading.
- **Settings and the share dialog are unloaded** a minute or two after they are closed (they were ~20 MB for as long as Notch ran). Your place in Settings is kept.
- **A crash-at-exit fixed:** closing Notch while `pactl` was still running could read freed memory (found with AddressSanitizer; no other leak was found in Notch's own code).
- `scripts/memcheck.sh` measures all this on your own machine.

### Downloads
- **Linux:** `notch_0.13.1_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.13.1_amd64.deb`, then restart Olyzano Notch.
- **Windows:** unchanged, see the [0.13.0 release](https://github.com/Kepin17/olyzano-notch/releases/tag/v0.13.0).
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).

---

## Olyzano Notch 0.13.0

A calendar for your tasks, a clipboard history, a System monitor in Quick Settings, a roomier Files shelf, and Settings and glass that look and read better.

### Interface
- **One slider for the pages** (Files, Music, Timer, Tasks, Color, Clipboard). The open page shows its icon, name and count; the others are icons with a count badge. Click, drag across, scroll the wheel or press the arrows.
- **Pointer cursor** on every button and clickable area.
- **Settings is easier to find your way in.** The sidebar is grouped (Basics, On the screen, Files & sharing, You), and *General* is arranged by how the notch sits: one choice (Hidden / Capsule / Widget / Always open) with only the options that belong to it. The duplicate *Dynamic island* switch is gone.
- **Appearance → Size & shape:** *Interface size* (90 to 125 %, text, icons and spacing together) and *Corners* (Sharp, Normal, Round).
- **A new glass look:** a soft sheen from the top, a bright rim along the top edge and a slightly lifted tint. The blur behind the panel now follows the panel's final size.

### New
- **Tasks with a calendar.** A month calendar with a dot on every day that has something to do (red when late), an *Up next* list with a colour bar for how soon, a tap on a day to see just that day, and a window for *New task* (a line of words like "Buy milk fri 17:00" still works).
- **Clipboard history (off by default).** A *Clipboard* page with search, pin, keep-in-Files and an age limit. On KDE Plasma it uses Klipper (so it also works while Notch is not in front); items marked secret by password managers are skipped.
- **System in Quick Settings:** CPU (with temperature), RAM, graphics card, disk and network, with the three busiest programs. Read only while the card is open. Choose which parts of Quick Settings show in *Settings → Panel*.
- **Files shelf as a grid.** As many rows as you have files (it grows to three rows, then scrolls up and down), with no gap at the right edge.
- **Sharing servers, from the app.** The host makes a key for someone (with their username, how long their access lasts and how much space they get, up to 5 GB) in *Settings → Sharing*; the other person pastes it under *Join a server*. Members can be revoked, and the list loads as you scroll. The relay now takes files up to 5 GB (20 GB in all) by default.

### Fixed
- The page names are no longer cut off, and the member and task windows are opaque with Glass on.
- The pill capsule has no shadow and sits on the top edge.

### Downloads
- **Linux:** `notch_0.13.0_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.13.0_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.13.0-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).

### Is the Windows warning safe to click through?
Windows SmartScreen says *"Windows protected your PC"* because the installer is not code-signed yet and Notch is new to Microsoft, not because anything was found in it. Click **More info → Run anyway**. To check the file yourself, compare its SHA-256 with `SHA256SUMS-windows.txt` (and `SHA256SUMS-linux.txt` for the `.deb`) on this page: in PowerShell, `Get-FileHash .\Notch-0.13.0-windows-x64-setup.exe`. The code is open and built by GitHub Actions from this repository.
