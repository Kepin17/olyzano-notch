import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Text button; `iconName` adds a line icon before the text.
AbstractButton {
    id: pb
    property string iconName
    property bool iconOnly: false // just the icon (tooltip and screen readers keep the text), when there is no room for words
    implicitWidth: iconOnly ? 34 : pbRow.implicitWidth + 22
    implicitHeight: 26
    opacity: enabled ? 1 : 0.4
    Accessible.name: text
    ToolTip.visible: iconOnly && hovered
    ToolTip.delay: 400
    ToolTip.text: text
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
                visible: !pb.iconOnly
                text: pb.text
                color: pb.checked ? theme.onColor(theme.accent) : theme.text // whichever of white / dark reads on the accent
                font.pixelSize: 12
                font.weight: pb.checked ? Font.DemiBold : Font.Medium
            }
        }
    }
}
