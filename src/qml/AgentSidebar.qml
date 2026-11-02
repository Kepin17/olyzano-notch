import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The tasks of Dev → Agent, grouped by folder. `tab` is the Agent tab that owns the state (renaming, locked, detail).
Rectangle { // the tasks, grouped by folder
    id: sidebar
    required property var tab
    property var model
    signal addFolder()
    signal deleteTask(string sid)
    Layout.preferredWidth: 220
    Layout.fillHeight: true
    radius: theme.radiusMd
    color: theme.well
    border.width: 1
    border.color: theme.hairline
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                text: "TASKS"
                color: theme.textFaint
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 0.8
            }
            PillButton { // one line per action, or everything
                iconOnly: true; iconName: "eye"
                text: tab.detailFull ? "Showing everything: click for one line per action" : "One line per action: click to show everything"
                checked: tab.detailFull
                implicitHeight: 26
                implicitWidth: 30
                onClicked: codingAgent.detail = tab.detailFull ? "headings" : "full"
            }
            PillButton {
                iconOnly: true; iconName: "folder"; text: "Add a folder"
                implicitHeight: 26
                implicitWidth: 30
                enabled: !tab.locked
                onClicked: sidebar.addFolder()
            }
            PillButton {
                iconOnly: true; iconName: "plus"; text: "New task in this folder"
                implicitHeight: 26
                implicitWidth: 30
                enabled: !tab.locked
                onClicked: tab.newTask()
            }
        }
        ListView {
            id: sessionList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 1
            boundsBehavior: Flickable.StopAtBounds
            model: sidebar.model
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: item
                required property string kind
                required property string rowKey
                required property string path
                required property string name
                required property string title
                required property string sid
                required property int count
                required property bool collapsed
                required property bool exists
                required property bool active
                readonly property bool isFolder: kind === "folder"
                readonly property bool renaming: tab.renamingKey === rowKey
                width: sessionList.width - 8
                height: isFolder ? 32 : 34
                radius: theme.radiusSm
                color: !isFolder && active ? Qt.alpha(theme.accent, 0.16) : rowArea.containsMouse && !renaming ? theme.surfaceHover : "transparent"
                opacity: !isFolder && tab.locked && !active ? 0.5 : 1
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton
                    enabled: !item.renaming
                    onClicked: {
                        if (item.isFolder) codingAgent.setFolderCollapsed(item.path, !item.collapsed)
                        else if (!tab.locked) codingAgent.selectSession(item.sid)
                    }
                    onDoubleClicked: if (!item.isFolder && !tab.locked) tab.startRename(item.rowKey, item.title)
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: item.isFolder ? 6 : 24
                    anchors.rightMargin: 6
                    spacing: 6
                    Text { visible: item.isFolder; text: item.collapsed ? "›" : "⌄"; color: theme.textFaint; font.pixelSize: 12; Layout.preferredWidth: 10 }
                    LineIcon { visible: item.isFolder; glyph: "folder"; sourceSize: Qt.size(14, 14); color: item.exists === false ? "#ff9f0a" : item.active ? theme.accentText : theme.textDim }
                    Text {
                        visible: !item.renaming
                        Layout.fillWidth: true
                        text: item.isFolder ? item.name : (item.title || "New task")
                        color: item.isFolder ? theme.text : item.active ? theme.accentText : theme.text
                        font.pixelSize: item.isFolder ? 12 : 12
                        font.weight: item.isFolder ? Font.DemiBold : Font.Medium
                        elide: Text.ElideRight
                    }
                    TextField { // renaming, right where the name is
                        id: renameField
                        visible: item.renaming
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        color: theme.text
                        font.pixelSize: 12
                        leftPadding: 6
                        selectByMouse: true
                        background: Rectangle { radius: 5; color: theme.surface; border.width: 1; border.color: Qt.alpha(theme.accent, 0.6) }
                        onVisibleChanged: if (visible) { text = tab.renameText; forceActiveFocus(); selectAll() }
                        onAccepted: tab.commitRename(text)
                        Keys.onEscapePressed: tab.renamingKey = ""
                        onActiveFocusChanged: if (!activeFocus && item.renaming) tab.commitRename(text)
                    }
                    Text { visible: item.isFolder && !item.renaming && !rowArea.containsMouse; text: item.count; color: theme.textFaint; font.pixelSize: 11 }
                    // actions, on hover
                    Row {
                        visible: !item.renaming && rowArea.containsMouse && !tab.locked
                        spacing: 2
                        Repeater {
                            model: item.isFolder
                                ? (item.count === 0 ? ["plus", "pen", "trash"] : ["plus", "pen"])
                                : ["pen", "trash"]
                            delegate: MouseArea {
                                id: act
                                required property string modelData
                                width: 22; height: 22
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (modelData === "plus") codingAgent.openFolder(item.path)
                                    else if (modelData === "pen") tab.startRename(item.rowKey, item.isFolder ? item.name : item.title)
                                    else if (item.isFolder) codingAgent.removeFolder(item.path)
                                    else sidebar.deleteTask(item.sid)
                                }
                                LineIcon { anchors.centerIn: parent; glyph: act.modelData; sourceSize: Qt.size(13, 13); color: act.containsMouse ? theme.accentText : theme.textFaint }
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 400
                                ToolTip.text: act.modelData === "plus" ? "New task in this folder" : act.modelData === "pen" ? "Rename"
                                    : item.isFolder ? "Remove this folder from the list (files stay)" : "Delete task"
                            }
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: sessionList.count === 0
                text: "No folders yet.\nAdd one with the folder button."
                horizontalAlignment: Text.AlignHCenter
                color: theme.textFaint
                font.pixelSize: 11
            }
        }
    }
}
