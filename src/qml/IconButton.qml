import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Round header button: a line icon (`iconName`) or, without one, its text.
AbstractButton {
    id: ib
    property string iconName
    implicitWidth: 26
    implicitHeight: 26
    background: Rectangle {
        radius: theme.washi ? theme.chipRadius : 13
        color: ib.checked ? theme.accentSoft : ib.down ? theme.surfacePressed : ib.hovered ? theme.surfaceHover : theme.surface
        border.width: theme.washi ? 1 : 0
        border.color: ib.checked ? theme.accent : theme.outline
    }
    contentItem: Item {
        LineIcon {
            anchors.centerIn: parent
            visible: ib.iconName !== ""
            glyph: ib.iconName
            color: ib.checked && theme.washi ? theme.accent : theme.text
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
