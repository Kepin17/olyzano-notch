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
