# Window system behaviour

Tested on Kubuntu 26.04, KDE Plasma 6.6.6, Wayland, Qt 6.10.2, layer-shell-qt 6.6.4.

## How Notch is placed

| Session | Mechanism | Result |
|---|---|---|
| Wayland + LayerShellQt (KWin) | `zwlr_layer_shell_v1`, layer `top`, anchored `top` only → compositor centers it horizontally, no margin, exclusive zone -1 (sits on the real screen edge, over a top panel) | **Verified**: island fused to the top edge, drawn above the top panel and normal windows |
| Wayland without layer-shell (build lacked `liblayershellqtinterface-dev`, or compositor without the protocol, e.g. GNOME) | Regular frameless window | Compositor decides position; top-center is **not** guaranteed. Not supported for MVP |
| X11 | Frameless `WindowStaysOnTopHint \| Tool` window moved to top-center of `availableGeometry()` | Untested |

The surface is a fixed 720×300 transparent window. `QWindow::setMask()` limits input to:
- **hidden** (default): a 300×3 px strip on the top edge, center. Everything else is click-through.
- **peek / open**: the island's current bounds (follows the spring animation).
- context menu open: the whole surface, so the menu can be clicked.

Reveal = pointer rests on the strip ≥160 ms, or a file drag enters it. Close = 380 ms after the
pointer leaves the island, unless pinned. While copies run with the panel closed, a small "Copying N
files" pill peeks out and disappears when done.

## Verified
- Layer-shell path is selected (`notch: Using wlr-layer-shell` in the log).
- Hidden state shows only a faint 44×3 px handle line; open state renders the island, tiles, image
  thumbnails (screenshots).
- Startup cleanup and persistence: items reload with the correct count after restart.
- Manual tests with a real pointer: top-edge reveal, clicks below/around the strip reaching the app
  underneath, drag-to-edge reveal + drop from Dolphin, drag-out to Dolphin, spring animation.

## Needs manual verification
- Multi-monitor: one layer surface per `QScreen` (`LayerShellQt::Window::setScreen`), created and
  destroyed on `screenAdded`/`screenRemoved`. An app can't read the global cursor position on
  Wayland, so each output gets its own hot strip. Verified: with a monitor plugged in, surfaces
  appear on both eDP-1 and HDMI-A-1 (screenshots). Hover on the second monitor and unplugging
  need a manual check.
- Fullscreen apps: KWin may place fullscreen windows above the `top` layer.

## Tray
`Qt.labs.platform` SystemTrayIcon, backed by `QSystemTrayIcon` (the app is a `QApplication` for this).
Verified: registers with Plasma's StatusNotifierWatcher (`Id=notch`). At startup Qt logs
`Failed to create grabbing popup ... QMenuClassWindow`: Qt's widget menu has no parent window on
Wayland; Plasma renders the tray menu itself over DBusMenu. Tray click and menu items verified by hand,
as were preview, search, settings and the storage folder picker, multi-select (Ctrl/Shift+click,
multi-file drag-out), categories (chips, drag tiles onto a chip, rename/delete), folder drops and
drags from Chrome (image download, also an image wrapped in a link; page link shortcut).

## System alerts
`SystemEvents` subscribes to `PropertiesChanged` on the system bus: UPower `DisplayDevice` (State:
on mains = charging/full/pending vs discharging) and every BlueZ `Device1` (`Connected`). Verified:
both subscriptions register and the initial battery state is read; the toast renders for charging
and Bluetooth (signal emitted by hand). Low-battery warnings: UPower `Percentage` while discharging
and BlueZ `Battery1.Percentage`, once per drop through 20/10/5 % (`crossedLowBattery`, unit-tested).
A Bluetooth device already connected when Notch starts is tracked from its first battery update
on. A real plug-in/unplug, headset connect and battery drop need a manual check.

## Music (MPRIS)
`MediaPlayers` watches `org.mpris.MediaPlayer2.*` on the session bus (`NameOwnerChanged`,
`PropertiesChanged`); `pickPlayer` (unit-tested) chooses the most recent Playing player, else the
most recent Paused one, and collapses a browser listed twice via plasma-browser-integration.
Position is read on demand and interpolated in QML. Verified with YouTube in Chrome: metadata,
cover, duration/position, and play/pause from Notch (status flipped, then restored). Next/previous
depend on the player (Chrome reports CanGoNext=false for a single video).

## Equalizer
`AudioLevels` runs `parec -d @DEFAULT_MONITOR@` (11025 Hz mono float) and computes 5 Goertzel
bands per 512-sample window (`bandLevels`, unit-tested). Reference-counted by the visible
`Equalizer` items that are live (on screen and media playing); stops 1 s after the last one goes.
Verified: parec runs while the open panel shows the equalizer, not while Notch is hidden, and exits
with Notch; levels track a playing YouTube video. Without parec the bars fall back to an animation.

## Folder watching
`FolderWatcher` (QFileSystemWatcher, unit-tested) reports a new file once it is not a partial download
name and its size is stable for 1.5 s; files already present are ignored. Screenshot folder: the
`screenshotDir` setting, else Spectacle's `ImageSave/imageSaveLocation`, else
`~/Pictures/Screenshots`. Verified with a file dropped into a watched scratch folder (added to the
Screenshots category, toast); a real Spectacle capture and a real browser download need a manual check.
Spectacle writes no file unless `[General] autoSaveImage=true` (default false): with it off, captures
only open Spectacle's window / go to the clipboard and never reach the folder. Notch detects this and
offers a button that sets the key with `kwriteconfig6` (Spectacle's config is read from `~/.config`,
not Notch's possibly redirected config dir).

## KDE Connect
`KdeConnect` uses `org.kde.kdeconnect` on the session bus: `daemon.devices(true, true)` for
reachable paired devices, `device.share.shareUrls` to send, and the `shareReceived` signal (any
device path) for incoming files. Verified: device list (Galaxy A56 5G), the receive path via
`addReceived` (added once to the KDE Connect category), and a failed send reporting its error.
A real send to the phone and a real incoming file need a manual check.

## Clipboard
Wayland only hands the clipboard to the focused surface, so Ctrl+V works after clicking Notch
(keyboard interactivity is `OnDemand`). Copy actions come from a click, which gives the needed focus.

## Known limitations
- The top 3 px of the 300 px center strip belong to Notch, not to the app below (e.g. a browser tab
  clicked at its very top pixel in that strip).
- Keyboard focus is `OnDemand`: click the island first, then Space/Enter toggles pin, Esc closes.
- No global shortcut (P1). Reduced-motion preference is not read.
- Running from the build tree prints a portal warning (`App info not found for 'notch'`) because
  `notch.desktop` is not installed; harmless, gone after installing the package.
