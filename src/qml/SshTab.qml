import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// SSH tab: hosts on the left; a terminal-style pane on the right.
// New/edit connection is a modal form (open with + or Edit).
ColumnLayout {
    id: tab
    required property var shell
    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 0

    readonly property bool modalOpen: formPopup.visible || deleteDialog.visible
    property string selectedId: ""
    property var draft: ({})
    property bool editing: false
    property string menuId: ""

    function blankDraft() {
        return { id: "", name: "", host: "", user: "", port: 22, auth: "key", keyPath: "", defaultCommand: "", savePassword: false }
    }
    function say(msg) {
        shell.infoText = msg
        shell.restartInfo()
    }
    function openForm(id) {
        if (id) {
            selectedId = id
            const c = sshManager.connectionById(id)
            draft = {
                id: c.id, name: c.name, host: c.host, user: c.user, port: c.port || 22,
                auth: c.auth || "key", keyPath: c.keyPath || "", defaultCommand: c.defaultCommand || "",
                savePassword: !!c.savePassword, password: ""
            }
            editing = true
        } else {
            selectedId = ""
            draft = blankDraft()
            editing = false
        }
        formPopup.open()
    }
    function select(id) {
        selectedId = id
        draft = id ? sshManager.connectionById(id) : blankDraft()
        editing = !!id
        const c = id ? sshManager.connectionById(id) : null
        cmdInput.text = (c && c.defaultCommand) ? c.defaultCommand : ""
        termOutput.text = ""
        if (sshManager.shellOpen)
            sshManager.closeShell()
        if (id) {
            termOutput.text = "— connecting to " + (draft.user || "?") + "@" + (draft.host || "?") + ":" + (draft.port || 22) + " —\n"
            sshManager.openShell(id)
        } else {
            termOutput.text = "Welcome to the SSH terminal.\nSelect a host on the left, or press + to add one.\n"
        }
    }
    function saveForm() {
        const host = (hostField.text !== undefined && hostField.text !== "") ? hostField.text.trim() : String(draft.host || "").trim()
        const user = (userField.text !== undefined && userField.text !== "") ? userField.text.trim() : String(draft.user || "").trim()
        const name = (nameField.text !== undefined && nameField.text !== "") ? nameField.text.trim() : String(draft.name || "").trim()
        const port = parseInt(portField.text) || Number(draft.port) || 22
        const fields = {
            name: name, host: host, user: user, port: port,
            auth: draft.auth || "key",
            keyPath: String(draft.keyPath || keyField.text || "").trim(),
            defaultCommand: draft.defaultCommand || "",
            savePassword: !!draft.savePassword
        }
        if (draft.password && draft.password.length > 0)
            fields.password = draft.password
        if (!fields.host || !fields.user) {
            say("SSH — host and user are required")
            return
        }
        if (editing) {
            if (sshManager.updateConnection(selectedId, fields))
                say("SSH — saved")
        } else {
            const id = sshManager.addConnection(fields)
            if (id) {
                select(id)
                say("SSH — added")
            }
        }
        formPopup.close()
    }
    function runCmd(cmd) {
        if (!selectedId || !cmd.trim().length)
            return
        const c = cmd.trim()
        // local clear — wipe the pane without sending to the remote
        if (c === "clear" || c === "cls") {
            termOutput.text = ""
            cmdInput.clear()
            return
        }
        if (sshManager.shellOpen) {
            termOutput.text += c + "\n"
            sshManager.writeShell(c)
        } else {
            termOutput.text += "$ " + c + "\n"
            sshManager.runCommand(selectedId, c)
        }
        cmdInput.clear()
        termFlick.contentY = Math.max(0, termOutput.height - termFlick.height)
    }

    Component.onCompleted: {
        sshManager.statusMessage.connect(msg => tab.say(msg))
        sshManager.shellOutput.connect(chunk => {
            const clean = String(chunk).replace(/\x1b\[[0-9;?]*[A-Za-z]/g, "").replace(/\r/g, "")
            termOutput.text += clean
            termFlick.contentY = Math.max(0, termOutput.height - termFlick.height)
        })
        sshManager.shellStateChanged.connect(() => {
            // auto-clear the pane whenever the shell opens or closes
            termOutput.text = ""
            if (sshManager.shellOpen)
                termOutput.text = "— connected " + sshManager.shellHost + " —\n"
        })
        sshManager.testFinished.connect(r => {
            if (r.id !== tab.selectedId)
                return
            termOutput.text += (r.ok ? "✓ reachable (" + r.latencyMs + " ms)\n" : "✗ " + (r.error || "failed") + "\n")
        })
        sshManager.commandFinished.connect(r => {
            if (r.output && r.output.length)
                termOutput.text += r.output + (r.output.endsWith("\n") ? "" : "\n")
            if (!r.ok)
                termOutput.text += "— exit " + r.exitCode + " —\n"
            termFlick.contentY = Math.max(0, termOutput.height - termFlick.height)
        })
        select("")
    }

    component Field: ColumnLayout {
        id: fl
        property string label: ""
        property string placeholder: ""
        property string value: ""
        property bool mono: false
        property bool show: true
        property int echoMode: TextInput.Normal
        // live text always available for save, even if the draft binding lags
        readonly property alias text: tf.text
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
            echoMode: fl.echoMode
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
            onTextChanged: {
                // keep the draft in sync on every change (not only textEdited — paste/autofill too)
                if (text !== fl.value)
                    fl.edited(text)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 12

        // ---- hosts ----
        ColumnLayout {
            Layout.preferredWidth: 200
            Layout.maximumWidth: 230
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
                    onClicked: openForm("")
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
                        id: hRow
                        required property var modelData
                        readonly property bool sel: modelData.id === tab.selectedId
                        width: hostList.width
                        height: 40
                        radius: 6
                        color: sel ? Qt.alpha(theme.accent, 0.18) : hHov.hovered ? Qt.alpha(theme.text, 0.07) : "transparent"
                        border.width: sel ? 1 : 0
                        border.color: Qt.alpha(theme.accent, 0.4)
                        HoverHandler { id: hHov }
                        TapHandler { onTapped: tab.select(hRow.modelData.id) }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                menuId = hRow.modelData.id
                                tab.select(hRow.modelData.id)
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
                                text: hRow.modelData.name
                                color: theme.text
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: hRow.modelData.label
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

        // ---- terminal ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radiusMd
            color: theme.dark ? "#0d0d0f" : "#1a1a1c"
            border.width: 1
            border.color: theme.dark ? Qt.rgba(1, 1, 1, 0.1) : Qt.rgba(0, 0, 0, 0.15)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                // title bar
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: !sshManager.shellOpen ? (selectedId === "" ? theme.textFaint : theme.warn) : theme.success
                    }
                    Text {
                        text: selectedId === "" ? "Terminal — select a host"
                            : sshManager.shellOpen ? (draft.user || "?") + "@" + (draft.host || "?") + ":" + (draft.port || 22) + "  (shell)"
                            : (draft.user || "?") + "@" + (draft.host || "?") + ":" + (draft.port || 22) + "  (offline)"
                        color: "#c8c8cc"
                        font.family: "monospace"
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    PillButton {
                        text: sshManager.shellOpen ? "Disconnect" : "Connect"
                        checked: sshManager.shellOpen
                        enabled: selectedId !== ""
                        onClicked: {
                            if (sshManager.shellOpen)
                                sshManager.closeShell()
                            else
                                sshManager.openShell(selectedId)
                        }
                    }
                    PillButton {
                        text: "Test"
                        enabled: selectedId !== "" && !sshManager.busy
                        onClicked: sshManager.testConnection(selectedId)
                    }
                    PillButton {
                        text: "Terminal app"
                        iconName: "external"
                        enabled: selectedId !== ""
                        onClicked: sshManager.openTerminal(selectedId)
                    }
                    IconButton {
                        visible: selectedId !== ""
                        iconName: "pen"
                        Accessible.name: "Edit connection"
                        onClicked: openForm(selectedId)
                    }
                    IconButton {
                        visible: selectedId !== ""
                        iconName: "trash"
                        Accessible.name: "Delete connection"
                        onClicked: deleteDialog.open()
                    }
                }

                // scrollback
                Flickable {
                    id: termFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: termOutCol.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: termOutCol
                        width: termFlick.width
                        Text {
                            id: termOutput
                            width: parent.width
                            text: selectedId === ""
                                ? "Welcome to the SSH terminal.\nSelect a host on the left, or press + to add one.\n"
                                : ""
                            color: "#a8a8b0"
                            font.family: "monospace"
                            font.pixelSize: 12
                            wrapMode: Text.WrapAnywhere
                            textFormat: Text.PlainText
                        }
                    }

                    // click to focus the prompt
                    TapHandler {
                        onTapped: cmdInput.forceActiveFocus()
                    }
                }

                // prompt line
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        text: sshManager.shellOpen ? "$" : "›"
                        color: sshManager.shellOpen ? theme.success : theme.textFaint
                        font.family: "monospace"
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        Layout.alignment: Qt.AlignVCenter
                    }
                    TextField {
                        id: cmdInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        enabled: selectedId !== ""
                        placeholderText: !selectedId ? "Select a host first"
                            : sshManager.shellOpen ? "command…  (clear to wipe)" : "Connect to start the shell"
                        placeholderTextColor: "#5a5a5e"
                        color: "#e8e8ec"
                        font.family: "monospace"
                        font.pixelSize: 12
                        leftPadding: 8
                        rightPadding: 8
                        background: Rectangle {
                            radius: 6
                            color: "#1c1c1e"
                            border.width: 1
                            border.color: cmdInput.activeFocus ? Qt.alpha(theme.success, 0.5) : Qt.rgba(1, 1, 1, 0.08)
                        }
                        onAccepted: tab.runCmd(text)
                    }
                    PillButton {
                        text: "Send"
                        checked: true
                        enabled: selectedId !== "" && cmdInput.text.trim().length > 0
                        onClicked: tab.runCmd(cmdInput.text)
                    }
                }
            }
        }
    }

    // ---- modal: new / edit connection ----
    Popup {
        id: formPopup
        parent: Overlay.overlay
        // centre on the notch panel, not the whole window
        x: Math.max(12, (Overlay.overlay.width - width) / 2)
        y: Math.max(48, (Overlay.overlay.height - height) / 2)
        width: 400
        padding: 0
        modal: true
        dim: false
        Overlay.modal: Item {}
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: theme.radiusLg
            color: theme.surface
            border.width: 1
            border.color: theme.outline
        }

        contentItem: ColumnLayout {
            spacing: 0

            // header
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 14
                Layout.bottomMargin: 8
                Text {
                    text: editing ? "Edit connection" : "New connection"
                    color: theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                IconButton {
                    iconName: "close"
                    Accessible.name: "Close"
                    onClicked: formPopup.close()
                }
            }

            // form body
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(360, formCol.implicitHeight + 28)
                clip: true
                contentHeight: formCol.implicitHeight + 28
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: formCol
                    width: formPopup.width - 28
                    x: 14
                    y: 0
                    spacing: 12

                    Field {
                        id: nameField
                        label: "Name"
                        placeholder: "prod-web"
                        value: draft.name || ""
                        onEdited: t => draft.name = t
                    }

                    // Host (wide) | Port (fixed)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Field {
                            id: hostField
                            label: "Host"
                            placeholder: "10.0.0.5 or example.com"
                            value: draft.host || ""
                            mono: true
                            Layout.fillWidth: true
                            onEdited: t => draft.host = t
                        }
                        ColumnLayout {
                            Layout.preferredWidth: 100
                            Layout.maximumWidth: 110
                            spacing: 4
                            Text { text: "Port"; color: theme.textDim; font.pixelSize: 11; font.weight: Font.Medium }
                            TextField {
                                id: portField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                text: String(draft.port || 22)
                                color: theme.text
                                font.family: "monospace"
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                leftPadding: 8
                                rightPadding: 8
                                validator: IntValidator { bottom: 1; top: 65535 }
                                background: Rectangle {
                                    radius: 8
                                    color: theme.well
                                    border.width: 1
                                    border.color: portField.activeFocus ? Qt.alpha(theme.accent, 0.45) : theme.hairline
                                }
                                onEditingFinished: draft.port = parseInt(text) || 22
                            }
                        }
                    }

                    // User | Auth — equal halves
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Field {
                            id: userField
                            label: "User"
                            placeholder: "root"
                            value: draft.user || ""
                            mono: true
                            Layout.fillWidth: true
                            onEdited: t => draft.user = t
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "Auth"; color: theme.textDim; font.pixelSize: 11; font.weight: Font.Medium }
                            PillButton {
                                id: authBtn
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                text: draft.auth === "password" ? "Password" : "Key / agent"
                                onClicked: authMenu.popup()
                            }
                        }
                    }

                    Field {
                        id: keyField
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

            // footer actions
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 14
                Layout.topMargin: 4
                spacing: 6
                PillButton {
                    text: "Cancel"
                    onClicked: formPopup.close()
                }
                Item { Layout.fillWidth: true }
                PillButton {
                    text: editing ? "Save" : "Add"
                    checked: true
                    onClicked: tab.saveForm()
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

    DarkMenu {
        id: contextMenu
        MenuItem { text: "Open in terminal"; onTriggered: tab.select(menuId) }
        MenuItem { text: "Edit…"; onTriggered: openForm(menuId) }
        MenuItem { text: "Test"; onTriggered: sshManager.testConnection(menuId) }
        MenuItem { text: "Open external terminal"; onTriggered: sshManager.openTerminal(menuId) }
        MenuItem { text: "Duplicate"; onTriggered: sshManager.duplicateConnection(menuId) }
        MenuItem {
            text: "Delete"
            onTriggered: {
                selectedId = menuId
                deleteDialog.open()
            }
        }
    }

    Popup {
        id: deleteDialog
        parent: Overlay.overlay
        x: Math.max(12, (Overlay.overlay.width - width) / 2)
        y: Math.max(48, (Overlay.overlay.height - height) / 2)
        width: 320
        padding: 16
        modal: true
        dim: false
        Overlay.modal: Item {}
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: theme.radiusLg
            color: theme.surface
            border.width: 1
            border.color: theme.outline
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "Delete connection?"
                color: theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: "The host is removed from the list. Saved password (if any) is dropped too."
                wrapMode: Text.Wrap
                color: theme.textDim
                font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: deleteDialog.close() }
                PillButton {
                    text: "Delete"
                    checked: true
                    onClicked: {
                        const wasSelected = selectedId === tab.selectedId
                        sshManager.removeConnection(selectedId)
                        if (wasSelected)
                            tab.select("")
                        tab.say("SSH — deleted")
                        deleteDialog.close()
                    }
                }
            }
        }
    }
}
