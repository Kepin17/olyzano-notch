cmake_minimum_required(VERSION 3.21)
project(notch VERSION 0.4.0 LANGUAGES CXX)

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
)
target_include_directories(notch_core PUBLIC src third_party)
target_link_libraries(notch_core PUBLIC Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network ${NOTCH_CRYPTO}) # Gui: QImage/QPdfWriter in DocTools

qt_add_executable(notch
    src/main.cpp
    src/app/NotchController.h src/app/NotchController.cpp
    src/app/ShareController.h src/app/ShareController.cpp
    src/app/ShareSession.h src/app/ShareSession.cpp
    src/models/FileItemModel.h src/models/FileItemModel.cpp
    src/platform/DesktopIntegration.h src/platform/DesktopIntegration.cpp
    src/platform/SystemEvents.h
    src/platform/MediaPlayers.h
    src/platform/AudioLevels.h src/platform/AudioLevels.cpp
    src/platform/KdeConnect.h
    src/platform/Theme.h src/platform/Theme.cpp
    src/platform/ColorPicker.h
)
if(NOTCH_DBUS)
    target_sources(notch PRIVATE src/platform/SystemEvents.cpp src/platform/MediaPlayers.cpp
                                 src/platform/KdeConnect.cpp src/platform/ColorPicker.cpp)
else()
    target_sources(notch PRIVATE src/platform/SystemEvents_other.cpp) # the rest are header-only stubs
endif()
if(WIN32)
    configure_file(packaging/windows/notch.rc.in ${CMAKE_CURRENT_BINARY_DIR}/notch.rc)
    target_sources(notch PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/notch.rc)
    set_target_properties(notch PROPERTIES WIN32_EXECUTABLE TRUE) # a window, not a console
endif()
qt_add_qml_module(notch
    URI Notch
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml src/qml/FileItemDelegate.qml src/qml/Tray.qml src/qml/MediaCard.qml src/qml/Equalizer.qml
        # shared controls
        src/qml/PillButton.qml src/qml/IconButton.qml src/qml/Toggle.qml src/qml/PulseDot.qml
        src/qml/SectionLabel.qml src/qml/Caption.qml src/qml/DarkMenu.qml src/qml/DarkSeparator.qml
        src/qml/SettingToggle.qml src/qml/Chip.qml src/qml/WheelScroller.qml
        src/qml/NotePage.qml src/qml/SettingsPage.qml src/qml/LineIcon.qml src/qml/SharePopup.qml
    RESOURCES
        src/qml/icons/back.svg src/qml/icons/close.svg src/qml/icons/plus.svg src/qml/icons/lock.svg
        src/qml/icons/unlock.svg src/qml/icons/trash.svg src/qml/icons/palette.svg src/qml/icons/pen.svg
        src/qml/icons/gear.svg src/qml/icons/sort.svg src/qml/icons/camera.svg src/qml/icons/download.svg
        src/qml/icons/phone.svg src/qml/icons/search.svg src/qml/icons/share.svg src/qml/icons/wifi.svg src/qml/icons/server.svg src/qml/icons/notch.png
)
# Widgets: QApplication backs the tray icon (Qt.labs.platform falls back to QSystemTrayIcon).
target_link_libraries(notch PRIVATE notch_core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Concurrent Qt6::Network)
target_compile_definitions(notch PRIVATE NOTCH_VERSION="${PROJECT_VERSION}")
if(NOTCH_DBUS)
    target_link_libraries(notch PRIVATE Qt6::DBus)
    target_compile_definitions(notch PRIVATE NOTCH_HAVE_DBUS)
endif()
if(LayerShellQt_FOUND)
    target_link_libraries(notch PRIVATE LayerShellQt::Interface)
    target_compile_definitions(notch PRIVATE NOTCH_HAVE_LAYERSHELL)
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
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite, layer-shell-qt, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtqml-workerscript, qml6-module-qtquick-templates, qml6-module-qtquick-shapes, qml6-module-qtquick-effects, qml6-module-qtquick-dialogs, qml6-module-qt-labs-platform, qml6-module-qtcore, pulseaudio-utils, poppler-utils, ghostscript, gocryptfs, fuse3, qt6-svg-plugins, fonts-noto-cjk")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "tesseract-ocr")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
endif()
