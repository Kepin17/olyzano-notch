import QtQuick
import Qt.labs.platform as Labs

// One tray icon for the whole app; actions go to the primary monitor's Notch.
// Right-click shows this menu (Qt.labs.platform → QSystemTrayIcon on Windows).
Labs.SystemTrayIcon {
    visible: true
    icon.name: "folder-download" // a theme icon where there is one (KDE) ...
    icon.source: "icons/notch.png" // ... else ours (Windows has no icon theme)
    tooltip: gaming.active ? "Olyzano Notch · gaming mode" : "Olyzano Notch · " + notch.count + (notch.count === 1 ? " file" : " files")
    onActivated: (reason) => {
        if (reason !== Labs.SystemTrayIcon.Trigger && reason !== Labs.SystemTrayIcon.DoubleClick)
            return // Context is the menu itself; MiddleClick keeps the default
        if (!gaming.active) // over a full-screen app nothing opens
            desktop.toggleFromTray("")
    }
    menu: Labs.Menu {
        Labs.MenuItem { text: gaming.enabled ? "Gaming mode: on (turn off)" : "Gaming mode: off (turn on)"; onTriggered: gaming.enabled = !gaming.enabled }
        Labs.MenuItem { text: "Show / hide Olyzano Notch"; enabled: !gaming.active; onTriggered: desktop.toggleFromTray("") }
        Labs.MenuItem { text: nearby.receiving ? "Stop receiving from nearby" : "Receive from nearby (10 min)"; onTriggered: nearby.receiving ? nearby.stopReceiving() : nearby.startReceiving() }
        Labs.MenuSeparator {}
        Labs.MenuItem { text: "Settings…"; enabled: !gaming.active; onTriggered: desktop.toggleFromTray("settings") }
        Labs.MenuItem { text: "Settings: General"; enabled: !gaming.active; onTriggered: desktop.toggleFromTray("settings:general") }
        Labs.MenuItem { text: "Settings: Appearance"; enabled: !gaming.active; onTriggered: desktop.toggleFromTray("settings:appearance") }
        Labs.MenuItem { text: "Settings: Panel"; enabled: !gaming.active; onTriggered: desktop.toggleFromTray("settings:panel") }
        Labs.MenuSeparator {}
        Labs.MenuItem { text: "Quit Olyzano Notch"; onTriggered: Qt.quit() }
    }
}
