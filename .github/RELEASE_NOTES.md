## Olyzano Notch 0.12.4

A cleaner Share page and dialog, a floating pill look, a volume bar, and the web app on a phone.

- **Share via QR is one dialog with two steps.** Pick the options, press Share, and the same dialog shows the QR code on the left and the link (with Copy) on the right. Only the panel behind it is dimmed, and nothing stacks on anything.
- **A cleaner Share page for the receiver.** Flat and neutral (light and dark), a table of files with a toolbar, and a PIN typed into one box per digit that opens by itself. The boxes wrap on a narrow phone, and the keyboard no longer covers the fields.
- **Floating pill (Settings → General).** The capsule can hang a little below the top edge as a full pill with a thin outline and a soft shadow, instead of growing out of the edge. Off by default.
- **Volume bar in the music player.** Drag, use the wheel, or click the speaker to mute. On Windows it controls the default speakers.
- **Music fixes.** The progress bar never passes its end and restarts at 0 on a new song; on Windows the player no longer flickers between tracks or when an app is busy.
- **The notch is no longer cut off** when the Sharing button appears: the panel is as wide as its buttons need.
- **About:** a *Contribute* card with how to contribute, the developer guide, issues and pull requests.
- **The web app on a phone:** touch-sized controls, a full-height sheet with the sections as a bar at the bottom, a *Scan* button (a photo of a page becomes a PDF), text and links from the Android share sheet land as a note, *Receive from a computer* scans the QR code of Share via QR, a maskable icon and home-screen shortcuts. The timer's total time is centred.
- **The main site** has the links as a bottom bar on a phone.
- **Relay:** the server's address shows a small page instead of raw JSON when nothing is shared.

### Downloads
- **Linux:** `notch_0.12.4_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.12.4_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.12.4-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).

### Is the Windows warning safe to click through?
Windows SmartScreen says *"Windows protected your PC"* because the installer is not code-signed yet and Notch is new to Microsoft, not because anything was found in it. Click **More info → Run anyway**. To check the file yourself, compare its SHA-256 with `SHA256SUMS-windows.txt` (and `SHA256SUMS-linux.txt` for the `.deb`) on this page: in PowerShell, `Get-FileHash .\Notch-0.12.4-windows-x64-setup.exe`. The code is open and built by GitHub Actions from this repository.
