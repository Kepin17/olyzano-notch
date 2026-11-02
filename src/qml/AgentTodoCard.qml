import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The agent's to-do list for a bigger task. `tab` holds the items.
Rectangle { // the agent's own checklist for a bigger task
    id: todoCard
    required property var tab
    readonly property int total: tab.todoItems.length
    readonly property int done: tab.todoItems.filter(t => t.status === "completed").length
    readonly property var current: tab.todoItems.find(t => t.status === "in_progress")
    property bool open: true
    visible: total > 0
    Layout.fillWidth: true
    Layout.preferredHeight: todoCol.implicitHeight + 16
    radius: theme.radiusMd
    color: theme.well
    border.width: 1
    border.color: done === total ? Qt.alpha("#30d158", 0.5) : theme.hairline
    ColumnLayout {
        id: todoCol
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            MouseArea { // the whole header folds the list
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                cursorShape: Qt.PointingHandCursor
                onClicked: todoCard.open = !todoCard.open
                RowLayout {
                    anchors.fill: parent
                    spacing: 8
                    LineIcon { glyph: todoCard.done === todoCard.total ? "check" : "grid"; sourceSize: Qt.size(14, 14); color: todoCard.done === todoCard.total ? "#30d158" : theme.accentText }
                    Text { text: "To-do"; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                    AgentBadge { text: todoCard.done + " / " + todoCard.total; tone: todoCard.done === todoCard.total ? "ok" : "accent"; mono: true }
                    Text {
                        Layout.fillWidth: true
                        visible: !todoCard.open && !!todoCard.current
                        text: todoCard.current ? todoCard.current.content : ""
                        color: theme.textDim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Item { Layout.fillWidth: true; visible: todoCard.open || !todoCard.current }
                    Text { text: todoCard.open ? "⌃" : "⌄"; color: theme.textFaint; font.pixelSize: 11 }
                }
            }
        }
        Rectangle { // progress
            Layout.fillWidth: true
            height: 3; radius: 1.5
            color: theme.hairline
            Rectangle { width: parent.width * (todoCard.total > 0 ? todoCard.done / todoCard.total : 0); height: parent.height; radius: parent.radius; color: todoCard.done === todoCard.total ? "#30d158" : theme.accent
                Behavior on width { enabled: activities.animations; NumberAnimation { duration: 200 } } }
        }
        Flickable {
            visible: todoCard.open
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(150, todoList.implicitHeight)
            contentHeight: todoList.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: todoList
                width: parent.width - 10
                spacing: 3
                Repeater {
                    model: tab.todoItems
                    delegate: RowLayout {
                        id: todoRow
                        required property var modelData
                        readonly property bool finished: modelData.status === "completed"
                        readonly property bool active: modelData.status === "in_progress"
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            Layout.preferredWidth: 14; Layout.preferredHeight: 14; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2
                            radius: 7
                            color: todoRow.finished ? Qt.alpha("#30d158", 0.2) : "transparent"
                            border.width: 1.5
                            border.color: todoRow.finished ? "#30d158" : todoRow.active ? theme.accent : theme.outline
                            LineIcon { anchors.centerIn: parent; visible: todoRow.finished; glyph: "check"; sourceSize: Qt.size(9, 9); color: "#30d158" }
                            Rectangle { anchors.centerIn: parent; visible: todoRow.active; width: 6; height: 6; radius: 3; color: theme.accent }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: todoRow.modelData.content
                            color: todoRow.finished ? theme.textFaint : todoRow.active ? theme.text : theme.textDim
                            font.pixelSize: 13
                            font.weight: todoRow.active ? Font.Medium : Font.Normal
                            font.strikeout: todoRow.finished
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
