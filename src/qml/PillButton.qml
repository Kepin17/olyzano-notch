import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Text button; `iconName` adds a line icon before the text.
AbstractButton {
    id: pb
    property string iconName
    property bool iconOnly: false // just the icon (tooltip and screen readers keep the text), when there is no room for words
    implicitWidth: iconOnly ? 34 : pbRow.implicitWidth + 22
    implicitHeight: 28
    opacity: enabled ? 1 : 0.4
    Accessible.name: text
    ToolTip.visible: iconOnly && hovered
    ToolTip.delay: 400
    ToolTip.text: text
    // Hairline outline, no fill until the pointer is there; the chosen one is tinted with the accent (not a solid block).
    background: Rectangle {
        radius: theme.washi ? theme.chipRadius : theme.radiusSm
        color: pb.checked ? Qt.alpha(theme.accent, 0.16) : pb.down ? theme.pressFill : pb.hovered ? theme.hoverFill : "transparent"
        border.width: 1
        border.color: pb.checked ? Qt.alpha(theme.accent, 0.45) : theme.washi ? theme.outline : theme.hairline
        Behavior on color { enabled: activities.animations; ColorAnimation { duration: 120 } }
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
                color: pb.checked ? theme.accentText : theme.text
                font.pixelSize: 12
                font.weight: pb.checked ? Font.DemiBold : Font.Medium
            }
        }
    }
}
