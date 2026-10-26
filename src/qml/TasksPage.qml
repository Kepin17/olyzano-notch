import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Your tasks and their deadlines. One line adds a task: "Buy milk tomorrow 17:00", "Call Ana @ in 45m". The list is
// ordered by deadline; the nearest one also shows in the idle capsule, and a note comes 15 minutes before and at the deadline.
ColumnLayout {
    id: page
    required property var shell
    visible: shell.page === "tasks"
    Layout.fillWidth: true
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 8

    function add() {
        if (tasks.addQuick(field.text))
            field.text = ""
    }
    onVisibleChanged: if (visible) field.forceActiveFocus()

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        TextField {
            id: field
            Layout.fillWidth: true
            implicitHeight: 30
            leftPadding: 12
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            placeholderText: "New task, e.g. Buy milk tomorrow 17:00"
            placeholderTextColor: theme.placeholder
            color: shell.textPrimary
            selectionColor: shell.accent
            font.pixelSize: 12
            background: Rectangle {
                radius: theme.radiusMd
                color: theme.surface
                border.width: 1
                border.color: field.activeFocus ? shell.accent : "transparent"
            }
            onAccepted: page.add()
            Keys.onEscapePressed: shell.page = "shelf"
        }
        PillButton { text: "Add"; iconName: "plus"; enabled: field.text.trim() !== ""; onClicked: page.add() }
    }
    Flow { // a deadline in one tap: it is added to what is typed
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: [["in 1h", "In 1 hour"], ["today 18:00", "This evening"], ["tomorrow 09:00", "Tomorrow 9:00"]]
            PillButton {
                required property var modelData
                text: modelData[1]
                onClicked: {
                    const q = field.text.trim().replace(/\s*@.*$/, "")
                    field.text = (q !== "" ? q + " @ " : "") + modelData[0]
                    field.forceActiveFocus()
                }
            }
        }
    }
    Text {
        Layout.fillWidth: true
        visible: field.text.trim() !== ""
        text: "→ " + tasks.preview(field.text)
        color: theme.textDim
        font.pixelSize: 11
        elide: Text.ElideRight
    }

    Text {
        visible: tasks.list.length === 0
        Layout.fillWidth: true
        Layout.topMargin: 14
        horizontalAlignment: Text.AlignHCenter
        text: "No tasks yet. Add one above; give it a time and Notch reminds you."
        color: theme.textDim
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    Flickable {
        visible: tasks.list.length > 0
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(col.implicitHeight, 190)
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: col
            width: parent.width
            spacing: 4
            Repeater {
                model: tasks.list
                Rectangle {
                    id: row
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 34
                    radius: theme.radiusMd
                    color: rowHover.hovered ? theme.surfaceHover : theme.surface
                    readonly property color tint: modelData.state === "overdue" ? theme.danger : modelData.state === "soon" ? theme.accent : theme.textDim
                    HoverHandler { id: rowHover }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 10
                        Rectangle { // the check circle
                            Layout.preferredWidth: 18; Layout.preferredHeight: 18
                            radius: 9
                            color: row.modelData.done ? theme.accent : "transparent"
                            border.width: 1.5
                            border.color: row.modelData.done ? theme.accent : row.tint
                            LineIcon { anchors.centerIn: parent; visible: row.modelData.done; glyph: "check"; sourceSize: Qt.size(11, 11); color: theme.onColor(theme.accent) }
                            TapHandler { onTapped: tasks.toggle(row.modelData.id) }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.title
                            color: row.modelData.done ? theme.textFaint : shell.textPrimary
                            font.pixelSize: 13
                            font.strikeout: row.modelData.done
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: row.modelData.hasDue && !row.modelData.done
                            text: row.modelData.remaining
                            color: row.tint
                            font.pixelSize: 11
                            font.weight: row.modelData.state === "none" || row.modelData.state === "later" ? Font.Normal : Font.DemiBold
                        }
                        IconButton {
                            visible: rowHover.hovered
                            iconName: "trash"
                            Accessible.name: "Delete task"
                            onClicked: tasks.remove(row.modelData.id)
                        }
                    }
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: tasks.list.length > 0
        Text {
            Layout.fillWidth: true
            text: tasks.openCount + " open" + (tasks.overdueCount > 0 ? " · " + tasks.overdueCount + " overdue" : "")
            color: tasks.overdueCount > 0 ? theme.danger : theme.textDim
            font.pixelSize: 11
        }
        PillButton { text: "Clear done"; onClicked: tasks.clearDone() }
    }
}
