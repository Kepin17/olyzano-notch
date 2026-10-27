import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Round header button: a line icon (`iconName`) or, without one, its text.
AbstractButton {
    id: ib
    property string iconName
    implicitWidth: 28
    implicitHeight: 28
    background: Rectangle {
        radius: theme.washi ? theme.chipRadius : theme.radiusSm
        color: ib.checked ? Qt.alpha(theme.accent, 0.16) : ib.down ? theme.pressFill : ib.hovered ? theme.hoverFill : "transparent"
        border.width: 1
        border.color: ib.checked ? Qt.alpha(theme.accent, 0.45) : theme.washi ? theme.outline : theme.hairline
    }
    contentItem: Item {
        LineIcon {
            anchors.centerIn: parent
            visible: ib.iconName !== ""
            glyph: ib.iconName
            color: ib.checked ? theme.accent : theme.text
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
