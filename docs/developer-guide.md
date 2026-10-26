# Olyzano Notch: developer guide

How the code is organised and how to build, test, change and release it. Users: see the [user guide](user-guide.md).
The [README](../README.md#build-from-source) has the per-distribution package lists.

## 1. Stack

C++17, **Qt 6.5+** (Core, Gui, Widgets, Sql/SQLite, Concurrent, Network, Quick/QML, Test; DBus on Linux), CMake 3.21+ with Ninja.
Wayland placement uses `layer-shell-qt`; AES-GCM uses OpenSSL's libcrypto. The relay in `server/` is Python.

## 2. Build, run, test, package

**Linux**

```bash
sudo apt install cmake ninja-build g++ libssl-dev qt6-base-dev qt6-base-private-dev qt6-declarative-dev
# optional, found automatically: liblayershellqtinterface-dev libkf6windowsystem-dev libkf6idletime-dev

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure   # what CI runs
./build/notch
cd build && cpack                                                       # notch_<version>_amd64.deb
```

Run against a throw-away shelf so your real one is untouched:
`XDG_DATA_HOME=/tmp/notch-test XDG_CONFIG_HOME=/tmp/notch-test-config ./build/notch`.
Add `-DNOTCH_NO_DBUS=ON` to build the variant Windows gets (no D-Bus integrations) and exercise those code paths on Linux.

**Windows**: Qt 6.8 (MSVC 2022), CMake, Ninja, `vcpkg install openssl:x64-windows`. The exact steps, including `windeployqt` and the
Inno Setup installer (`packaging/windows/notch.iss`), are in [`.github/workflows/windows.yml`](../.github/workflows/windows.yml).

The configure output states which optional parts were found (layer-shell, KF6WindowSystem, KF6IdleTime).

**Discord muted / deafened (Windows).** Notch reads it from the running Discord's local RPC pipe, which needs a Discord application
(Client ID and Secret). Create one at <https://discord.com/developers/applications>, add `http://localhost` under *OAuth2 → Redirects*,
then copy [`.env.example`](../.env.example) to `.env` in the repository root, fill in the two values and run CMake again
(or pass `-DNOTCH_DISCORD_CLIENT_ID=… -DNOTCH_DISCORD_CLIENT_SECRET=…`). `.env` is ignored by git. For releases, put them in the repository secrets
`DISCORD_CLIENT_ID` and `DISCORD_CLIENT_SECRET`; `windows.yml` passes them on. Never commit them. Without them the build works and
Settings says the feature is unavailable. Limits: until Discord approves the application, only its owner and up to 50 listed testers can use
RPC, and a secret inside a program that is handed out can be extracted, so treat it as low-trust (reset it if it leaks).
`notch --dump-discord <file>` records what Discord's audio sessions do (the in-call / talking ring uses those, not RPC).

## 3. Architecture

The code is layered; dependencies point downwards. Everything in the static library `notch_core` compiles without QML or a
window, which is why the tests can link it.

| Layer | Folder | What lives there |
|---|---|---|
| Core (`notch_core`) | `src/repositories`, `src/services`, `src/models/FileItem.h` | `FileRepository` (SQLite + migrations), `FileService`, `CleanupService`, `FolderWatcher`, `DocTools`, `Vault`, `ShareServer`, `ShareCrypto`, `NearbyService`, `NearbyCrypto`, `ZipWriter`, `QrImage`, `Invite`, plus `Lyrics` |
| Controllers (QML bridges) | `src/app` | `NotchController` (the main one), `ShareController` / `ShareSession`, `NearbyController`; `src/models/FileItemModel` is the `QAbstractListModel` |
| Platform features | `src/platform` | `AppDock`, `Activities` (timer, downloads, privacy), `Wellbeing` and `Doomscroll`, `QuickSettings`, `PanelBar`, `AppMenu`, `TrayHost`, `Launcher`, `Sounds`, `Karaoke`, `GamingMode`, `Theme`, `DesktopIntegration` (layer-shell / plain-window placement, input region) |
| UI | `src/qml` | `Main.qml` (hot edge, shelf, menus, live pill), `Dock.qml`, `Bar.qml`, `QuickSettings.qml`, `TimerPage.qml`, `SettingsPage.qml`, shared controls (`Toggle`, `IconButton`, `PillButton`, …), `icons/`, `sounds/` |
| Entry | `src/main.cpp` | Builds the objects, exposes them to QML, handles the debug flags |

**How C++ reaches QML.** `main.cpp` creates one instance of each object and registers it as a context property:
`notch` → `NotchController`, `share`, `nearby`, `dock`, `activities`, `wellbeing`, `quick`, `topbar` (`PanelBar`), `appMenu`, `theme`,
`sounds`, `gaming`, `lyrics`, `karaoke`, and so on. QML calls their `Q_INVOKABLE` methods, reads their `Q_PROPERTY`s and listens to their
signals. Small `*Model.h` headers (`WellbeingModel.h`, `PanelModel.h`, `DoomscrollModel.h`, …) hold the list/row models those objects expose.

**Most-connected classes** (from a code graph built with graphify): `NotchController`, `AppDock`, `QuickSettings`, `Activities`,
`Doomscroll`, `ShareController`, `Wellbeing`, `NearbyService`. Start reading there.

## 4. Adding a feature

1. **Logic first, no UI.** If it needs no windowing, put it in `src/services` (or `src/platform` for a desktop-integration piece) and
   add the `.h/.cpp` to the right list in [CMakeLists.txt](../CMakeLists.txt): `notch_core` if tests should link it, `notch` otherwise.
2. **Expose it.** Add `Q_PROPERTY` / `Q_INVOKABLE` / signals on an existing controller, or create an object and register it in
   `main.cpp` with `setContextProperty`. Persisted settings follow the existing `*.conf` pattern (`notch.conf`, `dock.conf`, `wellbeing.conf`, …).
3. **UI.** Add the `.qml` file to `QML_FILES` in `qt_add_qml_module(notch …)` and any icon or sound to `RESOURCES`; reuse the shared controls
   in `src/qml` and the `theme` object for colours so every theme works.
4. **Test it** (section 6).
5. **Docs.** Put the user-facing text in the README and a bullet in `.github/RELEASE_NOTES.md`.

### Platform-specific code

- Linux D-Bus integrations are compiled when `NOTCH_DBUS` is on (Unix, not `-DNOTCH_NO_DBUS=ON`) and guarded in code by
  `NOTCH_HAVE_DBUS`. Without it, header-only stubs or a `*_other.cpp` file provide the same API (`SystemEvents_other.cpp`).
- Windows implementations are `*_win.cpp` (`AudioLevels_win.cpp`, `ColorPicker_win.cpp`, `MediaPlayers_win.cpp`,
  `DesktopNotifications_win.cpp`, …) added under `if(WIN32)`; files using C++/WinRT are built with `/std:c++20`.
- Rule of thumb: keep the **same class API on every platform** so QML does not branch on the OS, and hide or grey out controls a platform cannot offer.

### Database migrations

Never edit an applied migration. Add `database/migrations/00N_name.sql`, then register it in **two** places: the
`qt_add_resources(notch_core migrations …)` list in CMakeLists.txt and the `Migrations` list at the top of
`src/repositories/FileRepository.cpp`. The schema version is SQLite's `PRAGMA user_version`; a database newer than the code is refused
and nothing is deleted.

## 5. Debug flags

Run `notch` with:

| Flag | Does |
|---|---|
| `--doom-debug <site> <seconds>` | Pretend to scroll a site and print what the doomscroll warning would say |
| `--dump-menu <pid>` | Print the D-Bus menu an app shares |
| `--dump-tray` | List the tray items it sees |
| `--dump-quick` | Print what Quick Settings sees on this computer |
| `--super-key [status\|on\|off]` | Show or change who gets the Windows key (default `status`) |
| `--shot <file.png>` (add `--launcher` for the launcher) | Picture of the status bar with Quick Settings open |
| `--shot-menu <file.png> <pid> [index]` | Picture of one title's dropdown in an app's menu |
| `--shot-settings <file.png> [section]` | Picture of the settings page |
| `--shot-page <file.png> <timer\|timer-run\|color\|shelf\|share>` | Picture of a page |

The `--shot*` flags render offscreen, so they work where nobody is looking at a screen. `src/main.cpp` is the source of truth for these.

## 6. Tests

One QtTest binary, [tests/tst_core.cpp](../tests/tst_core.cpp), linked to `notch_core`. Each test is a `private slot` using a temporary
folder and database, so nothing touches your real shelf. Header-only models and helpers (`WellbeingModel.h`, `DoomscrollModel.h`,
`BatteryThreshold.h`, …) are tested the same way because they have no UI dependency. To add one, write a `void myCase()` slot, keep it
deterministic (no network, no screen), and run `ctest --test-dir build --output-on-failure`. Covered today: storage, copy, cleanup, tools,
zip, sharing, crypto, dock, stretch reminder, doomscroll. The relay has its own Python tests in `server/tests/`.

## 7. CI and release

| Workflow | Runs on | Produces |
|---|---|---|
| [linux.yml](../.github/workflows/linux.yml) | Every push to `main`, tags `v*`, or by hand | Debian 13 container (Qt 6.8): build, `ctest`, `cpack` → `notch_*.deb` |
| [windows.yml](../.github/workflows/windows.yml) | Tags `v*` or by hand | MSVC build, tests, `windeployqt`, portable zip and Inno Setup installer |

**Cut a release**

1. Bump `project(notch VERSION x.y.z …)` in CMakeLists.txt (it becomes `NOTCH_VERSION` and the package version).
2. Rewrite `.github/RELEASE_NOTES.md` for the new version; both workflows publish it as the release body.
3. Update the version in the README download commands.
4. Tag and push: `git tag vX.Y.Z && git push origin vX.Y.Z`. Both workflows attach their downloads to the release.

## 8. Related

- The share relay (Python, Docker): [server/README.md](../server/README.md).
- Wayland constraints: [wayland-limitations.md](wayland-limitations.md).
- Sound effects are generated by `tools/make_sounds.py` into `src/qml/sounds/`.
