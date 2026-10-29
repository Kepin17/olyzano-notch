import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// SSH tab inside DevPage: saved hosts on the left; connection card + run/output on the right.
// Layout: label-above fields in a card, actions in their own row, output in a fixed well.
ColumnLayout {
    id: tab
    required property var shell
    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 0

    readonly property bool modalOpen: deleteDialog.visible
    property string selectedId: ""
    property var draft: ({})
    property bool editing: false
    property string menuId: ""

    function blankDraft() {
        return { id: "", name: "", host: "", user: "", port: 22, auth: "key", keyPath: "", defaultCommand: "", savePassword: false }
    }
    function select(id) {
        selectedId = id
        draft = id ? sshManager.connectionById(id) : blankDraft()
        editing = !!id
        if (id) {
            const c = sshManager.connectionById(id)
            cmdField.text = (c && c.defaultCommand) ? c.defaultCommand : ""
        } else {
            cmdField.text = ""
        }
    }
    function say(msg) {
        shell.infoText = msg
        shell.restartInfo()
    }
    function save() {
        const fields = {
            name: draft.name, host: draft.host, user: draft.user, port: Number(draft.port) || 22,
            auth: draft.auth, keyPath: draft.keyPath, defaultCommand: draft.defaultCommand,
            savePassword: !!draft.savePassword
        }
        if (editing) {
            if (sshManager.updateConnection(selectedId, fields))
                say("SSH — saved")
        } else {
            const id = sshManager.addConnection(fields)
            if (id) { select(id); say("SSH — added") }
        }
    }

    Component.onCompleted: {
        sshManager.statusMessage.connect(msg => tab.say(msg))
        sshManager.testFinished.connect(r => {
            if (r.id !== tab.selectedId)
                return
            resultLabel.text = r.ok ? "Reachable · " + r.latencyMs + " ms" : (r.error || "failed")
            resultLabel.color = r.ok ? theme.success : theme.danger
        })
        select("")
    }

    // shared field chrome
 component Field: ColumnLayout {
        id: fl
        required property string label
        property string placeholder: ""
        property string value: ""
        property bool mono: false
        property bool show: true
        signal edited(string text)
        visible: show
        Layout.fillWidth: true
        spacing: 4
        Text {
            text: fl.label
            color: theme.textDim
            font.pixelSize: 11
            font.weight: Font.Medium
        }
        TextField {
            id: tf
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            text: fl.value
            placeholderText: fl.placeholder
            placeholderTextColor: theme.textFaint
            color: theme.text
            font.family: fl.mono ? "monospace" : ""
            font.pixelSize: 12
            leftPadding: 10
            rightPadding: 10
            background: Rectangle {
                radius: 8
                color: theme.well
                border.width: 1
                border.color: tf.activeFocus ? Qt.alpha(theme.accent, 0.45) : theme.hairline
            }
            onTextEdited: fl.edited(text)
        }
    }

    // Port spinner themed for dark/light
    component PortSpin: SpinBox {
        id: spin
        Layout.fillWidth: true
        Layout.preferredHeight: 32
        from: 1
        to: 65535
        font.pixelSize: 12
        font.family: "monospace"
        contentItem: TextInput {
            id: textIn
            text: spin.textFromValue(spin.value, spin.locale)
            font: spin.font
            color: theme.text
            selectionColor: Qt.alpha(theme.accent, 0.35)
            selectedTextColor: theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            readOnly: !spin.editable
            validator: spin.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }
        background: Rectangle {
            radius: 8
            color: theme.well
            border.width: 1
            border.color: textIn.activeFocus ? Qt.alpha(theme.accent, 0.45) : theme.hairline
        }
        up.indicator: Rectangle {
            x: spin.mirrored ? 0 : parent.width - width
            width: 22; height: parent.height
            color: up.hovered ? Qt.alpha(theme.accent, 0.18) : "transparent"
            Text { anchors.centerIn: parent; text: "▴"; color: theme.textDim; font.pixelSize: 10 }
            HoverHandler { id: up }
        }
        down.indicator: Rectangle {
            x: spin.mirrored ? parent.width - width : 0
            width: 22; height: parent.height
            color: down.hovered ? Qt.alpha(theme.accent, 0.18) : "transparent"
            Text { anchors.centerIn: parent; text: "▾"; color: theme.textDim; font.pixelSize: 10 }
            HoverHandler { id: down }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 12

        // ---- sidebar ----
        ColumnLayout {
            Layout.preferredWidth: 200
            Layout.maximumWidth: 220
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                Text {
                    text: "Hosts"
                    color: theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignVCenter
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    iconName: "plus"
                    Accessible.name: "New SSH connection"
                    onClicked: {
                        selectedId = ""
                        draft = tab.blankDraft()
                        editing = false
                        cmdField.text = ""
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.well
                border.width: 1
                border.color: theme.hairline
                radius: theme.radiusMd

                ListView {
                    id: hostList
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: sshManager.connections
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool sel: modelData.id === tab.selectedId
                        width: hostList.width
                        height: 40
                        radius: 6
                        color: sel ? Qt.alpha(theme.accent, 0.16) : hover.hovered ? Qt.alpha(theme.text, 0.05) : "transparent"

                        HoverHandler { id: hover }
                        TapHandler {
                            onTapped: {
                                tab.select(modelData.id)
                                contextMenu.close()
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                menuId = modelData.id
                                tab.select(modelData.id)
                                contextMenu.popup()
                            }
                        }

                        Column {
                            anchors {
                                left: parent.left; right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10; rightMargin: 8
                            }
                            spacing: 2
                            Text {
                                width: parent.width
                                text: modelData.name
                                color: theme.text
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: modelData.label
                                color: theme.textFaint
                                font.family: "monospace"
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: sshManager.connections.length === 0
                    text: "No hosts yet.\nPress + to add one."
                    color: theme.textFaint
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // ---- detail ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // header
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                Text {
                    text: editing ? (draft.name || "Connection") : "New connection"
                    color: theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    id: resultLabel
                    text: ""
                    color: theme.textDim
                    font.pixelSize: 11
                }
                IconButton {
                    visible: editing
                    iconName: "trash"
                    Accessible.name: "Delete connection"
                    onClicked: deleteDialog.open()
                }
            }

            // connection form card
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: formCol.implicitHeight + 28
                radius: theme.radiusMd
                color: theme.well
                border.width: 1
                border.color: theme.hairline

                GridLayout {
                    id: formCol
                    anchors {
                        left: parent.left; right: parent.right; top: parent.top
                        margins: 14
                    }
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 12

                    Field {
                        label: "Name"
                        placeholder: "prod-web"
                        value: draft.name || ""
                        Layout.columnSpan: 2
                        onEdited: t => draft.name = t
                    }
                    Field {
                        label: "Host"
                        placeholder: "10.0.0.5 or example.com"
                        value: draft.host || ""
                        mono: true
                        onEdited: t => draft.host = t
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: "Port"; color: theme.textDim; font.pixelSize: 11; font.weight: Font.Medium }
                        PortSpin {
                            id: portField
                            value: draft.port || 22
                            onValueModified: draft.port = value
                        }
                    }
                    Field {
                        label: "User"
                        placeholder: "root"
                        value: draft.user || ""
                        mono: true
                        onEdited: t => draft.user = t
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: "Auth"; color: theme.textDim; font.pixelSize: 11; font.weight: Font.Medium }
                        PillButton {
                            id: authBtn
                            Layout.fillWidth: true
                            text: draft.auth === "password" ? "Password" : "Key / agent"
                            onClicked: authMenu.popup()
                        }
                    }
                    Field {
                        label: "Key path"
                        placeholder: "~/.ssh/id_ed25519  (empty = agent)"
                        value: draft.keyPath || ""
                        mono: true
                        show: draft.auth !== "password"
                        onEdited: t => draft.keyPath = t
                    }
                    Field {
                        label: "Default command"
                        placeholder: "uptime"
                        value: draft.defaultCommand || ""
                        mono: true
                        onEdited: t => draft.defaultCommand = t
                    }
                }
            }

            // actions
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                PillButton {
                    text: editing ? "Save" : "Add"
                    checked: true
                    onClicked: tab.save()
                }
                PillButton {
                    text: "Test"
                    iconName: "check"
                    enabled: editing && !sshManager.busy
                    onClicked: {
                        resultLabel.text = "Testing…"
                        resultLabel.color = theme.textDim
                        sshManager.testConnection(selectedId)
                    }
                }
                PillButton {
                    text: "Open terminal"
                    iconName: "external"
                    enabled: editing
                    onClicked: sshManager.openTerminal(selectedId)
                }
                Item { Layout.fillWidth: true }
            }

            // run + output card
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: editing
                radius: theme.radiusMd
                color: theme.well
                border.width: 1
                border.color: theme.hairline

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: "Run a command"
                            color: theme.textDim
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Item { Layout.fillWidth: true }
                        PillButton {
                            text: sshManager.commandRunning ? "Running…" : "Run"
                            checked: sshManager.commandRunning
                            enabled: !sshManager.commandRunning && cmdField.text.trim().length > 0
                            onClicked: sshManager.runCommand(selectedId, cmdField.text.trim())
                        }
                        IconButton {
                            visible: sshManager.commandRunning
                            iconName: "stop"
                            Accessible.name: "Cancel command"
                            onClicked: sshManager.cancelCommand()
                        }
                    }

                    TextField {
                        id: cmdField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        placeholderText: "uptime · df -h · docker ps"
                        placeholderTextColor: theme.textFaint
                        color: theme.text
                        font.family: "monospace"
                        font.pixelSize: 12
                        leftPadding: 10
                        rightPadding: 10
                        background: Rectangle {
                            radius: 8
                            color: theme.surface
                            border.width: 1
                            border.color: cmdField.activeFocus ? Qt.alpha(theme.accent, 0.45) : theme.hairline
                        }
                        onAccepted: if (text.trim().length && !sshManager.commandRunning) sshManager.runCommand(selectedId, text.trim())
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 90
                        radius: 8
                        color: theme.surface
                        border.width: 1
                        border.color: theme.hairline

                        Text {
                            id: emptyOut
                            anchors.centerIn: parent
                            visible: sshManager.lastOutput.length === 0
                            text: "Output appears here"
                            color: theme.textFaint
                            font.pixelSize: 11
                        }

                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 8
                            visible: sshManager.lastOutput.length > 0
                            TextArea {
                                id: sshOut
                                readOnly: true
                                text: sshManager.lastOutput
                                color: theme.textSoft
                                font.family: "monospace"
                                font.pixelSize: 11
                                wrapMode: Text.WrapAnywhere
                                background: null
                            }
                        }
                    }
                }
            }

            // empty state when no selection
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !editing
                Text {
                    anchors.centerIn: parent
                    text: "Select a host, or press + to add one"
                    color: theme.textFaint
                    font.pixelSize: 12
                }
            }
        }
    }

    DarkMenu {
        id: authMenu
        MenuItem {
            text: "Key / agent"
            onTriggered: { draft.auth = "key"; authBtn.text = "Key / agent" }
        }
        MenuItem {
            text: "Password"
            onTriggered: { draft.auth = "password"; authBtn.text = "Password" }
        }
    }

    // Host context menu — same high-contrast chrome as DarkMenu
    DarkMenu {
        id: contextMenu
        MenuItem { text: "Test"; onTriggered: sshManager.testConnection(menuId) }
        MenuItem { text: "Open terminal"; onTriggered: sshManager.openTerminal(menuId) }
        MenuItem { text: "Duplicate"; onTriggered: sshManager.duplicateConnection(menuId) }
        MenuItem { text: "Delete"; onTriggered: deleteDialog.open() }
    }

    Dialog {
        id: deleteDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Delete connection?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 340
        palette.window: theme.dark ? "#232326" : "#ffffff"
        palette.windowText: theme.dark ? "#f2f2f7" : "#111111"
        palette.text: theme.dark ? "#f2f2f7" : "#111111"
        palette.buttonText: theme.dark ? "#f2f2f7" : "#111111"
        palette.button: theme.dark ? "#2c2c2e" : "#f0f0f2"
        background: Rectangle {
            radius: theme.radiusMd
            color: theme.dark ? "#232326" : "#ffffff"
            border.width: 1
            border.color: Qt.alpha(theme.text, 0.2)
        }
        onAccepted: {
            const wasSelected = selectedId === tab.selectedId
            sshManager.removeConnection(selectedId)
            if (wasSelected)
                tab.select("")
            tab.say("SSH — deleted")
        }
        contentItem: Text {
            text: "The host is removed from the list. Saved password (if any) is dropped too."
            wrapMode: Text.Wrap
            color: theme.dark ? "#f2f2f7" : "#1d1d1f"
            font.pixelSize: 12
        }
    }
}
