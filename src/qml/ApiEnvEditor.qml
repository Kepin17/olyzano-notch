import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Environments of the API tester: the list on the left, the chosen one's name and variables on the right. Every edit is saved
// at once; a variable marked secret has its value hidden. {{name}} in a request is replaced from the environment that is in use.
Popup {
    id: ed
    modal: true
    dim: false
    Overlay.modal: Item {}
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 40 : 680, 700)
    height: 400
    padding: 14
    property string currentId: ""
    property string envName: ""
    property var vars: []
    property var revealed: ({}) // secret rows whose value is shown for now

    background: Rectangle {
        radius: theme.radiusMd
        color: theme.card
        border.width: 1
        border.color: theme.outline
    }

    function load() {
        const e = currentId !== "" ? apiTester.environmentById(currentId) : {}
        envName = e.name || ""
        vars = e.variables || []
        revealed = ({})
    }
    function save() {
        if (currentId !== "")
            apiTester.updateEnvironment(currentId, { name: envName, variables: vars })
    }
    function pick(id) {
        currentId = id
        apiTester.activeEnvId = id
        load()
    }
    function setVar(i, field, val) {
        const a = vars.slice()
        const row = Object.assign({}, a[i])
        row[field] = field === "enabled" || field === "secret" ? !row[field] : val
        a[i] = row
        vars = a
        save()
    }
    onOpened: {
        if (apiTester.environments.length === 0)
            apiTester.newEnvironment("Environment 1")
        const known = apiTester.environments.find(e => e.id === apiTester.activeEnvId)
        pick(known ? known.id : apiTester.environments[0].id)
    }

    contentItem: RowLayout {
        spacing: 12

        // ---- the environments ----
        ColumnLayout {
            Layout.preferredWidth: 150
            Layout.fillHeight: true
            spacing: 8
            Text { text: "Environments"; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                model: apiTester.environments
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 30
                    radius: theme.radiusSm
                    color: modelData.id === ed.currentId ? Qt.alpha(theme.accent, 0.16) : hov.hovered ? theme.hoverFill : "transparent"
                    HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: ed.pick(modelData.id) }
                    Text {
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 10; rightMargin: 8 }
                        text: modelData.name
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: modelData.id === ed.currentId ? theme.accent : theme.text
                        font.pixelSize: 12
                        font.weight: modelData.id === apiTester.activeEnvId ? Font.DemiBold : Font.Normal
                    }
                }
            }
            RowLayout {
                spacing: 4
                PillButton { text: "New"; iconName: "plus"; onClicked: ed.pick(apiTester.newEnvironment("Environment " + (apiTester.environments.length + 1))) }
                IconButton { iconName: "copy"; Accessible.name: "Duplicate environment"; enabled: ed.currentId !== ""
                    onClicked: { const id = apiTester.duplicateEnvironment(ed.currentId); if (id) ed.pick(id) } }
                IconButton { iconName: "trash"; Accessible.name: "Delete environment"; enabled: ed.currentId !== ""
                    onClicked: {
                        apiTester.removeEnvironment(ed.currentId)
                        const first = apiTester.environments[0]
                        ed.pick(first ? first.id : "")
                    } }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: theme.hairline }

        // ---- the chosen one ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            TextField {
                Layout.fillWidth: true
                implicitHeight: 32
                text: ed.envName
                placeholderText: "Environment name"
                placeholderTextColor: theme.textFaint
                color: theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                leftPadding: 10; rightPadding: 10
                background: Rectangle { radius: 8; color: theme.well; border.width: 1; border.color: parent.activeFocus ? Qt.alpha(theme.accent, 0.5) : theme.hairline }
                onEditingFinished: { if (text.trim() !== "" && text !== ed.envName) { ed.envName = text.trim(); ed.save() } }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Item { Layout.preferredWidth: 22 }
                Text { Layout.fillWidth: true; Layout.preferredWidth: 1; text: "VARIABLE"; color: theme.textFaint; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 1 }
                Text { Layout.fillWidth: true; Layout.preferredWidth: 1; text: "VALUE"; color: theme.textFaint; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 1 }
                Item { Layout.preferredWidth: 58 }
            }

            ListView {
                id: varList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                model: ed.vars
                ScrollBar.vertical: ScrollBar { padding: 2 }

                Text {
                    visible: varList.count === 0
                    anchors.centerIn: parent
                    width: parent.width - 20
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: theme.textDim
                    font.pixelSize: 12
                    text: "No variables yet.\nAdd one, then write {{name}} in a URL, header or body."
                }

                delegate: RowLayout {
                    required property int index
                    required property var modelData
                    width: varList.width - 8
                    height: 30
                    spacing: 6
                    Rectangle {
                        Layout.preferredWidth: 16; Layout.preferredHeight: 16; Layout.leftMargin: 3; radius: 4
                        color: modelData.enabled ? Qt.alpha(theme.accent, 0.15) : "transparent"
                        border.width: 1
                        border.color: modelData.enabled ? Qt.alpha(theme.accent, 0.5) : theme.hairline
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: ed.setVar(index, "enabled") }
                        Text { visible: modelData.enabled; anchors.centerIn: parent; text: "✓"; color: theme.accent; font.pixelSize: 10; font.weight: Font.Bold }
                    }
                    TextField {
                        Layout.fillWidth: true; Layout.preferredWidth: 1
                        implicitHeight: 28
                        text: modelData.key
                        placeholderText: "name"
                        placeholderTextColor: theme.textFaint
                        color: modelData.enabled ? theme.text : theme.textDim
                        font.family: "monospace"; font.pixelSize: 12
                        leftPadding: 8; rightPadding: 8
                        background: Rectangle { radius: 6; color: theme.well; border.width: 1; border.color: parent.activeFocus ? Qt.alpha(theme.accent, 0.4) : theme.hairline }
                        onEditingFinished: ed.setVar(index, "key", text)
                    }
                    TextField {
                        Layout.fillWidth: true; Layout.preferredWidth: 1
                        implicitHeight: 28
                        text: modelData.value
                        echoMode: modelData.secret && !ed.revealed[index] ? TextInput.Password : TextInput.Normal
                        placeholderText: "value"
                        placeholderTextColor: theme.textFaint
                        color: modelData.enabled ? theme.textSoft : theme.textDim
                        font.family: "monospace"; font.pixelSize: 12
                        leftPadding: 8; rightPadding: 8
                        background: Rectangle { radius: 6; color: theme.well; border.width: 1; border.color: parent.activeFocus ? Qt.alpha(theme.accent, 0.4) : theme.hairline }
                        onEditingFinished: ed.setVar(index, "value", text)
                    }
                    IconButton {
                        implicitWidth: 24; implicitHeight: 24
                        iconName: modelData.secret ? "lock" : "unlock"
                        checked: modelData.secret
                        Accessible.name: modelData.secret ? "Secret: value hidden" : "Make this value secret"
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.secret ? "Secret: the value is hidden" : "Hide this value"
                        onClicked: ed.setVar(index, "secret")
                    }
                    IconButton {
                        implicitWidth: 24; implicitHeight: 24
                        iconName: "close"
                        Accessible.name: "Remove variable"
                        onClicked: { const a = ed.vars.slice(); a.splice(index, 1); ed.vars = a; ed.save() }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                PillButton {
                    text: "Add variable"
                    iconName: "plus"
                    onClicked: { const a = ed.vars.slice(); a.push({ key: "", value: "", enabled: true, secret: false }); ed.vars = a; ed.save(); varList.positionViewAtEnd() }
                }
                Item { Layout.fillWidth: true }
                PillButton { text: "Done"; checked: true; onClicked: ed.close() }
            }
        }
    }
}
