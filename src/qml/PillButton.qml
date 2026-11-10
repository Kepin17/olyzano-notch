import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Text button; `iconName` adds a line icon before the text.
AbstractButton {
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    id: pb
    property string iconName
    property bool primary: false  // the one thing to do on this page: the solid accent
    property bool iconOnly: false // just the icon (tooltip and screen readers keep the text), when there is no room for words
    implicitWidth: iconOnly ? 36 : pbRow.implicitWidth + 28
    implicitHeight: theme.controlHeight
    opacity: enabled ? 1 : 0.4
    Accessible.name: text
    ToolTip.visible: iconOnly && hovered
    ToolTip.delay: 400
    ToolTip.text: text
    // Filled, no outline (washi keeps its ink line): quiet grey for most, a tint of the accent when chosen, the accent itself for the main action.
    background: Rectangle {
        radius: theme.washi ? theme.chipRadius : theme.radiusSm
        color: pb.primary ? (pb.down ? Qt.darker(theme.accent, 1.18) : pb.hovered ? Qt.lighter(theme.accent, 1.08) : theme.accent)
             : pb.checked ? (pb.hovered ? Qt.alpha(theme.accent, 0.26) : Qt.alpha(theme.accent, 0.18))
             : pb.down ? theme.surfacePressed
             : pb.hovered ? theme.surfaceHover
             : theme.surface
        border.width: theme.washi || pb.visualFocus ? 1 : 0
        border.color: pb.visualFocus ? theme.accent : pb.checked ? Qt.alpha(theme.accent, 0.45) : theme.outline
        Behavior on color { enabled: activities.animations; ColorAnimation { duration: 120 } }
    }
    contentItem: Item {
        Row {
            id: pbRow
            anchors.centerIn: parent
            spacing: 6
            LineIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: pb.iconName !== ""
                glyph: pb.iconName
                sourceSize: Qt.size(14, 14)
                color: pbLabel.color
            }
            Text {
                id: pbLabel
                visible: !pb.iconOnly
                text: pb.text
                color: pb.primary ? theme.onColor(theme.accent) : pb.checked ? theme.accentText : theme.text
                font.pixelSize: 13
                font.weight: pb.checked || pb.primary ? Font.DemiBold : Font.Medium
            }
        }
    }
}
