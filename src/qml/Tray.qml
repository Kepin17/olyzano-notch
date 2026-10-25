import QtQuick
import Qt.labs.platform as Labs

// One tray icon for the whole app; actions go to the primary monitor's Notch.
Labs.SystemTrayIcon {
    visible: true
    icon.name: "folder-download" // a theme icon where there is one (KDE) ...
    icon.source: "icons/notch.png" // ... else ours (Windows has no icon theme)
    tooltip: gaming.active ? "Olyzano Notch · gaming mode" : "Olyzano Notch · " + notch.count + (notch.count === 1 ? " file" : " files")
    onActivated: (reason) => {
        if (reason !== Labs.SystemTrayIcon.Trigger)
            return
        if (gaming.active) // the icon is how it wakes up
            gaming.wake()
        else
            desktop.toggleFromTray("")
    }
    menu: Labs.Menu {
        Labs.MenuItem { text: gaming.active ? "Leave gaming mode" : "Gaming mode"; onTriggered: gaming.active ? gaming.wake() : gaming.manual = true }
        Labs.MenuItem { text: "Show / hide Olyzano Notch"; onTriggered: { gaming.wake(); desktop.toggleFromTray("") } }
        Labs.MenuItem { text: nearby.receiving ? "Stop receiving from nearby" : "Receive from nearby (10 min)"; onTriggered: nearby.receiving ? nearby.stopReceiving() : nearby.startReceiving() }
        Labs.MenuItem { text: "Settings…"; onTriggered: { gaming.wake(); desktop.toggleFromTray("settings") } }
        Labs.MenuSeparator {}
        Labs.MenuItem { text: "Quit Olyzano Notch"; onTriggered: Qt.quit() }
    }
}
