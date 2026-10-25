# Notch

**A temporary file shelf that lives at the top edge of your screen.**
Move the mouse (or a dragged file) to the top-centre and it slides out. Drop files on it, take them
back out whenever you need them, send them to a phone with a QR code. Files are **copied** into
Notch's own storage and cleaned up automatically after a few days; your originals are never touched.

Runs on **Linux (KDE Plasma, Wayland or X11)** and **Windows 10 / 11**.

- [Install](#install): Linux · Windows · [optional components](#optional-components)
- [Features](#features)
- [Using Notch](#using-notch) · [the dock](#the-app-dock) · [live activities](#live-activities-and-the-idle-clock) ·
  [timer & Pomodoro](#timer-and-pomodoro) · [stretch reminder](#stretch-reminder)
- [Share via QR](#share-via-qr)
- [Settings and storage](#settings-and-storage)
- [Build from source](#build-from-source)
- [Project layout](#project-layout)

---

## Install

The easiest way is the **[install page](https://kepin17.github.io/olyzano-notch/)**, which links the newest download for your system.

### Linux

**Needs** a 64-bit distribution with **Qt 6.5 or newer**: Debian 13, Ubuntu / Kubuntu 25.04 or newer, KDE neon.
Made for **KDE Plasma** (Wayland or X11); other desktops work with fewer extras (see [what works where](#what-works-where)).
Tested on Kubuntu 26.04 (Qt 6.10, KDE Plasma on Wayland).

**1. Download** `notch_<version>_amd64.deb` from the **[install page](https://kepin17.github.io/olyzano-notch/)** or the
**[latest release](https://github.com/Kepin17/olyzano-notch/releases/latest)**, or from a terminal:

```bash
wget https://github.com/Kepin17/olyzano-notch/releases/download/v0.7.1/notch_0.7.1_amd64.deb
```

**2. Install** it. `apt` pulls in everything it needs (Qt 6 runtime, `layer-shell-qt`, Poppler, Ghostscript,
`gocryptfs`, `fuse3`, `pulseaudio-utils`, …):

```bash
sudo apt install ./notch_0.7.1_amd64.deb
```

**3. Start** **Notch** from the application menu, or turn on **Settings → General → Start at login**.
For OCR, KDE Connect and the rest, see [optional components](#optional-components).

**Update:** install the newer `.deb` the same way, then restart Notch (`pkill notch; notch &`). Your shelf and settings stay.

**Remove:** `sudo apt remove notch`. Your shelf is kept; delete it yourself with `rm -r ~/.local/share/notch ~/.config/notch`.

> On Wayland the shelf is pinned to the real top edge through `layer-shell-qt`. Without it the app still
> runs but cannot sit over the panel. Details in [docs/wayland-limitations.md](docs/wayland-limitations.md).

### Windows

1. Download **`Notch-<version>-windows-x64-setup.exe`** (installer) or **`Notch-<version>-windows-x64-portable.zip`**
   from the repository's **Releases** page (a `v*` tag builds one), or from the latest **Windows** run under *Actions →
   Artifacts*.
2. Run the installer. It installs for your user (no administrator prompt), adds a Start-menu entry and can
   start Notch when you sign in. The portable zip needs no install: extract it anywhere and run `notch.exe`.
3. The build is not code-signed, so Windows SmartScreen says *"Windows protected your PC"*:
   click **More info → Run anyway**.

**Good to know**

- Sharing over WiFi: Windows asks once to let Notch through its firewall. Allow it on **private** networks.
- PDF and OCR tools need Poppler, Ghostscript and Tesseract on your `PATH`: download links and steps are in
  [optional components](#optional-components). The image tools need nothing.
- Settings and files live in `%LOCALAPPDATA%\notch`.

**Remove:** *Settings → Apps → Notch → Uninstall* (installer), or delete the folder (portable).
Delete `%LOCALAPPDATA%\notch` too if you want your shelf gone.

### Optional components

Notch runs without these; each one switches on a feature. Notch finds them by itself once they are installed
(restart Notch after installing).

| Component | What it adds | Linux | Windows |
|---|---|---|---|
| **Tesseract OCR** + a language | *OCR* in the Tools menu (text from images and scanned PDFs) | `sudo apt install tesseract-ocr tesseract-ocr-eng`<br>(more languages: `tesseract-ocr-ind`, `tesseract-ocr-deu`, …) | [UB Mannheim installer](https://github.com/UB-Mannheim/tesseract/wiki): tick the languages you need, then add `C:\Program Files\Tesseract-OCR` to `PATH` |
| **Poppler** | PDF page count, split, extract, PDF → image, OCR of PDFs | included in the `.deb` (`poppler-utils`) | [poppler-windows releases](https://github.com/oschwartz10612/poppler-windows/releases/latest): unzip, add its `Library\bin` folder to `PATH` |
| **Ghostscript** | *Compress PDF* | included in the `.deb` (`ghostscript`) | [Ghostscript downloads](https://ghostscript.com/releases/gsdnld.html) (64-bit AGPL release); its installer adds it to `PATH` |
| **KDE Connect** (computer + phone) | *Send to device*, files from your phone, incoming calls, phone notifications and a low phone battery in the notch | `sudo apt install kdeconnect` and on the phone [KDE Connect for Android](https://play.google.com/store/apps/details?id=org.kde.kdeconnect_tp) ([F-Droid](https://f-droid.org/packages/org.kde.kdeconnect_tp/)) or [iOS](https://apps.apple.com/app/kde-connect/id1580245991); pair them once | – (Linux only) |
| **KDE Plasma's KWin** | Dock: real running apps, a click raises or minimizes the app's window, the window list | part of Plasma | built in (Windows windows) |
| **Plasma blur effect** | Frosted-glass blur behind the dock | *System Settings → Window Management → Desktop Effects → Blur* | – |
| **Spectacle** auto-save | *Add new screenshots* | in Spectacle: *Configure → Save → save automatically* (Notch has a button for it) | built in (Win + PrtSc) |
| **Dolphin** / `ktrash6` | Dock Trash: open and empty it | part of Plasma (`sudo apt install dolphin kio-extras`) | built in (Recycle Bin) |
| **PipeWire or PulseAudio** (`pactl`) | Equalizer, the microphone indicator and its mute state | included in the `.deb` (`pulseaudio-utils`; works with PipeWire) | – |
| **Noto CJK fonts** | The Washi theme's serif headings (和紙) | included in the `.deb` (`fonts-noto-cjk`) | [Noto Serif JP](https://fonts.google.com/noto/specimen/Noto+Serif+JP) (optional) |

**Adding a folder to `PATH` on Windows:** Start → type *environment variables* → *Edit the environment variables for your
account* → select **Path** → **Edit** → **New** → paste the folder → OK. Then restart Notch.

---

## Features

### What works where

| | Linux | Windows |
|---|:---:|:---:|
| The shelf: drop, preview, categories, notes, search, tools | ✅ | ✅ |
| Several monitors (one shelf per screen) | ✅ | ✅ |
| Share via QR (WiFi, and your own encrypted server) | ✅ | ✅ |
| Nearby: send to another computer running Notch | ✅ | ✅ |
| Auto-add new screenshots and downloads | ✅ | ✅ |
| Charger and low-battery alerts | ✅ | ✅ |
| Start at login · tray icon | ✅ | ✅ |
| 8 themes (Classic, Washi 和紙, Frost, Ocean, Forest, Sunset, Lavender, Mocha), light / dark | ✅ | ✅ |
| App dock: pinned and running apps, folders, Trash, drop files on apps | ✅ | ✅ |
| Dock: click raises the app's window, window list | ✅ (KDE Plasma) | ✅ |
| Dock: unread badges and progress from apps · blur | ✅ | – |
| Live activities: timer, downloads, Nearby transfers in the closed notch | ✅ | ✅ |
| Timer, stopwatch and Pomodoro | ✅ | ✅ |
| Stretch reminder (counts real work time) · idle clock | ✅ | ✅ |
| Phone calls and notifications (KDE Connect) · microphone / camera indicators | ✅ | – |
| Locked (encrypted) categories | ✅ | – |
| Now-playing card with a real-sound equalizer | ✅ | – |
| Bluetooth alerts · KDE Connect · colour picker | ✅ | – |

Controls that a platform cannot offer are hidden or greyed out.

### At a glance

| Area | What you get |
|---|---|
| **Adding** | Drop files, folders (sorted into categories by folder), browser images and links, plain text (becomes a note); paste with Ctrl+V; new screenshots and downloads are picked up for you |
| **Organizing** | Categories you can drag files onto, multi-select, rename, sort, rules that file new PDFs / images / … into a category, search inside text files |
| **Safety nets** | Undo, a 24-hour *Recently removed* bin, an "expiring soon" badge with **Keep**, names that never overwrite ("name (1).ext") |
| **Looking** | Preview, Quick Look (hold Space), an in-place note editor with a small toolkit |
| **Working** | Merge / split / compress PDFs, images → PDF, convert and resize images, OCR, zip, save to folder |
| **Sharing** | QR code to a phone over WiFi or through your own server, with PIN or password and an expiry; several shares at once |
| **Privacy** | Locked categories encrypted on disk (Linux); server shares are end-to-end encrypted; dots show when the microphone or a camera is on |
| **Dock** | A macOS-style dock on the bottom, left or right edge: pinned and running apps, folders as stacks, Trash, badges, glass look |
| **Focus** | Timer, stopwatch, Pomodoro, and a stretch reminder that only counts the time you actually work |

---

## Using Notch

### The shelf

- **Open:** rest the mouse on the top-centre edge (a faint line marks the spot), or drag files there. It closes
  when the mouse leaves; **Pin** keeps it open; **Esc** closes it.
- **Every monitor has its own shelf** at its top edge; it opens wherever you move the mouse.
- **Tray icon:** click to show or hide; the menu has Show/Hide, Settings and Quit.
- **A tile:** click to preview (text files open in the note editor). Hover for **↗** (open) and **✕** (remove,
  deletes only Notch's copy). Right-click for the full menu. Drag a tile out to a folder or an app to copy it there.
- **Select several:** Ctrl+click toggles, Shift+click selects a range, Ctrl+A selects all in view; Delete removes,
  Esc clears. Dragging a selected tile drags the whole selection.

### Adding things

| You do | You get |
|---|---|
| Drop files | Copies in Notch. A name already on the shelf becomes "name (1).ext" |
| Drop a **folder** | Every file inside (hidden ones skipped, up to 1000); each goes into a category named after its folder |
| Drag an **image or link** from a browser | The image is downloaded; a page link becomes a shortcut that reopens the page |
| Drop **text** from any app | A new note |
| **Ctrl+V** after clicking Notch | Copied files; an image from the clipboard; a link (downloaded); other text becomes a `.txt` / `.json` / code file |
| Take a **screenshot** | Added to a *Screenshots* category (Settings → Add new screenshots; Windows: Win + PrtSc) |
| Finish a **download** | Added to *Downloads* (off by default) |

Files dropped while a category is open, or dropped on its chip, go into that category.

### Organizing

- **Categories:** chips under the header. **+** adds one; right-click a chip to rename or delete (files stay).
  Drag tiles onto a chip to move them, or drag a chip to a chat / file manager to send everything in it.
- **Sort** (the pill at the end of the chip row): date, name, size or type.
- **Sort new files by type** (Settings): new PDFs, images, video, audio, documents, archives or code go to the
  category you choose. An open category, folder drops and the special categories still win.
- **Search** finds file names and the text inside notes and text files (up to 512 KB).
- **Rename:** right-click → *Rename…* (or F2). The extension stays unless you change it.
- **Keep:** right-click → *Keep (never auto-delete)* marks files with ★. Files due within a day show an orange
  ⏳ badge.

### Removing and undo

Removing a file moves it to **Recently removed** (the bin button in the header) for 24 hours, then it is deleted
for good. **Undo** in the toast or Ctrl+Z brings back the last removal. In the bin, ↺ restores and ✕ deletes now.

### Looking and editing

- **Quick Look:** hover a tile and hold **Space** (or press and hold the tile) for a large preview.
- **Notes:** **✎** opens an editor (name, `.txt` or `.md`, monospace). **Save** or Ctrl+S stores it as a normal file in the
  category you are viewing. Clicking any text file opens it in the same editor. A small toolkit formats JSON
  (key order kept), encodes / decodes Base64 and URLs, and changes case, on the selection or the whole note.

### Tools

Right-click → **Tools…** (or **Tools** while selecting). New files appear next to the originals.

- **PDF:** merge in the order you picked, images → PDF (one A4 page per image), compress, split into pages,
  extract pages ("1-3, 5, 8-").
- **Images:** convert to PNG / JPG / WebP, resize to 1920 / 1280 / 800 px wide (never upscales).
- **OCR:** copy the text, or make it a note, from an image or a scanned PDF.
- **Zip:** *Compress to .zip*, or *Zip category*. **Save to…** copies files out to a folder without overwriting.
- **Copy** puts files on the clipboard as files; *Copy contents* copies a text file's text; *Copy path* the path.

### Appearance and settings (⚙)

- **Theme:** eight looks, each in Dark, Light or Follow system: **Classic** (follows your KDE accent colour),
  **Washi 和紙** (sumi ink / washi paper, a vermilion accent, serif headings), **Frost**, **Ocean**, **Forest**, **Sunset**,
  **Lavender** and **Mocha**. Pick one from the swatches in *Settings → Appearance*.
- Settings are grouped in tabs: **General · Files · Dock · Wellbeing · Sharing · Appearance**.
- **Keep files for** 1–30 days (shortening it removes older files right away), the storage folder (changeable while the
  shelf is empty), alerts, start at login, automatic screenshots and downloads, sharing servers.

---

## The app dock

A dock of its own on the **bottom, left or right** edge of every monitor (*Settings → Dock*).

- **Shows** your pinned apps, a divider, the other running apps (a dot under them; the app in front gets a longer accent
  pill), then the **Trash**. It hides past the screen edge and comes back when the pointer touches it, and stays while the
  pointer is anywhere over it; or switch on *Always show*.
- **Click** an app to bring its window forward, again to minimize it; apps with several windows cycle through them.
  Not running: it starts. **Middle-click** opens a new window. **Right-click:** *Show / Open*, *New window*,
  *Keep in Dock* / *Remove from Dock*, *Quit*.
- **Rest on** an app with several windows to list them by title; click one to switch to it.
- **Drag** a pinned icon to reorder it, or off the dock to remove it; drag a running app among the pinned ones to keep it.
  **+** adds an installed app, *Downloads* or any folder.
- **Folders** open as a stack of their newest files: click one to open it, or drag it out to another app.
- **Drop files** on an app's icon to open them with it, or on the **Trash** to move them to the trash.
  Right-click the Trash to empty it (after a confirmation).
- **Badges:** unread counts and progress that apps report (Telegram, Thunderbird, Discord, …) show on their icons.
- **Look:** icon size, magnification, transparency, blur, animation (off / smooth / bouncy), position.

On KDE Plasma the dock learns the open windows from KWin; on other Linux desktops it can only start apps and guesses
which are running. Window thumbnails aren't possible: Wayland doesn't let an app capture other apps' windows.

## Live activities and the idle clock

While the notch is closed it shows what is going on as a small pill, the most important first:
an **incoming call** (KDE Connect), a **break** of the stretch reminder, the **timer**, a **Nearby transfer** with its
percentage, a **browser download** still being written to Downloads (size and speed), or files being **copied**.
A hairline along the bottom shows the progress.

When nothing is going on, the **idle clock** shows the time, the **microphone** (red and struck through when muted,
orange while an app records) and the **battery**. Rest the pointer on it to open Notch. Turn it off in
*Settings → General → Clock when idle*.

**Privacy dots:** a green dot beside the notch while a camera is on, an orange one while an app records the microphone;
the open panel names the apps. **Phone:** incoming and missed calls, the phone's notifications ("Notification from
WhatsApp · Mom", never the message itself) and a low phone battery, through KDE Connect.

## Timer and Pomodoro

Open Notch and press the **clock button** in the header.

- **Timer:** 1 to 60 minutes with one click, or any number of minutes and a name ("Tea").
- **Pomodoro:** 25 or 50 minutes of focus, 5 / 10 / 15 minutes of break, round after round until you stop it.
- **Stopwatch:** counts up.

The running one shows in the closed notch; open the page again to pause, add a minute or stop. A toast says when time is up.

## Stretch reminder

*Settings → Wellbeing.* Notch counts the time you **actually work** (mouse and keyboard). Stepping away for a few minutes
counts as a break and starts over. After 50 minutes of work it asks:

> 🧘 **Time to stretch** — **Start break** · **Later (10 min)** · **Skip**

You always decide; unanswered for a minute counts as *Later*. A break counts down in the notch with stretch suggestions;
end it early from the open panel. It holds back while a video plays, during a call and during a Pomodoro (each can be
switched off). Optional: a **20-20-20** eye reminder. The *Today* card shows your work time and breaks.

Work time, break length, the idle threshold and *Later* are adjustable. The idle time comes from KDE's KIdleTime (or the
ScreenSaver D-Bus service) on Linux and from Windows itself.

---

## Share via QR

Right-click a file (or select several and press **Share**) → **Share via QR…**, then scan the code with a phone.

### Two ways to share

| | **This WiFi** | **My server** |
|---|---|---|
| How | Notch serves the files on your local network | Encrypted on your computer, then uploaded to your own relay |
| Needs | Phone and computer on the same WiFi | A relay you run ([server/](server/README.md)); your computer may then be switched off |
| Encryption | None (plain HTTP on your own network) | End-to-end (AES-256-GCM); the key stays in the link after `#`, so the server cannot read anything |
| Ends when | One file: after its first download. Several: when it expires or you press Stop | It expires, or after the first download if you choose *Single download* (one file) |

Expiry: 10 minutes, 1 hour or 24 hours.

### Protect it

- **PIN** (4–12 digits): checked by the server (or by Notch for WiFi shares). Five wrong tries lock that phone out
  for 10 minutes.
- **Password** (8+ characters): on a server share it also locks the key inside the link, so the link alone opens
  nothing. Tell the password another way.
- File names are only shown after the PIN / password.

### Several shares at once

Every share is its own session. The share button in the header (with the number running) opens the list: state,
countdown, **QR**, **Stop**, **Stop all**, **Clear finished**. Server shares stay on the server, so they are still in
the list after you restart Notch; WiFi shares end with Notch.

### Servers

**Settings → Sharing** keeps any number of servers (yours, a friend's, your team's). Add one with its address and API key and
press **Test**. When you share, pick the server; the popup shows what it allows and greys out what it would refuse.
Members with their own keys and quotas exist on the relay
([server/README.md](server/README.md#for-a-team-members-quotas-invitations)), but joining from inside Notch is switched off for now.

### What the person who scans sees

A phone-friendly page (light / dark). For several files it lists them with thumbnails, sizes and a live countdown;
they can download one file, tick a few, or take everything as a zip (built on their phone, nothing extra is stored).

---

## Nearby: send to another computer

Computer to computer, like Quick Share, between two computers running Notch (Linux or Windows) on the same network. Phones use the QR page instead.

1. **Receiver:** open the Share window and press **Receive** (or use the tray menu). The computer is visible as its device name for 10 minutes, then hides again. It is invisible to the network the rest of the time.
2. **Sender:** right-click files → Share via QR → **Nearby**, pick the computer, press **Send**.
3. Both screens show the **same 4-digit number**. If they match, the receiver presses **Accept**; the files land in a **Nearby** category.

Files are encrypted on the way. The number is derived from a key exchange in which the sender commits to its key first, so someone in the middle cannot make both screens agree. Three declined or broken requests from one address are ignored for 5 minutes, and only one transfer is accepted at a time.

Limits: it only talks to Notch (not Google's Quick Share), it needs both computers on one network (guest WiFi often isolates devices), and a firewall may need to allow Notch (Windows asks once; with `ufw`, open UDP 47655 and Notch's transfer port). The device name is in Settings → Nearby.

---

## Linux extras

- **Locked categories:** right-click a chip → **Lock with password…**. Files in it are encrypted on disk with
  [gocryptfs](https://nuetzlich.net/gocryptfs/), never expire and stay hidden until you unlock. The password has at least 8
  characters and **cannot be recovered**. Everything locks again a few seconds after Notch closes, when it quits, or with
  the unlock button in the header. *Remove lock* moves files back to normal storage. Locked files can be shared too.
- **Now playing:** when YouTube (any browser tab), Spotify, Elisa or another MPRIS player plays or pauses, an equalizer
  button appears. It opens a player with cover, title, a seekable bar and previous / play / next. The bars follow the real
  sound through `parec`, only while they are on screen; levels are computed, nothing is stored.
- **Alerts:** plugging the charger in or out, a Bluetooth device connecting, and low battery (20 %, 10 %, 5 %) make Notch
  peek out. Read from UPower and BlueZ; nothing is polled. (On Windows: charger and low battery only.)
- **KDE Connect:** right-click → **Send to device ▸**; files a phone sends arrive in a *KDE Connect* category.
- **Colour picker:** the palette button lets you click any pixel on screen; the hex code is copied and kept in a swatch row.

---

## Settings and storage

| What | Linux | Windows |
|---|---|---|
| Files | `~/.local/share/notch/files/<uuid>/<name>` | `%LOCALAPPDATA%\notch\files\…` |
| Metadata | `~/.local/share/notch/notch.db` (SQLite) | `%LOCALAPPDATA%\notch\notch.db` |
| Settings | `~/.config/notch/notch.conf` | `%LOCALAPPDATA%\notch\notch.conf` |
| Dock, live activities, wellbeing | `~/.config/notch/dock.conf`, `activities.conf`, `wellbeing.conf` | same folder as `notch.conf` |
| Locked files | `~/.local/share/notch/vault/` (encrypted; plain view only in RAM while unlocked) | – |
| Server keys | `~/.config/notch/servers/*.key` (owner-only) | same folder, protected by your user profile |
| Start at login | `~/.config/autostart/notch.desktop` | `HKCU\…\Run` |

Cleanup runs at start-up, hourly and when the retention changes; it removes items older than the retention period
(default 7 days). Deletion is crash-safe (mark → delete file → delete row) and limited to Notch's own folder. If the
database cannot be read, Notch shows an error and deletes nothing.

---

## Build from source

**Linux**

Needs Qt 6.5 or newer (Debian 13, Ubuntu 25.04+).

```bash
# required
sudo apt install cmake ninja-build g++ libssl-dev qt6-base-dev qt6-base-private-dev qt6-declarative-dev
# optional, each found automatically by CMake
sudo apt install liblayershellqtinterface-dev   # Wayland: pin the notch and the dock to the screen edges
sudo apt install libkf6windowsystem-dev         # blur behind the dock on KDE Plasma
sudo apt install libkf6idletime-dev             # stretch reminder: idle time on Wayland (else the ScreenSaver D-Bus API)

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/notch
cd build && cpack                               # notch_<version>_amd64.deb
```

The configure output says which optional parts were found (e.g. *KF6WindowSystem found: the dock can blur what's behind it*).
At run time the app also uses the [optional components](#optional-components).

Try it without touching your real shelf:
`XDG_DATA_HOME=/tmp/notch-test XDG_CONFIG_HOME=/tmp/notch-test-config ./build/notch`.
Add `-DNOTCH_NO_DBUS=ON` to build the variant Windows gets (no D-Bus integrations) and try those code paths on Linux.

**Windows**: Qt 6.8 (MSVC 2022), CMake, Ninja and OpenSSL (`vcpkg install openssl:x64-windows`). The exact steps, including the
installer, are in [`.github/workflows/windows.yml`](.github/workflows/windows.yml).

**The relay server** (Docker Compose, nginx or Caddy, members and quotas) is documented in [server/README.md](server/README.md).

---

## Project layout

```
src/models         FileItem, FileItemModel (QAbstractListModel for QML)
src/repositories   FileRepository: SQLite and migrations (database/migrations)
src/services       FileService, CleanupService, DocTools, ZipWriter, Vault,
                   ShareServer, ShareCrypto, QR rendering, invitation parsing
src/app            NotchController, ShareController / ShareSession: the QML bridges
src/platform       DesktopIntegration (layer-shell / plain-window placement, input region),
                   D-Bus integrations on Linux, small stubs elsewhere, Theme (palettes),
                   AppDock + DockModel (the dock), Activities (timer, downloads, privacy),
                   Wellbeing + WellbeingModel (stretch reminder)
src/qml            Main.qml (hot edge, shelf, menus, live pill), Dock.qml, TimerPage.qml, pages and controls, icons
server/            The share relay (Python, Docker) and the download page it serves
packaging/         .desktop file, app icon, Windows installer script
third_party/       qrcodegen (QR codes, MIT)
tests              tst_core: storage, copy, cleanup, tools, zip, sharing, crypto, dock, stretch reminder
```
