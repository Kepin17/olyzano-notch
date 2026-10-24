# Notch

Temporary file shelf for KDE Plasma. It stays hidden at the top center of the screen and slides
out when the mouse (or a dragged file) reaches the top edge. Drop files on it, they are copied into
Notch's own storage, and removed automatically after 7 days. Source files are never modified or
deleted.

## Dependencies (Kubuntu 26.04)

```bash
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-declarative-dev liblayershellqtinterface-dev
```

Runtime (Linux): Qt 6 (Core, Gui, Widgets, Network, Sql + `libqt6sql6-sqlite`, Quick, Controls, Layouts, Shapes, Effects,
Dialogs, `Qt.labs.platform`), `layer-shell-qt`; `gocryptfs` + `fuse3` for locked categories.
Without `liblayershellqtinterface-dev` it still builds, but on Wayland the pill can't be pinned to
the top edge. See [docs/wayland-limitations.md](docs/wayland-limitations.md).

## Build, test, run

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/notch
```

To try it without touching your real shelf or settings:
`XDG_DATA_HOME=/tmp/notch-test XDG_CONFIG_HOME=/tmp/notch-test-config ./build/notch`.

## Use

- Move the mouse to the top edge, center (a faint line marks the spot) and rest a moment; or drag
  files there. The panel closes when the mouse leaves; **Pin** keeps it open. Esc closes.
- Click a tile to preview it (images; other types show their details). Text files open in the note
  editor instead.
  **Open** in the preview header opens it with the default app.
- Hover a tile: **↗** opens, **✕** removes it (deletes Notch's copy only). Hovering shows
  size/added/expiry/source, right-click for more.
- Drag a tile out to a folder or app to copy it there.
- **Select several:** Ctrl+click toggles, Shift+click selects a range, Ctrl+A selects all in view.
  While selecting, clicks toggle. The header then offers **Move to…** / **Remove** / ✕; Delete
  removes, Esc clears. Dragging a selected tile drags the whole selection.
- **Categories:** chips under the header filter the shelf (**All** shows everything). **+** creates
  one; right-click a chip to rename or delete it (its files stay, uncategorized). Move files by
  dragging tiles onto a chip, or right-click → **Move to category…**. Files dropped while a category
  is shown, or dropped onto a chip, go into that category.
- **Folders:** dropping a folder adds every file inside (hidden files/folders skipped, up to 1000
  files). Each file goes into a category named after the folder it sits in, e.g. dropping `Proj`
  puts `Proj/a.txt` in **Proj** and `Proj/sub/b.txt` in **sub**. Same-named categories merge.
- **From the browser:** dragging an image (also one wrapped in a link, e.g. Google Images) or a file
  link downloads it into Notch. Dragging a web page link stores a link shortcut (`.desktop`) that
  opens the page in your browser.
- **Multiple monitors:** every monitor has its own Notch at its top edge; it appears wherever you
  move the mouse. Plugging a monitor in or out adds or removes its Notch. Tray actions open the
  Notch on the primary monitor.
- **Paste (Ctrl+V)** after clicking Notch: copied files are added; an image on the clipboard
  (Spectacle, "Copy image" in a browser) is saved in its own format; a copied link is downloaded;
  other text becomes a file named after its language when the editor reports one (VS Code), `.json`
  for JSON, else `.txt`. Pasted items go into the category being viewed.
- **Copy** (right-click, or Ctrl+C on a selection) puts the files on the clipboard as files, so file
  managers and chat apps attach them; a single image also goes as image data. **Copy contents**
  (text files) copies the text, **Copy path** the stored path.
- **Recently removed:** removing a file moves it to **Recently removed** (the 🗑 button in the header,
  with a count) for 24 hours, then it is deleted for good. **Undo** in the toast (or Ctrl+Z) brings the
  last removal back; in Recently removed, ↺ restores and ✕ deletes now; right-click 🗑 to restore all
  or empty it. Drop tiles on 🗑 to remove them.
- **Quick Look:** hover a tile and hold **Space** (or press and hold the tile) for a large preview;
  release to close.
- **Drop text:** text dragged from any app onto Notch (or onto a chip) becomes a note.
- **Share via QR:** right-click a file (or select several → **Share**) → **Share via QR…**, then scan
  the code with a phone.
  - **This WiFi:** Notch serves the files to your local network for a short time (10 min / 1 h /
    24 h). One file: the link stops after its first download. Several files: it stays open until it
    expires or you press Stop, and the popup counts the downloads. Plain HTTP, so use it on your own
    network.
  - **My server:** the file is encrypted on this computer (AES-256-GCM) and uploaded to your own relay;
    the key stays in the link after `#`, so the server cannot read it, and your computer can be switched
    off. Options: PIN, single download (one file only), expiry. Set it up in **Settings → Sharing** (address + API key,
    **Test connection**); the relay itself (Docker Compose, `.env`) is in [server/](server/README.md).
    The API key is kept in `~/.config/notch/relay.key` (owner-only).
  - **Several at once, and a list:** every share is its own session. The share button in the header (with the number
    running) opens the list: each session shows its state and countdown, **QR** shows its code, **Stop**
    ends it, **Stop all** / **Clear finished** tidy up. Server shares stay on the server, so they are still in
    the list (and can be stopped) after you restart Notch; WiFi shares end with Notch.
  - **Servers (yours, a friend's, your team's):** Settings → Sharing keeps any number of servers. Add one with its
    address and key. Pick the server per share; the popup shows how much space and which limits that server
    gives you, and greys out what it would refuse. (Members with their own keys and quotas exist on the relay
    — [server/README.md](server/README.md#for-a-team-members-quotas-invitations) — but joining from inside Notch
    is switched off for now; it will come back with single sign-on.)
  - **PIN or password:** choose one when sharing. A **PIN** (4–12 digits) is checked by the server (or by Notch for
    WiFi shares): 5 wrong tries lock that phone out for 10 minutes. A **password** (8+ characters) on a server
    share also locks the key inside the link (PBKDF2 + AES-GCM), so the link alone opens nothing; tell the
    password another way. File names are only shown after the PIN / password.
  - **What the person who scans sees:** a phone-friendly page (light/dark, Washi style). For several files
    it lists them with image thumbnails and sizes and a live countdown; they can download one file,
    tick a few, or take everything as a zip (built on their phone, nothing extra is stored). The same page
    serves both modes.
  Files in locked categories can be shared too (the popup warns that WiFi sharing is unencrypted).
- **Locked categories:** right-click a chip → **Lock with password…** for sensitive files (ID cards,
  pay slips). The first time sets the password (at least 8 characters; **forget it and the files are
  gone**, there is no recovery). Files in locked categories are encrypted on disk with
  [gocryptfs](https://nuetzlich.net/gocryptfs/), never expire, and are hidden until you click the 🔒 chip
  and unlock. Unlocking opens all locked categories; they lock again a few seconds after Notch closes,
  when Notch quits, or with the 🔓 header button / **Lock now**. **Remove lock** moves the files back
  to normal storage. Removing a locked file deletes it at once (after a confirmation), it skips
  Recently removed.
- **Expiring soon:** files that will be auto-deleted within a day get an orange ⏳ badge; click
  "· Keep" under the name to keep them.
- **Keep:** right-click → **Keep (never auto-delete)** marks files with ★; cleanup skips them until
  you choose **Allow auto-delete**.
- **Now playing:** when YouTube (any browser tab), Spotify, Elisa or another MPRIS player is playing
  or paused, an equalizer button appears next to ⚙. Click it to open the player: cover, title,
  artist, a seekable progress bar, previous / play-pause / next, ↗ to bring the player to front and
  ⇄ to switch when several are active. The player also pops open for 6 s when playback starts or the
  track changes, then folds back into the button. While Notch is hidden, a new track shows a short
  toast (follows the alerts setting). The equalizer bars follow the real sound: Notch reads the
  output's monitor with `parec` only while bars are on screen and something is playing (Plasma may
  show its recording indicator meanwhile); only levels are computed, nothing is stored.
- **Charger & Bluetooth alerts:** plugging the charger in or out, or a Bluetooth device connecting
  or disconnecting, makes Notch peek out for ~3 seconds (battery level for the laptop and, when the
  device reports it, the headset). **Low battery:** the laptop (while unplugged) and Bluetooth devices
  that report their battery get a red warning when they drop to 20%, 10% and 5%. Turn off in settings. Read from UPower and BlueZ on the system
  D-Bus; nothing is polled.
- **Rename:** right-click → **Rename…** (or F2 on one selected tile) edits the name in place; the
  extension stays unless you change it.
- **Sort:** the **↕** pill at the end of the category row sorts by date added (newest first),
  name, size (largest first) or type. Remembered.
- **Save to folder:** right-click → **Save to…**, **Save…** while selecting, or right-click a category
  chip → **Save category to…**. Copies out, never overwrites ("name (1).ext").
- **Zip:** **Zip** while selecting, right-click → **Compress to .zip**, or **Zip category**. The archive
  becomes a new Notch item (needs `zip`).
- **Screenshots:** new Spectacle screenshots (its save folder, default `~/Pictures/Screenshots`) are
  added automatically into the **📷 Screenshots** category, shown right after **All**. This needs
  Spectacle to save files automatically (off by default); Settings shows a **Turn on in Spectacle**
  button when it isn't (sets `[General] autoSaveImage=true` in `~/.config/spectaclerc`). Screenshots
  that only go to the clipboard can still be pasted with Ctrl+V. Optional, same
  for **⬇ Downloads** (finished files only, off by default). Both in settings.
- **KDE Connect:** right-click → **Send to device ▸** lists your paired devices that are reachable
  right now and sends the file(s) (or the selection) to it. Files a phone/tablet sends to this
  computer are added to the **📱 KDE Connect** category with a toast (setting, on by default); with
  Downloads watching on as well, a received file is still added only once.
- **Many categories:** the chip row scrolls sideways with the mouse wheel/touchpad, fades at the
  edges when more are hidden, and keeps the selected one in view; **+** and **↕** stay put.
- **Quick notes:** **✎** opens an editor for a note or snippet (name, .txt or .md, monospace text);
  **Save** / Ctrl+S stores it as a regular file in the category being viewed, ready to drag anywhere.
  Esc goes back and keeps the draft. Clicking any text file (≤ 1 MB) opens it in the same editor;
  saving updates Notch's copy (and renames it if you changed the name). Right-click → **Edit** too.
- **Tools** (right-click → **Tools…**, or **Tools** while selecting; new files are added next to
  the originals, which stay untouched):
  - **Merge PDFs** in the order you selected them; **Images → PDF** (one A4 page per image, landscape
    pictures get landscape pages, phone photos keep their rotation);
  - **Compress PDF** (Ghostscript, 150 dpi), **Split into pages**, **Extract pages…** ("1-3, 5, 8-");
  - **Copy text (OCR)** / **Text to note (OCR)** from an image or a scanned PDF (Tesseract; English
    data installed by default, add `tesseract-ocr-ind` for Indonesian);
  - **Convert to PNG / JPG / WebP** and **Resize to 1920 / 1280 / 800 px** wide (never upscales);
  - **Compress to .zip**.
- **Sort by type:** Settings → **Sort new files by type** sends new PDFs, images, video, audio,
  documents, archives or code/text into the categories you pick. Dropping into an open category,
  folder drops and the Screenshots/Downloads/KDE Connect categories still win. **Apply to existing
  files** sorts files that have no category yet.
- **Search** matches file names and the text inside notes and text files (up to 512 KB).
- **Duplicate names:** a file whose name is already on the shelf is added as "name (1).ext".
- **Note toolkit:** buttons in the note editor format JSON (key order kept), encode/decode Base64 and
  URLs, and switch to UPPER/lower case — on the selection, or the whole note.
- **Drag a category:** drag a chip to Dolphin or a chat to send all its files at once; drop it on
  another chip to move its files there.
- **Color picker:** 🎨 hides Notch and lets you click any pixel on screen (desktop portal, KWin's
  picker). The hex code is copied and kept in a swatch row; click a swatch to copy again,
  right-click to remove.
- **Appearance:** style **Classic** (highlights use your KDE accent color) or **Washi 和紙** (Japanese:
  sumi ink when dark, washi paper when light, vermilion *shu* accent, hanko-style count, Noto Serif JP
  headings); each in Dark (default), Light or Follow system.
- **⚙ Settings**: how long files are kept (1–30 days; shortening it removes older files right away),
  storage folder (changeable while the shelf is empty), start at login, quit.
- Tray icon: click toggles the panel; menu has Show/Hide, Settings, Quit.
- Plain-text drops are not supported.

## Windows

Notch also runs on Windows 10 / 11 (64-bit). Download `Notch-<version>-windows-x64-setup.exe` (installer: Start menu,
optional "start with Windows", uninstall) or the `...-portable.zip` (extract anywhere, run `notch.exe`) from the
repository's Releases page (a `v*` tag builds one) or from the latest **Windows** run under Actions. The build is not code-signed, so SmartScreen says
"Windows protected your PC": **More info → Run anyway**.

- **Works the same:** the shelf at the top edge of every monitor, categories, notes, search, preview, Quick Look,
  sorting, rename, undo, zip, share via QR (WiFi and your own server), settings, tray icon, "start with Windows",
  battery / charger alerts, new screenshots (Win + PrtSc saves to `Pictures\Screenshots`) and downloads.
- **Not on Windows yet:** the music-player card and real-audio equalizer, picking a colour from the screen, Bluetooth
  alerts, KDE Connect, and locked categories (they need gocryptfs). Their buttons are hidden or greyed out.
- **PDF and OCR tools** need [poppler](https://github.com/oschwartz10612/poppler-windows),
  [Ghostscript](https://ghostscript.com/) and [Tesseract](https://github.com/UB-Mannheim/tesseract) on `PATH`; the image
  tools need nothing.
- **Sharing over WiFi:** Windows asks once to let Notch through its firewall; allow it on private networks.
- Settings live in `%LOCALAPPDATA%\notch`, your files in `%LOCALAPPDATA%\notch\files`.

Build from source: Qt 6.8 (MSVC 2022), CMake, Ninja and OpenSSL (`vcpkg install openssl:x64-windows`); see
`.github/workflows/windows.yml` for the exact steps. Run `cmake` with `-DNOTCH_NO_DBUS=ON` on Linux to try the
Windows code paths (no D-Bus integrations) without a Windows machine.

## Storage

| What | Where |
|---|---|
| Files | `~/.local/share/notch/files/<uuid>/<original name>` |
| Locked files | `~/.local/share/notch/vault/` (gocryptfs, encrypted); while unlocked, plain view at `$XDG_RUNTIME_DIR/notch-vault-*` (RAM only) |
| Metadata | `~/.local/share/notch/notch.db` (SQLite, schema version in `PRAGMA user_version`) |
| Settings | `~/.config/notch/notch.conf` (`retentionDays`, `storageDir`) |
| Start at login | `~/.config/autostart/notch.desktop`, written/removed by the settings toggle |

The files row is the default; a storage folder chosen in settings replaces it. Cleanup runs at
startup, hourly, and when retention changes, and removes items whose `added_at` (UTC) is at least the
retention period old (default 7 days).
Deletion is crash-safe (mark → delete file → delete row) and restricted to the files directory.
If the database is unreadable, Notch shows an error and deletes nothing.

## Package, install, uninstall

```bash
cd build && cpack          # produces notch_0.1.4_amd64.deb
sudo apt install ./notch_0.1.4_amd64.deb
```

Autostart: turn on **Start at login** in settings.

Uninstall: `sudo apt remove notch`. Your shelf data is kept; delete it explicitly with
`rm -r ~/.local/share/notch` if you no longer want it.

## Layout

```
src/models         FileItem, FileItemModel (QAbstractListModel for QML)
src/repositories   FileRepository: SQLite, migrations (database/migrations)
src/services       FileService (copy/delete), CleanupService (retention, crash recovery)
src/app            NotchController: QML bridge, async copies
src/platform       DesktopIntegration: layer-shell / X11 window placement, input region
src/qml            Main.qml (hot edge, island, shelf, menus), NotePage.qml, SettingsPage.qml,
                   FileItemDelegate.qml (tile), MediaCard.qml, shared controls (PillButton,
                   Toggle, DarkMenu, Chip, SettingToggle, WheelScroller, …)
tests              tst_core: storage, copy, cleanup, guards, recovery
```
