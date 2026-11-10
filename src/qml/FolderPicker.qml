import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Choose a folder inside the notch. A system dialog opens as a separate window that the notch covers; this one never does,
// and it can make a new folder too.
Popup {
    id: picker
    property string title: "Choose a folder"
    property string confirmText: "Choose this folder"
    property string current: ""
    property bool creating: false
    property bool picked: false
    signal chosen(string path)

    function start(path) {
        const start = path && codingAgent.folderExists(path) ? path : codingAgent.homeFolder()
        current = start
        creating = false
        picked = false
        open()
    }
    function go(path) { if (path && codingAgent.folderExists(path)) { current = path; list.positionViewAtBeginning() } }

    parent: Overlay.overlay
    x: Math.max(12, (Overlay.overlay.width - width) / 2)
    y: Math.max(12, (Overlay.overlay.height - height) / 2)
    width: Math.min(560, Overlay.overlay.width - 24)
    height: Math.min(440, Overlay.overlay.height - 24)
    padding: 14
    modal: true
    dim: false
    focus: true
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        color: theme.surface
        radius: theme.radiusLg
        border.width: 1
        border.color: theme.outline
    }
    contentItem: ColumnLayout {
        spacing: 8
        Text { text: picker.title; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            PillButton { iconOnly: true; iconName: "back"; text: "Up one folder"; implicitHeight: 30; onClicked: picker.go(codingAgent.parentFolder(picker.current)) }
            PillButton { text: "Home"; iconName: "folder"; implicitHeight: 30; onClicked: picker.go(codingAgent.homeFolder()) }
            TextField {
                id: pathField
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                text: picker.current
                color: theme.text
                font.pixelSize: 12
                font.family: "monospace"
                leftPadding: 10
                selectByMouse: true
                onAccepted: picker.go(text)
                background: Rectangle { radius: theme.radiusSm; color: theme.well; border.width: 1; border.color: pathField.activeFocus ? Qt.alpha(theme.accent, 0.55) : theme.hairline }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radiusSm
            color: theme.well
            border.width: 1
            border.color: theme.hairline
            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: picker.visible ? codingAgent.listFolders(picker.current) : []
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: list.width - 8
                    height: 32
                    radius: theme.radiusSm
                    color: rowArea.containsMouse ? theme.surfaceHover : "transparent"
                    MouseArea { id: rowArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: picker.go(row.modelData.path) }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8
                        LineIcon { glyph: "folder"; sourceSize: Qt.size(15, 15); color: theme.accentText }
                        Text { Layout.fillWidth: true; text: row.modelData.name; color: theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                        Text { text: "›"; color: theme.textFaint; font.pixelSize: 14 }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: "No folders here.\nYou can make one below, or choose this folder."
                    horizontalAlignment: Text.AlignHCenter
                    color: theme.textFaint
                    font.pixelSize: 12
                }
            }
        }
        RowLayout { // a new folder, made right here
            Layout.fillWidth: true
            visible: picker.creating
            spacing: 6
            TextField {
                id: newName
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                placeholderText: "New folder name"
                placeholderTextColor: theme.textFaint
                color: theme.text
                font.pixelSize: 12
                leftPadding: 10
                onVisibleChanged: if (visible) { text = ""; forceActiveFocus() }
                onAccepted: create.clicked()
                Keys.onEscapePressed: picker.creating = false
                background: Rectangle { radius: theme.radiusSm; color: theme.well; border.width: 1; border.color: newName.activeFocus ? Qt.alpha(theme.accent, 0.55) : theme.hairline }
            }
            PillButton {
                id: create
                text: "Create"
                primary: true
                implicitHeight: 30
                onClicked: {
                    const made = codingAgent.makeFolder(picker.current, newName.text)
                    if (made) { picker.creating = false; picker.go(made) }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PillButton { text: "New folder"; iconName: "plus"; implicitHeight: 30; visible: !picker.creating; onClicked: picker.creating = true }
            Text {
                Layout.fillWidth: true
                text: picker.current
                color: theme.textFaint
                font.pixelSize: 11
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignRight
            }
            PillButton { text: "Cancel"; implicitHeight: 30; onClicked: picker.close() }
            PillButton { text: picker.confirmText; primary: true; implicitHeight: 30; onClicked: { picker.picked = true; picker.chosen(picker.current); picker.close() } }
        }
    }
}
