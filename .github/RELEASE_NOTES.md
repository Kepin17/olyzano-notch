## Olyzano Notch 0.12.1

A round of polish for the notch, plus a task list.

- **The capsule is no longer hidden by KDE's top panel.** The notch now sits above Plasma's own panel and menu bar.
- **Spectrum styles.** Pick how the song's spectrum looks: Bars, Thin, Dots, Wave, Pulse or Off (*Settings → General*).
- **Capsule size.** A slider (80–130%) makes the whole capsule smaller or bigger.
- **Tasks with deadlines.** A new *Tasks* section: type "Buy milk tomorrow 17:00" (or "Call Ana @ in 45m"), tick tasks off, and Notch reminds you 15 minutes before and when the deadline comes. The next task shows in the idle capsule (it can be switched off).
- **Pointing at a running timer opens the Timer** right away, the way pointing at the song opens the player.
- **Smoother panel.** The panel closes sooner when the pointer leaves, and its content appears only once the black background has grown to fit, so nothing shows outside it.
- Windows build fix and SHA-256 checksums (`SHA256SUMS-windows.txt`, `SHA256SUMS-linux.txt`) with every release.
- Discord muted / deafened now works on Linux too (it needs a Discord app: see `.env.example`), and *Settings → About* thanks the contributors, fetched from GitHub.

### Downloads
- **Linux:** `notch_0.12.1_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.12.1_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.12.1-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools on Windows): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).

### Is the Windows warning safe to click through?
Windows SmartScreen says *"Windows protected your PC"* because the installer is not code-signed yet and Notch is new to Microsoft, not because anything was found in it. Click **More info → Run anyway**. To check the file yourself, compare its SHA-256 with `SHA256SUMS-windows.txt` (and `SHA256SUMS-linux.txt` for the `.deb`) on this page: in PowerShell, `Get-FileHash .\Notch-0.12.1-windows-x64-setup.exe`. The code is open and built by GitHub Actions from this repository.
