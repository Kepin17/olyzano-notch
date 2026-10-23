import QtQuick
import Qt.labs.platform as Labs

// One tray icon for the whole app; actions go to the primary monitor's Notch.
Labs.SystemTrayIcon {
    visible: true
    icon.name: "folder-download"
    tooltip: "Notch · " + notch.count + (notch.count === 1 ? " file" : " files")
    onActivated: (reason) => {
        if (reason === Labs.SystemTrayIcon.Trigger)
            desktop.toggleFromTray("")
    }
    menu: Labs.Menu {
        Labs.MenuItem { text: "Show / hide Notch"; onTriggered: desktop.toggleFromTray("") }
        Labs.MenuItem { text: "Settings…"; onTriggered: desktop.toggleFromTray("settings") }
        Labs.MenuSeparator {}
        Labs.MenuItem { text: "Quit Notch"; onTriggered: Qt.quit() }
    }
}
