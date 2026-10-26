cmake_minimum_required(VERSION 3.21)
project(notch VERSION 0.12.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# D-Bus integrations (battery / Bluetooth, music player, KDE Connect, colour picker) are Linux's. Windows has
# none of that, and NOTCH_NO_DBUS builds the same stubs on Linux so those paths can be run and tested here.
option(NOTCH_NO_DBUS "Build without the D-Bus integrations (what Windows gets)" OFF)
if(UNIX AND NOT APPLE AND NOT NOTCH_NO_DBUS)
    set(NOTCH_DBUS ON)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Sql Concurrent Network Quick Test)
if(NOTCH_DBUS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
endif()
find_package(LayerShellQt QUIET)
find_package(KF6WindowSystem QUIET) # blur behind the dock on KDE Plasma
find_package(KF6IdleTime QUIET)     # the stretch reminder's idle time on Wayland
qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

if(MSVC)
    add_compile_options(/W3 /utf-8) # sources hold UTF-8 text (通知, tests with accents)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# UI-free core: storage, file operations, cleanup. Shared by the app and tests.
qt_add_library(notch_core STATIC
    src/models/FileItem.h
    src/repositories/FileRepository.h src/repositories/FileRepository.cpp
    src/services/FileService.h src/services/FileService.cpp
    src/services/CleanupService.h src/services/CleanupService.cpp
    src/services/FolderWatcher.h src/services/FolderWatcher.cpp
    src/services/TextTools.h
    src/services/DocTools.h src/services/DocTools.cpp
    src/services/Vault.h src/services/Vault.cpp
    src/services/ShareCrypto.h src/services/ShareCrypto.cpp
    src/services/QrImage.h src/services/QrImage.cpp
    src/services/ShareServer.h src/services/ShareServer.cpp
    src/services/Invite.h src/services/Invite.cpp
    src/services/ZipWriter.h src/services/ZipWriter.cpp
    src/services/NearbyCrypto.h src/services/NearbyCrypto.cpp
    src/services/NearbyService.h src/services/NearbyService.cpp
    src/platform/Lyrics.h src/platform/Lyrics.cpp src/platform/LrcParser.h
    third_party/qrcodegen.cpp
)
set_source_files_properties(third_party/qrcodegen.cpp PROPERTIES COMPILE_OPTIONS "-w") # vendored (MIT)
# AES-GCM comes from OpenSSL's libcrypto; ShareCrypto declares the few calls it uses itself, so building needs
# no OpenSSL headers on Linux (Qt Network already loads libcrypto). Windows links the OpenSSL import library.
if(WIN32)
    find_package(OpenSSL REQUIRED)
    set(NOTCH_CRYPTO OpenSSL::Crypto)
else()
    find_library(NOTCH_CRYPTO NAMES crypto libcrypto.so.3 REQUIRED)
endif()
qt_add_resources(notch_core share PREFIX "/share" BASE server FILES server/page.html) # one page for the relay and local shares
qt_add_resources(notch_core migrations PREFIX "/" BASE database FILES
    database/migrations/001_initial.sql
    database/migrations/002_categories.sql
    database/migrations/003_keep.sql
    database/migrations/004_recently_removed.sql
    database/migrations/005_vault.sql
    database/migrations/006_category_empty.sql
)
target_include_directories(notch_core PUBLIC src third_party)
target_link_libraries(notch_core PUBLIC Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network Qt6::Concurrent ${NOTCH_CRYPTO}) # Gui: QImage/QPdfWriter in DocTools

qt_add_executable(notch
    src/main.cpp
    src/app/NotchController.h src/app/NotchController.cpp
    src/app/ShareController.h src/app/ShareController.cpp
    src/app/ShareSession.h src/app/ShareSession.cpp
    src/app/NearbyController.h src/app/NearbyController.cpp
    src/models/FileItemModel.h src/models/FileItemModel.cpp
    src/platform/DesktopIntegration.h src/platform/DesktopIntegration.cpp
    src/platform/SystemEvents.h
    src/platform/MediaPlayers.h
    src/platform/AudioLevels.h src/platform/AudioLevels.cpp
    src/platform/KdeConnect.h
    src/platform/Theme.h src/platform/Theme.cpp
    src/platform/ColorPicker.h
    src/platform/AppDock.h src/platform/AppDock.cpp src/platform/DockModel.h
    src/platform/Activities.h src/platform/Activities.cpp
    src/platform/Wellbeing.h src/platform/Wellbeing.cpp src/platform/WellbeingModel.h
    src/platform/Sounds.h src/platform/Sounds.cpp src/platform/SoundsModel.h
    src/platform/Karaoke.h src/platform/Karaoke.cpp src/platform/KaraokeModel.h
    src/platform/GamingMode.h src/platform/GamingMode.cpp src/platform/GamingModel.h
    src/platform/PanelBar.h src/platform/PanelBar.cpp
    src/platform/QuickSettings.h src/platform/QuickSettings.cpp src/platform/PanelModel.h
    src/platform/AppMenu.h src/platform/AppMenu.cpp src/platform/DbusMenuModel.h src/platform/CalendarModel.h src/platform/Tasks.h src/platform/Tasks.cpp src/platform/TasksModel.h src/platform/Updates.h src/platform/Updates.cpp src/platform/UpdateModel.h src/platform/Doomscroll.h src/platform/Doomscroll.cpp src/platform/DoomscrollModel.h src/platform/Launcher.h src/platform/Launcher.cpp src/platform/LauncherModel.h src/platform/ColorFormat.h src/platform/TrayHost.h src/platform/TrayHost.cpp
    src/platform/DesktopNotifications.h src/platform/DesktopNotifications.cpp src/platform/NotificationFilter.h
)
if(NOTCH_DBUS)
    target_sources(notch PRIVATE src/platform/SystemEvents.cpp src/platform/MediaPlayers.cpp
                                 src/platform/KdeConnect.cpp src/platform/ColorPicker.cpp)
else()
    target_sources(notch PRIVATE src/platform/SystemEvents_other.cpp) # the rest are header-only stubs
endif()
if(WIN32)
    # KDE Connect for Windows runs a D-Bus of its own; QtDBus loads its libdbus at runtime (KdeConnect.cpp).
    find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
    target_sources(notch PRIVATE src/platform/KdeConnect.cpp)
    target_link_libraries(notch PRIVATE Qt6::DBus)
    target_compile_definitions(notch PRIVATE NOTCH_HAVE_KDECONNECT)
    target_sources(notch PRIVATE src/platform/WinPrivacy.h src/platform/WinPrivacy.cpp src/platform/ColorPicker_win.cpp
                                 src/platform/WinAudioSessions.h src/platform/WinAudioSessions.cpp
                                 src/platform/WinBluetooth.h src/platform/WinBluetooth.cpp
                                 src/platform/MediaPlayers_win.cpp src/platform/MediaNames.h src/platform/AudioLevels_win.cpp
                                 src/platform/DesktopNotifications_win.cpp)
    # user32/shell32: AppDock lists and raises windows, and asks the Recycle Bin. ole32/advapi32: microphone, registry.
    # windowsapp: the WinRT APIs (Bluetooth, media sessions) through C++/WinRT, which ships with the Windows SDK.
    target_link_libraries(notch PRIVATE user32 shell32 ole32 advapi32 crypt32 windowsapp winmm) # winmm: PlaySound; crypt32: the saved Discord sign-in is encrypted for this user (on Linux it is a file only you can read)
    # C++/WinRT's coroutine support needs C++20 (under C++17 it pulls in the removed <experimental/coroutine>).
    set_source_files_properties(src/platform/WinBluetooth.cpp src/platform/MediaPlayers_win.cpp
                                src/platform/DesktopNotifications_win.cpp
                                PROPERTIES COMPILE_OPTIONS "/std:c++20")
    configure_file(packaging/windows/notch.rc.in ${CMAKE_CURRENT_BINARY_DIR}/notch.rc)
    target_sources(notch PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/notch.rc)
    set_target_properties(notch PROPERTIES WIN32_EXECUTABLE TRUE) # a window, not a console
endif()
target_sources(notch PRIVATE src/platform/DiscordRpc.h src/platform/DiscordRpc.cpp) # Windows and Linux; other systems build it but never start it
# The Discord app behind "muted / deafened" in the island. Pass them on the command line or from CI secrets
# (-DNOTCH_DISCORD_CLIENT_ID=... -DNOTCH_DISCORD_CLIENT_SECRET=...); never write them in a file that is committed.
# Without them the feature says so in Settings and the island still shows in-call / talking.
set(NOTCH_DISCORD_CLIENT_ID "" CACHE STRING "Discord application client ID")
set(NOTCH_DISCORD_CLIENT_SECRET "" CACHE STRING "Discord application client secret")
# Or from a `.env` next to this file (copy .env.example); a value given with -D wins.
if(EXISTS "${CMAKE_SOURCE_DIR}/.env")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/.env")
    file(STRINGS "${CMAKE_SOURCE_DIR}/.env" _notch_env REGEX "^NOTCH_DISCORD_CLIENT_(ID|SECRET)=")
    foreach(_line IN LISTS _notch_env)
        string(REGEX MATCH "^([A-Z_]+)=(.*)$" _unused "${_line}")
        set(_name "${CMAKE_MATCH_1}")
        set(_value "${CMAKE_MATCH_2}")
        string(STRIP "${_value}" _value)
        string(REGEX REPLACE "^\"(.*)\"$" "\\1" _value "${_value}") # allow quotes
        if(NOT ${_name})
            set(${_name} "${_value}")
        endif()
    endforeach()
endif()
# The values go into a generated header, not onto the compiler's command line: MSVC/Ninja strip the quotes of a
# -DNAME="value" define, and a secret that starts with a digit then fails to compile.
set(_discord_id "")
set(_discord_secret "")
if(NOTCH_DISCORD_CLIENT_ID AND NOTCH_DISCORD_CLIENT_SECRET)
    string(REPLACE "\\" "\\\\" _discord_id "${NOTCH_DISCORD_CLIENT_ID}")
    string(REPLACE "\"" "\\\"" _discord_id "${_discord_id}")
    string(REPLACE "\\" "\\\\" _discord_secret "${NOTCH_DISCORD_CLIENT_SECRET}")
    string(REPLACE "\"" "\\\"" _discord_secret "${_discord_secret}")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/generated/notch_discord_app.h"
     "#pragma once\n#define NOTCH_DISCORD_CLIENT_ID \"${_discord_id}\"\n#define NOTCH_DISCORD_CLIENT_SECRET \"${_discord_secret}\"\n")
target_include_directories(notch PRIVATE "${CMAKE_BINARY_DIR}/generated")
qt_add_qml_module(notch
    URI Notch
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml src/qml/FileItemDelegate.qml src/qml/Tray.qml src/qml/MediaCard.qml src/qml/Equalizer.qml
        # shared controls
        src/qml/PillButton.qml src/qml/IconButton.qml src/qml/Toggle.qml src/qml/PulseDot.qml
        src/qml/SectionLabel.qml src/qml/Caption.qml src/qml/DarkMenu.qml src/qml/DarkSeparator.qml
        src/qml/SettingToggle.qml src/qml/Chip.qml src/qml/WheelScroller.qml
        src/qml/NotePage.qml src/qml/SettingsPage.qml src/qml/LineIcon.qml src/qml/SharePopup.qml src/qml/IncomingPopup.qml src/qml/ActionBar.qml src/qml/Bar.qml src/qml/Segmented.qml src/qml/Launcher.qml src/qml/SignalIcon.qml src/qml/CircleAction.qml src/qml/ColorPage.qml src/qml/AppMenuPopup.qml src/qml/CalendarCard.qml src/qml/QuickSettings.qml src/qml/LyricsPanel.qml
        src/qml/Dock.qml src/qml/TimerPage.qml src/qml/TasksPage.qml src/qml/EdgeStrip.qml src/qml/DiscordBadge.qml
    RESOURCES
        src/qml/icons/back.svg src/qml/icons/close.svg src/qml/icons/plus.svg src/qml/icons/lock.svg
        src/qml/images/traktir.jpeg src/qml/icons/info.svg src/qml/icons/grid.svg src/qml/icons/ethernet.svg src/qml/icons/eye.svg src/qml/icons/eye-off.svg src/qml/icons/external.svg src/qml/icons/undo.svg src/qml/icons/play.svg src/qml/icons/pause.svg src/qml/icons/stop.svg src/qml/icons/minus.svg src/qml/icons/check.svg src/qml/icons/copy.svg src/qml/icons/pin.svg src/qml/icons/image.svg src/qml/icons/eyedropper.svg src/qml/icons/unlock.svg src/qml/icons/trash.svg src/qml/icons/palette.svg src/qml/icons/pen.svg
        src/qml/icons/gear.svg src/qml/icons/sort.svg src/qml/icons/camera.svg src/qml/icons/download.svg
        src/qml/icons/phone.svg src/qml/icons/search.svg src/qml/icons/share.svg src/qml/icons/wifi.svg src/qml/icons/server.svg src/qml/icons/notch.png src/qml/icons/laptop.svg src/qml/icons/folder.svg src/qml/icons/music.svg src/qml/icons/gamepad.svg src/qml/icons/dock.svg src/qml/icons/heart.svg src/qml/icons/panel.svg src/qml/icons/volume.svg src/qml/icons/volume-off.svg src/qml/icons/sun.svg src/qml/icons/chevron.svg src/qml/icons/bluetooth.svg src/qml/icons/headphones.svg
        src/qml/icons/timer.svg src/qml/icons/mic.svg src/qml/icons/mic-off.svg src/qml/icons/notifications.svg
        # made by tools/make_sounds.py
        src/qml/sounds/break-over.wav src/qml/sounds/device.wav src/qml/sounds/drop.wav src/qml/sounds/notify.wav src/qml/sounds/pick.wav
        src/qml/sounds/plug-in.wav src/qml/sounds/plug-out.wav src/qml/sounds/receive.wav src/qml/sounds/ring.wav src/qml/sounds/send.wav
        src/qml/sounds/stretch.wav src/qml/sounds/timer.wav src/qml/sounds/trash.wav src/qml/sounds/warning.wav
)
# Widgets: QApplication backs the tray icon (Qt.labs.platform falls back to QSystemTrayIcon).
target_link_libraries(notch PRIVATE notch_core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Concurrent Qt6::Network)
target_compile_definitions(notch PRIVATE NOTCH_VERSION="${PROJECT_VERSION}")
if(NOTCH_DBUS)
    target_link_libraries(notch PRIVATE Qt6::DBus)
    target_compile_definitions(notch PRIVATE NOTCH_HAVE_DBUS)
    # libdbus itself: watching other apps' notifications needs a monitor connection, which QtDBus can't be.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(DBUS1 QUIET IMPORTED_TARGET dbus-1)
    endif()
    if(DBUS1_FOUND)
        target_link_libraries(notch PRIVATE PkgConfig::DBUS1)
        target_compile_definitions(notch PRIVATE NOTCH_HAVE_LIBDBUS)
    else()
        message(STATUS "libdbus-1 development files not found: notifications from other apps won't show in the notch")
    endif()
endif()
if(KF6IdleTime_FOUND)
    target_link_libraries(notch PRIVATE KF6::IdleTime)
    target_compile_definitions(notch PRIVATE NOTCH_HAVE_KIDLETIME)
    message(STATUS "KF6IdleTime found: the stretch reminder reads the idle time from it")
endif()
if(KF6WindowSystem_FOUND)
    target_link_libraries(notch PRIVATE KF6::WindowSystem)
    target_compile_definitions(notch PRIVATE NOTCH_HAVE_KWINDOWSYSTEM)
    message(STATUS "KF6WindowSystem found: the dock can blur what's behind it")
endif()
if(LayerShellQt_FOUND)
    target_link_libraries(notch PRIVATE LayerShellQt::Interface)
    target_compile_definitions(notch PRIVATE NOTCH_HAVE_LAYERSHELL)
    # Window::setScreen is newer than the layer-shell-qt of some distributions (Debian 13 has none);
    # without it the surface goes to the window's own screen, which is what Notch asks for anyway.
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES LayerShellQt::Interface)
    check_cxx_source_compiles("#include <LayerShellQt/window.h>
        int main() { LayerShellQt::Window *w = nullptr; w->setScreen(nullptr); }" NOTCH_LAYERSHELL_SET_SCREEN)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOTCH_LAYERSHELL_SET_SCREEN)
        target_compile_definitions(notch PRIVATE NOTCH_LAYERSHELL_SET_SCREEN)
    endif()
    message(STATUS "LayerShellQt found: Wayland layer-shell placement enabled")
else()
    message(WARNING "LayerShellQt not found: on Wayland the pill cannot be pinned to the top edge")
endif()

include(GNUInstallDirs)
install(TARGETS notch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES packaging/notch.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()

enable_testing()
qt_add_executable(tst_core tests/tst_core.cpp)
target_compile_definitions(tst_core PRIVATE NOTCH_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(tst_core PRIVATE notch_core Qt6::Test)
add_test(NAME tst_core COMMAND tst_core)

if(UNIX AND NOT APPLE)
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME notch)
set(CPACK_PACKAGE_CONTACT "kevien.oj@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Temporary file shelf for KDE Plasma")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite, layer-shell-qt, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtqml-workerscript, qml6-module-qtquick-templates, qml6-module-qtquick-shapes, qml6-module-qtquick-effects, qml6-module-qtquick-dialogs, qml6-module-qt-labs-platform, qml6-module-qtcore, pulseaudio-utils, poppler-utils, ghostscript, gocryptfs, fuse3, qt6-svg-plugins, qt6-image-formats-plugins, fonts-noto-cjk")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "tesseract-ocr")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
endif()
