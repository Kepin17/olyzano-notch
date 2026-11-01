import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The Dev hub: API tester, SSH manager, database browser and coding agent.
// Thin shell only — each tab is a lazy Loader and costs nothing until opened.
ColumnLayout {
    id: dev
    required property var shell
    visible: shell.page === "dev"
    Layout.fillWidth: true
    Layout.maximumWidth: 100000
    spacing: 10

    function debugAgentConfig() {
        if (agentLoader.item) agentLoader.item.editProvider(codingAgent.activeProviderId)
    }
    property string tab: "api" // api | ssh | db | agent
    // Database and Agent need room for their multi-column controls.
    onTabChanged: shell.devWide = tab === "db" || tab === "agent"
    Component.onCompleted: shell.devWide = tab === "db" || tab === "agent"
    // forwarded so Main.qml can keep the notch open while a dialog is up
    readonly property bool modalOpen: (apiLoader.item && apiLoader.item.modalOpen) || (sshLoader.item && sshLoader.item.modalOpen)
                                      || (dbLoader.item && dbLoader.item.modalOpen) || (agentLoader.item && agentLoader.item.modalOpen)

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Segmented {
            Layout.preferredHeight: 30
            Layout.preferredWidth: implicitWidth
            model: [["api", "API"], ["ssh", "SSH"], ["db", "Database"], ["agent", "Agent"]]
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

    // ---- Agent ----
    Loader {
        id: agentLoader
        visible: dev.tab === "agent"
        Layout.fillWidth: true
        Layout.fillHeight: true
        // a long chat wants room: as tall as the notch window allows (the header takes about 160 px of it; below that the input
        // would be cut off), the layout would otherwise shrink it to its content. The window grows while this tab is open (Main.qml)
        Layout.preferredHeight: Math.max(300, shell.height - 160)
        active: dev.tab === "agent"
        sourceComponent: AgentTab { shell: dev.shell }
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
