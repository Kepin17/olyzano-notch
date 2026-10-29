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

**Updating.** *Settings → About → Updates* says when a newer version is out. **Update now** downloads the installer or package, checks its SHA-256 against the release's checksum file, installs it and starts the new version (Windows: the installer runs silently; Linux: your system asks for your password to install the `.deb`). The portable Windows zip and a copy you built yourself cannot update themselves: use *Download* there.

**Search from the notch.** Press **Shift + Space** (change it in *Settings → General → Search from the notch → Shortcut*), type the name of an app, and the matches drop down from the notch; **Enter** opens the highlighted one (arrows move, **Ctrl+1…7** open a row at once, **Esc** closes). With nothing typed it lists the apps you opened lately. On KDE Plasma it is a KWin shortcut called *Search apps (Notch)*, which can also be changed in *System Settings → Shortcuts*. On other Linux desktops bind a key to `qdbus6 org.olyzano.Notch /Launcher openSearch`. *Settings → General → Search from the notch* switches it off.

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

**Your own server.** In *Settings → Sharing & Nearby* the **How to set up a server** button opens a step-by-step guide (domain, `docker compose`, the key, adding it to Notch, and how to give other people their own keys), with the commands one click from your clipboard. The same steps are in `server/README.md`.

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

**Camera: a mirror and a photo booth.** Pick *Camera* in the bar. *Mirror* shows you live (to check yourself before a call); the camera is on only while that page is open, with a red "Camera on" mark, and nothing is sent anywhere. *Photo booth* counts 3-2-1 and takes a *Single* picture, a *Strip of 4*, a *Polaroid* or one in a *Frame* of your theme's colour, with a filter (Normal, Black & white, Warm, Vivid). The picture is saved to Files straight away; **Share (QR)** sends it to your phone, **Retake** starts over. It needs Qt's multimedia module (Debian/Ubuntu: `qml6-module-qtmultimedia`, recommended by the package). In *Low memory mode* the filter shows only in the finished picture.

### Tasks, clipboard, system

- **Tasks.** Pick *Tasks* in the bar at the top of the open panel: a month on the left (a dot under every day with something to do, red when it is late) and the list on the right, nearest first. Tap a day to see only that day. **New** opens a small window for a name, a deadline and a time; a line like `Buy milk fri 17:00` is understood too. The nearest deadline shows in the closed capsule, and a note comes 15 minutes before and at the deadline.
- **Clipboard history.** Off until you turn it on (*Settings → General → Clipboard*, or the button on the *Clipboard* page), because a clipboard often holds a password. Then the text and pictures you copy are kept on this computer, newest first: click one to copy it again, pin it, or keep it in Files. Copies marked secret by password managers are skipped, and the history forgets after a day, a week or never.
- **System in Quick Settings.** Open Quick Settings in the status bar: the top of the card shows how busy the computer is (processor with its temperature, memory, graphics card, disk, network); tap a row for details and the busiest programs. *Settings → Panel → Quick Settings* chooses which parts the card has.
- **Make it yours.** *Settings → Appearance* has *Interface size* (small to extra large), *Corners* (sharp to round), the theme and *Glass*. *Settings → General → Performance → Low memory mode* draws without the graphics card and needs about 90 MB instead of 130 MB (Linux; on Windows it skips the graphics driver, where most of the idle memory goes — measure with `scripts\memcheck.ps1`); restart Notch to apply it.

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
