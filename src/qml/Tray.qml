import QtQuick
import Qt.labs.platform as Labs

// One tray icon for the whole app; actions go to the primary monitor's Notch.
// Right-click shows this menu (Qt.labs.platform → QSystemTrayIcon on Windows).
Labs.SystemTrayIcon {
    visible: Qt.platform.os !== "windows" // on Windows main.cpp makes the icon and its menu itself (QSystemTrayIcon + QMenu): the menu of Qt.labs.platform did not open there
    // Theme name where there is one (KDE). On Windows there is no icon theme: a bare relative path
    // often fails to resolve for the native tray, so the source is an absolute qrc URL.
    icon.name: "folder-download"
    icon.source: Qt.resolvedUrl("icons/notch.png")
    tooltip: gaming.strict ? "Olyzano Notch · gaming mode" : gaming.active ? "Olyzano Notch · hidden in full screen" : "Olyzano Notch · " + notch.count + (notch.count === 1 ? " file" : " files")
    onActivated: (reason) => {
        if (reason !== Labs.SystemTrayIcon.Trigger && reason !== Labs.SystemTrayIcon.DoubleClick)
            return // Context is the menu itself; MiddleClick keeps the default
        if (!gaming.strict) { // Gaming mode keeps everything shut; the plain "hide in full screen" lets the tray bring Notch up
            if (gaming.active) gaming.reveal(true)
            desktop.toggleFromTray("")
        }
    }
    menu: Labs.Menu {
        Labs.MenuItem { text: gaming.enabled ? "Gaming mode: on (turn off)" : "Gaming mode: off (turn on)"; onTriggered: gaming.enabled = !gaming.enabled }
        Labs.MenuItem {
            text: gaming.active ? "Show Olyzano Notch over this app" : "Show / hide Olyzano Notch"
            enabled: !gaming.strict
            onTriggered: { if (gaming.active) gaming.reveal(true); desktop.toggleFromTray("") }
        }
        Labs.MenuItem { text: gaming.hideInFullScreen ? "Hide in full screen: on (turn off)" : "Hide in full screen: off (turn on)"; onTriggered: gaming.hideInFullScreen = !gaming.hideInFullScreen }
        Labs.MenuItem { text: nearby.receiving ? "Stop receiving from nearby" : "Receive from nearby (10 min)"; onTriggered: nearby.receiving ? nearby.stopReceiving() : nearby.startReceiving() }
        Labs.MenuSeparator {}
        Labs.MenuItem { text: "Settings…"; enabled: !gaming.strict; onTriggered: { if (gaming.active) gaming.reveal(true); desktop.toggleFromTray("settings") } }
        Labs.MenuItem { text: "Settings: General"; enabled: !gaming.strict; onTriggered: { if (gaming.active) gaming.reveal(true); desktop.toggleFromTray("settings:general") } }
        Labs.MenuItem { text: "Settings: Appearance"; enabled: !gaming.strict; onTriggered: { if (gaming.active) gaming.reveal(true); desktop.toggleFromTray("settings:appearance") } }
        Labs.MenuItem { text: "Settings: Panel"; enabled: !gaming.strict; onTriggered: { if (gaming.active) gaming.reveal(true); desktop.toggleFromTray("settings:panel") } }
        Labs.MenuSeparator {}
        Labs.MenuItem { text: "Quit Olyzano Notch"; onTriggered: Qt.quit() }
    }
}
