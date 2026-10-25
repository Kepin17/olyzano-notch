## Olyzano Notch 0.11.0

A status bar with Quick Settings, the app's own menu, a tray and a calendar, and a fresher look for Files, Timer, Color and Share.

- **Status bar** (*Settings → Panel*): a glass strip along the top edge of every screen with the app in front on the left and the status icons and the clock on the right. It stays (*Always show*, optionally keeping windows out from under it) or hides until the pointer touches the top edge (*Auto-hide*), with blur and a transparency you set. Gaming mode keeps it out of the way.
- **Quick Settings.** Click the status icons (or point at them, if you prefer) for **Wi-Fi** and **Bluetooth** (the networks and devices *in range*, the one in use on top; join a new network with its password, pair a device found nearby), **volume** and **output**, the **microphone** and **input**, **brightness**, the battery and **power mode**, and who is using the microphone or a camera. Its lists fold when the card closes.
- **The app's menu** beside its name: File, Edit, View, Tools… for apps that share their menu (Chrome and other Chromium browsers, Qt / KDE apps), with shortcuts, marks and submenus.
- **Tray**: the apps running in the background (Steam, Discord, Claude…) as small icons on the bar: click to open, right-click for the menu.
- **Calendar**: click the clock on the bar for a month grid (week numbers and the first day of the week in *Settings → Panel*).
- **Redesigned panels.** **Timer**: preset cards, a stepper, a ring with round controls. **Color**: the last pick big with HEX / RGB / HSL, click to copy in the chosen format. **Files**: card tiles and tinted category chips. **Share**: a window that fits and scrolls on any screen, and while something is being shared the closed notch shows a **Sharing** pill (a small mark next to a song) that opens the running shares again.
- With lyrics off, the music capsule keeps the song on one line at its normal width.
- *Add* and *Note* shrink to icons when the bar would not fit them.

### Downloads
- **Linux:** `notch_0.11.0_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.11.0_amd64.deb`, then restart Olyzano Notch. The status bar needs KDE Plasma (Wayland) for blur, the app menu and the tray.
- **Windows:** `Notch-0.11.0-windows-x64-setup.exe`, or the portable `.zip`. The status bar, app menu and tray are Linux only for now.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).
