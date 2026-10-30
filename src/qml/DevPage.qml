import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The Dev hub: API tester, SSH manager and database browser behind one top Segmented.
// Thin shell only — each tab is a lazy Loader so SSH/DB cost nothing until opened.
ColumnLayout {
    id: dev
    required property var shell
    visible: shell.page === "dev"
    Layout.fillWidth: true
    Layout.maximumWidth: 100000
    spacing: 10

    property string tab: "api" // api | ssh | db
    // Database gets a wider panel (Databases | Tables | values side by side)
    onTabChanged: shell.devWide = tab === "db"
    Component.onCompleted: shell.devWide = tab === "db"
    // forwarded so Main.qml can keep the notch open while a dialog is up
    readonly property bool modalOpen: (apiLoader.item && apiLoader.item.modalOpen) || (sshLoader.item && sshLoader.item.modalOpen)
                                      || (dbLoader.item && dbLoader.item.modalOpen)

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Segmented {
            Layout.preferredHeight: 30
            Layout.preferredWidth: implicitWidth
            model: [["api", "API"], ["ssh", "SSH"], ["db", "Database"]]
            value: dev.tab
            onPicked: v => dev.tab = v
        }

        Item { Layout.fillWidth: true }

        Text {
            visible: dev.tab === "api"
            text: "HTTP client · collections · environments"
            color: theme.textFaint
            font.pixelSize: 11
        }
        Text {
            visible: dev.tab === "ssh"
            text: "Saved hosts · run commands · open a terminal"
            color: theme.textFaint
            font.pixelSize: 11
        }
        Text {
            visible: dev.tab === "db"
            text: "SQLite · MySQL · PostgreSQL"
            color: theme.textFaint
            font.pixelSize: 11
        }
    }

    // ---- API (the existing tester; content unchanged) ----
    Loader {
        id: apiLoader
        visible: dev.tab === "api"
        Layout.fillWidth: true
        Layout.fillHeight: true
        active: dev.tab === "api"
        sourceComponent: ApiPage {
            shell: dev.shell
            visible: true
        }
    }

    // ---- SSH ----
    Loader {
        id: sshLoader
        visible: dev.tab === "ssh"
        Layout.fillWidth: true
        Layout.fillHeight: true
        active: dev.tab === "ssh"
        sourceComponent: SshTab { shell: dev.shell }
    }

    // ---- Database ----
    Loader {
        id: dbLoader
        visible: dev.tab === "db"
        Layout.fillWidth: true
        Layout.fillHeight: true
        active: dev.tab === "db"
        sourceComponent: DbTab { shell: dev.shell }
        // if the component fails to create, show why instead of a blank panel
        onStatusChanged: {
            if (status === Loader.Error)
                console.error("DbTab failed to load:", sourceComponent ? sourceComponent.errorString() : "unknown")
        }
    }
}
