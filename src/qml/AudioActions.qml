import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The tasks found in the recording, to tick off.
ScrollView {
    id: view
    required property var page
    contentWidth: availableWidth
    clip: true
    ColumnLayout {
        width: view.availableWidth - 10
        spacing: 6
        Text {
            visible: !audioIntel.notes.actions || audioIntel.notes.actions.length === 0
            Layout.fillWidth: true
            Layout.topMargin: 8
            wrapMode: Text.Wrap
            color: theme.textDim
            font.pixelSize: 12
            text: audioIntel.info.hasNotes ? "No tasks were found in this recording." : "Tasks come with the notes: download the language model to get them."
        }
        Repeater {
            model: audioIntel.notes.actions || []
            delegate: Rectangle {
                id: row
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: body.implicitHeight + 18
                radius: theme.radiusSm + 2
                color: rowHover.hovered ? theme.hoverFill : theme.well
                HoverHandler { id: rowHover }
                RowLayout {
                    id: body
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 10 }
                    spacing: 10
                    Rectangle { // the circle to tick
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        radius: 10
                        color: row.modelData.done ? theme.accent : "transparent"
                        border.width: 1.5
                        border.color: row.modelData.done ? theme.accent : theme.textDim
                        LineIcon { anchors.centerIn: parent; visible: row.modelData.done; glyph: "check"; sourceSize: Qt.size(12, 12); color: theme.onColor(theme.accent) }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: audioIntel.setActionDone(row.modelData.id, !row.modelData.done) }
                        Accessible.role: Accessible.CheckBox
                        Accessible.name: "Done"
                        Accessible.checked: row.modelData.done
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text { Layout.fillWidth: true; text: row.modelData.task; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: row.modelData.done ? theme.textFaint : theme.text; font.pixelSize: 13; font.strikeout: row.modelData.done }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [row.modelData.assignee !== "" ? row.modelData.assignee : "No owner said", row.modelData.deadline !== "" ? "By " + row.modelData.deadline : "No date said", row.modelData.priority]
                                delegate: Text { required property string modelData; visible: modelData !== ""; text: modelData; textFormat: Text.PlainText; color: theme.textDim; font.pixelSize: 11 }
                            }
                            Text { visible: row.modelData.review; text: "Needs review"; color: theme.warn; font.pixelSize: 11 }
                        }
                    }
                    AudioTimeLink { page: view.page; seconds: row.modelData.time }
                }
            }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
