import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Text button; `iconName` adds a line icon before the text.
AbstractButton {
    id: pb
    property string iconName
    implicitWidth: pbRow.implicitWidth + 22
    implicitHeight: 26
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        radius: theme.washi ? theme.chipRadius : height / 2
        color: pb.checked ? theme.accent : pb.down ? theme.surfacePressed : pb.hovered ? theme.surfaceHover : theme.surface
        border.width: theme.washi && !pb.checked ? 1 : 0
        border.color: theme.outline
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    contentItem: Item {
        Row {
            id: pbRow
            anchors.centerIn: parent
            spacing: 4
            LineIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: pb.iconName !== ""
                glyph: pb.iconName
                sourceSize: Qt.size(13, 13)
                color: pbLabel.color
            }
            Text {
                id: pbLabel
                text: pb.text
                color: pb.checked ? "white" : theme.text // on the accent fill, always white
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
    }
}
