# Olyzano Notch: user guide

A quick, task-by-task guide for version 0.11. The [README](../README.md) has every detail and table; this page tells you
where to start. Developers: see the [developer guide](developer-guide.md).

## 1. Install and start

| System | Do this |
|---|---|
| **Linux** (KDE Plasma; Debian 13 / Ubuntu 25.04+, Qt 6.5+) | Download `notch_<version>_amd64.deb`, then `sudo apt install ./notch_<version>_amd64.deb`. |
| **Windows 10 / 11** | Run `Notch-<version>-windows-x64-setup.exe` (no admin prompt), or unzip the portable `.zip` and run `notch.exe`. SmartScreen: *More info → Run anyway* (the build is not signed). |

Start **Olyzano Notch** from the application menu. Turn on *Settings → General → Start at login* to have it ready every day.
Update by installing the newer package and restarting the app; your shelf and settings stay.
Optional extras (OCR, KDE Connect, PDF tools on Windows): [optional components](../README.md#optional-components).

## 2. The shelf: your temporary file holder

1. **Open it:** rest the mouse on the top-centre edge, or drag files there. **Pin** keeps it open, **Esc** closes it.
2. **Add things:** drop files, folders, browser images/links or text; **Ctrl+V** pastes; new screenshots can be picked up
   automatically (*Settings → Add new screenshots*). Files are *copied*; your originals are never touched.
3. **Take things back out:** drag a tile to a folder or app, or right-click → *Save to…* / *Copy*.
4. **Tidy up:** categories (the chips under the header), sort, search (names and text inside notes), *Keep* (★) to stop auto-delete.
5. **Mistakes:** removed files wait 24 hours in *Recently removed*; **Ctrl+Z** undoes the last removal.

Files are deleted after the retention period (default 7 days; *Settings → Keep files for*). A ⏳ badge means *due within a day*.

## 3. Tools on files

Right-click → **Tools…**: merge, split, extract and compress PDFs; images → PDF; convert or resize images; OCR; zip. Results
appear next to the originals.

## 4. Share a file with a phone or another computer

- **Phone:** right-click a file → **Share via QR…** → pick *This WiFi* (same network, no encryption) or *My server*
  (end-to-end encrypted, needs your own relay: [server/README.md](../server/README.md)). Set an expiry, optionally a PIN or password,
  and scan the code.
- **Another computer running Olyzano Notch:** on the receiver press **Receive**; on the sender choose *Share via QR… → Nearby*,
  pick the computer, check both screens show the same 4-digit number, then **Accept**.

## 5. Focus and wellbeing

- **Timer / stopwatch / Pomodoro:** the clock button in the header; the running one shows in the closed notch.
- **Stretch reminder** (*Settings → Wellbeing*): counts real work time and asks for a break after 50 minutes.
- **Doomscrolling warning** (*Settings → Wellbeing*): when Instagram, TikTok or YouTube Shorts is in front for about 15 minutes
  or 30 videos in the last hour, it asks "Still scrolling?". It looks only at the window in front, what the browser plays and your idle
  time; only the *kind* of site is kept.
- **Gaming mode:** the gamepad button hides the notch and dock and silences sounds while a full-screen app is in front.

## 6. Dock and status bar

- **Dock** (*Settings → Dock*): pinned and running apps on the bottom, left or right edge; click raises or minimizes, drop files on an
  icon to open them with it, drop on the Trash to trash them.
- **Status bar** (*Settings → Panel*, Linux KDE Plasma only): the app in front with its menu on the left, tray and status icons and the
  clock on the right. Click the icons for **Quick Settings** (Wi-Fi, Bluetooth, volume, microphone, brightness, power mode); click the clock for a
  calendar; click the grid button for the **app launcher** (the Windows key can open it).

## 7. Where things are stored

| | Linux | Windows |
|---|---|---|
| Files and database | `~/.local/share/notch/` | `%LOCALAPPDATA%\notch\` |
| Settings | `~/.config/notch/` | `%LOCALAPPDATA%\notch\` |

To remove everything, uninstall and delete those folders. Full table: [Settings and storage](../README.md#settings-and-storage).

## 8. If something does not work

| Symptom | Try |
|---|---|
| The notch does not sit over the top panel (Wayland) | Install `layer-shell-qt`; see [wayland-limitations.md](wayland-limitations.md). |
| Status bar, app menu or tray missing | They are Linux + KDE Plasma only for now. |
| WiFi share cannot be reached from the phone | Same network? Guest WiFi often isolates devices; allow the firewall prompt on Windows. |
| OCR / PDF tools greyed out | Install Tesseract / Poppler / Ghostscript and restart: [optional components](../README.md#optional-components). |
| "Windows protected your PC" | The installer is unsigned: *More info → Run anyway*. |
