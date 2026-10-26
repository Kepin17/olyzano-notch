# Windows developer notes, for the Linux developers

What was built and measured on Windows 11 for this round of work, what already carries over to Linux, and what is Windows-only and
worth porting. Everything here comes from this working tree; nothing is committed yet.

## 1. State of the Windows build

| | |
|---|---|
| Toolchain | Qt 6.8.3 `win64_msvc2022_64` (installed with `aqtinstall` into `C:\Qt`), MSVC 2022, CMake + Ninja, OpenSSL 3 from aqt |
| Build | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt>\msvc2022_64 -DOPENSSL_ROOT_DIR=<openssl>` then `cmake --build build` |
| Tests (`tst_core`) | 71 passed, 1 failed, 7 skipped. The failure is `convertAndOcr`: this Qt install has no WebP image plugin (not related to the changes) |
| Idle memory | about 190 MB working set, 230 MB private, flat over time (no leak). Most of it is the graphics driver and the 3-4 QML windows per screen |
| Run it | `windeployqt --release --qmldir src\qml notch.exe`, then start `notch.exe` |
| Pictures without a screen | `notch --shot-settings`, `--shot-page`, `--shot` (see the developer guide) |

**Careful when testing:** on Windows `QStandardPaths` ignores the `LOCALAPPDATA` environment variable, so a "throwaway profile" through
that variable does not work. Test runs use the real `%LOCALAPPDATA%\notch`. (Linux honours `XDG_DATA_HOME` / `XDG_CONFIG_HOME`.)

## 2. Changes that apply to every platform (needs a check on Linux)

These are in shared code, so Linux gets them too. They were verified on Windows only.

| Change | Where | What to look at on Linux |
|---|---|---|
| Equalizer no longer shakes: time-based smoothing (`smoothLevel`, attack 70 ms, release 260 ms), a steady 33 ms tick, ignores changes under 1 %, soft top instead of a hard clip | `AudioBands.h`, `AudioLevels.cpp`, `Equalizer.qml` | The `parec` path feeds the same `apply()`; check the bars look calm with PipeWire |
| Settings, the Share window, the Launcher and Quick Settings are built the first time they are opened, not at start-up (about 40 MB less on Windows) | `Main.qml` (`settingsLoader`, `shareLoader`, `settingsPage()` / `sharePopup()`), `Bar.qml` (`launch`, `quick2`) | First open of each: no flicker, focus in the launcher search field, the pointer area of the bar still includes the open card |
| Open / close animation: stiffer spring, earlier end (`epsilon 0.4`); the pointer area is set to where the island is going, not every frame | `Main.qml` (`updateMask`, `maskMoving`, `maskSettle`, the `SpringAnimation`s) | On Wayland / layer-shell the input region is set the same way: check clicks near the edge of an opening island |
| `--debug-hover` also logs frames longer than ~24 ms while the island moves | `Main.qml` (`onFrameSwapped`) | Use it to compare feel against the old build |
| Settings: a **Dynamic island** switch (capsule on / off) | `SettingsPage.qml` | Works on Linux; the default there is still *Hidden* |

## 3. Windows-only features and how to port them

### 3.1 Dynamic island on by default
`Activities::capsuleMode()` returns `"capsule"` when nothing is saved, on Windows only (`#ifdef Q_OS_WIN`). Linux stays `"hidden"`.
Whether Linux should default to the capsule is a product decision; the layer-shell capsule already works.

### 3.2 Discord in a call: green ring, filled green while talking (portable logic, Windows reader)
- Windows reads Discord's audio sessions with WASAPI (`WinAudioSessions.cpp`): a *capture* session that is active means "in a call",
  its peak level means "talking". The state machine (`CallTracker` in `DiscordCall.h`) has no platform code and is tested.
- **Port idea (Linux):** PipeWire / PulseAudio. Find the microphone stream of the Discord process (`pactl list source-outputs`, matching
  `application.process.binary` or `application.name`), treat a running, not corked stream as "in a call", and take its level from a
  monitor stream of that source output. Feed the same `CallTracker`. Flatpak / snap installs name the application differently.
- **Probe:** `notch --dump-discord <file>` (Windows) writes what the sessions do for 90 s. Mute and deafen do **not** show up in the
  audio (the sessions stay active); only Discord's short sound effects appear. That is why muted / deafened use RPC (below).

### 3.3 Discord muted / deafened over Discord RPC (best candidate to port)
Files: `DiscordIpc.h` (frames and mute logic, no platform code, unit tested), `DiscordRpc.{h,cpp}` (connection and sign-in), `Activities`
(`discordRpc`, `discordStatus`, `discordAvailable`, `discordState`), `DiscordBadge.qml`, a switch in `SettingsPage.qml` (shown on Windows).

How it works: open Discord's local pipe `discord-ipc-0..9` with `QLocalSocket`; send a handshake (`{"v":1,"client_id":...}`); on `READY`
sign in (`AUTHENTICATE` with a saved token, else `AUTHORIZE`, which opens an approval window in Discord, then a code-for-token request to
`discord.com/api/v10/oauth2/token`); then `GET_VOICE_SETTINGS` and `SUBSCRIBE VOICE_SETTINGS_UPDATE`. `deaf` means deafened, else `mute`
means muted. A refused handshake comes back as op 2 (close) with a message such as `Invalid Client ID`.

What to change for Linux:
1. **Socket path.** On Linux Discord listens on a Unix socket, in the first existing of `$XDG_RUNTIME_DIR`, `$TMPDIR`, `$TMP`, `$TEMP`,
   `/tmp`, plus `app/com.discordapp.Discord/` (Flatpak) and `snap.discord/` (Snap) under the runtime directory. Pass the full path to
   `QLocalSocket::connectToServer` and try `discord-ipc-0` to `-9` in each.
2. **Token storage.** Windows keeps the sign-in encrypted with DPAPI in `discord.dat`. On Linux use the Secret Service
   (libsecret / QtKeychain) or at least a file with mode `0600` in the data folder.
3. **Build options.** `NOTCH_DISCORD_CLIENT_ID` / `NOTCH_DISCORD_CLIENT_SECRET` and the `.env` reader are inside the `WIN32` block of
   `CMakeLists.txt`; move them out, and add the two secrets to `linux.yml` as `windows.yml` does.
4. **Remove the `#ifdef Q_OS_WIN` fences** in `Activities.h/.cpp`, and drop the `crypt32` / `windows.h` includes from `DiscordRpc.cpp`.

The protocol layer was checked against the real Discord on Windows: a handshake with the real application id returns `READY`, and an invalid
one returns `{"code":4000,"message":"Invalid Client ID"}`.

### 3.4 App names for the island and media
`MediaNames.h` maps Windows app ids (AUMIDs) to names, and now includes Discord. On Linux the name comes from the MPRIS identity.

## 4. The Discord application: limits to know

- One Discord application is shared by every copy of Notch. Until Discord approves it, **only its owner and up to 50 listed testers** can use
  RPC. Anyone else sees the status line say Discord did not allow it (the code falls back from the `rpc.voice.read` scope to `rpc`, then stops).
- The client secret is compiled into the program, so it can be extracted. Keep it out of the repository (`.env` and the repository secrets
  `DISCORD_CLIENT_ID` / `DISCORD_CLIENT_SECRET`), and reset it in the Discord portal if it leaks.
- The saved sign-in belongs to one Discord account. If another account is signed in to Discord later, the old token is refused and the
  approval window opens again (at most 3 times per connection, so it cannot loop).
- Only the author's own Discord account has been tried. No other account has been tested.

## 5. Things that cost time on Windows

- A `Loader`'s default property is its component: putting a `HoverHandler` directly inside a `Loader` fails with "Property value set multiple
  times". Use `data: HoverHandler { ... }`.
- `WIN32_LEAN_AND_MEAN` (set for the whole project) leaves out the OLE headers some Windows headers expect; include `objbase.h` and
  `oleauto.h` first.
- `Bar.qml` already had `onQuickOpenChanged` and `onLauncherOpenChanged` handlers; a second one is a compile error.
- QML is compiled ahead of time, so a QML mistake shows up as a build failure rather than at run time: build after each QML edit.
