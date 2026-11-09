import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Text tabs with a line under the chosen one; `actions` (right) are whatever the page puts there. model: [[value, label], ...]
Item {
    id: tabs
    property var model: []
    property var value
    default property alias actions: right.data
    signal picked(var value)
    implicitHeight: 34
    Row {
        id: row
        spacing: 20
        height: parent.height
        Repeater {
            model: tabs.model
            delegate: AbstractButton {
                id: tab
                required property var modelData
                readonly property bool on: tabs.value === modelData[0]
                height: tabs.height
                width: label.implicitWidth
                onClicked: tabs.picked(modelData[0])
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                contentItem: Text {
                    id: label
                    text: tab.modelData[1]
                    color: tab.on ? theme.text : tab.hovered ? theme.textSoft : theme.textDim
                    font.pixelSize: 13
                    font.weight: tab.on ? Font.DemiBold : Font.Normal
                    verticalAlignment: Text.AlignVCenter
                    Behavior on color { enabled: activities.animations; ColorAnimation { duration: 120 } }
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 2
                    radius: 1
                    color: theme.accent
                    opacity: tab.on ? 1 : 0
                    Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 120 } }
                }
            }
        }
    }
    Row {
        id: right
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        spacing: 6
    }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: theme.hairline
        z: -1
    }
}
