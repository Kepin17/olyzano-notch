import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Round header button: a line icon (`iconName`) or, without one, its text.
AbstractButton {
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    id: ib
    property string iconName
    implicitWidth: 30
    implicitHeight: 30
    background: Rectangle {
        radius: theme.washi ? theme.chipRadius : theme.radiusSm
        color: ib.checked ? Qt.alpha(theme.accent, 0.16) : ib.down ? theme.pressFill : ib.hovered ? theme.hoverFill : "transparent"
        border.width: theme.washi || ib.visualFocus ? 1 : 0 // no outline: it shows when the pointer is on it
        border.color: ib.visualFocus ? theme.accent : ib.checked ? Qt.alpha(theme.accent, 0.45) : theme.outline
    }
    contentItem: Item {
        LineIcon {
            anchors.centerIn: parent
            visible: ib.iconName !== ""
            glyph: ib.iconName
            color: ib.checked ? theme.accentText : theme.text
        }
        Text {
            anchors.fill: parent
            visible: ib.iconName === ""
            text: ib.text
            color: theme.text
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
