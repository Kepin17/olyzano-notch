## Olyzano Notch 0.17.0

A cloud shelf you share through your own server, with the host in control of who has how much space; dropping big files no longer freezes Notch and shows how far it is; a category drags out as one .zip; and empty categories disappear on their own.

### New
- **Cloud shelf** (*Settings → Files → Cloud shelf*): a shelf shared with the members of one of your Share servers, kept on an S3-compatible store of your own (MinIO, Backblaze B2, Wasabi, Cloudflare R2, AWS S3...) behind the server. Files and their names are **encrypted on your computer** with a shelf key that only the people you invite have; the server and the store see sizes and who owns what, never names or contents. A **Cloud** chip on the Files page shows it: drop files on the chip or right-click a file → *Send to cloud shelf*; click a file to fetch it. The pill under the notch shows the progress, with a ✕ to stop. What anyone adds shows up on everyone's shelf. Set it up on the server with the new `S3_*` settings (see `server/README.md`; a MinIO is one `docker compose --profile minio up`).
- **Share a cloud file with a link and QR code** (right-click a file on the Cloud shelf → *Share via QR…*): Notch fetches it, then opens the same share window as for a local file (QR code, link, how long it lasts, PIN or password, one download). The link has a key of its own, never the shelf's, so sharing one file does not open the rest. The fetched copy stays on your own shelf like any file.
- **The host decides** (*Settings → Sharing → Members → ⚙*): give each member their own share and cloud space, **pause** a member with a reason they are shown ("Your access was paused by the host: ..."), let them back in, or **remove** them with a reason and choose whether their cloud files are deleted or become yours. A member whose key is used after that is told why instead of a bare "bad key". The same from the command line: `members disable NAME --reason "..."`, `members enable`, `members remove --reason --delete-files`, `members set --cloud-quota-mb`.
- **The key rides with the invitation:** an invitation the host makes carries the shelf's key after a `~` in the link's `#fragment` (it never goes to the server), so a new member joins the server and can read the shelf in one step.
- **Big drops with progress** (the pill under the notch): while a folder is listed and its files are copied, the pill says *Reading files*, then *Copying 12 of 340*, how many gigabytes are done, and how long is left, with a bar and a button to **stop it**. Every file shows its own percent while it is copied. Stopping removes what was half done.
- **A folder becomes a .zip when you drag it out:** drag a category chip (the folder you dropped) and Notch makes one `.zip` of it in the background, with the same progress; when it says *Zip ready: drag it out*, drag that file to where you want it, and extract it there. (Hold **Ctrl** while dragging a chip to carry the files themselves, as before.) *Compress to .zip* and *Zip category* show progress too, and can be stopped.
- **Zips bigger than 4 GB:** the zip writer now handles huge folders (Zip64) and more than 65535 files, and reads each file once instead of twice.

### Improved
- **A big shelf scrolls smoothly and a big drop no longer makes the panel lag:** tiles are reused while you scroll instead of rebuilt, and the heavy parts of a tile (the rounded thumbnail, the tooltip, the rename box, the hover buttons, the copy ring) are only built when they are used. While a file is copied, the shelf is no longer re-counted, re-indexed and its category chips rebuilt four times a second; a big drop updates the counts once, and a finished copy only tells the shelf what changed. The database no longer waits for the disk on every save.
- **Dropping a big folder or a 100 GB file no longer makes Notch "not responding":** the folder is listed and each file looked at on a worker, the files reach the shelf a few at a time, and the copy runs on its own two workers. Notch also checks the free space for the whole drop before it starts.
- **Empty categories go away at once:** when the last file leaves a category (moved, deleted for good, or its copy stopped) the category disappears, instead of after a day. A category that still has files in *Recently removed* stays until they are gone, and locked categories, the ones a "sort by type" rule files into, and a category you just made are kept.

### Fixed
- **The idle notch was cut off while charging** when the cat was on: the capsule now takes the width its contents need (the cat, the ⚡, the microphone and the camera).

### Downloads
- **Linux:** `notch_0.17.0_amd64.deb` for Debian 13, Ubuntu 25.04 or newer (Qt 6.5+). Install with `sudo apt install ./notch_0.17.0_amd64.deb`, then restart Olyzano Notch.
- **Windows:** `Notch-0.17.0-windows-x64-setup.exe`, or the portable `.zip`.
- Optional components (OCR, KDE Connect, PDF tools, ffmpeg, LibreOffice): see the [install page](https://kepin17.github.io/olyzano-notch/#optional).

### Is the Windows warning safe to click through?
Windows SmartScreen says *"Windows protected your PC"* because the installer is not code-signed yet and Notch is new to Microsoft, not because anything was found in it. Click **More info → Run anyway**. To check the file yourself, compare its SHA-256 with `SHA256SUMS-windows.txt` (and `SHA256SUMS-linux.txt` for the `.deb`) on this page: in PowerShell, `Get-FileHash .\Notch-0.17.0-windows-x64-setup.exe`. The code is open and built by GitHub Actions from this repository.
