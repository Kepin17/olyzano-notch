import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AbstractButton {
    id: ib
    implicitWidth: 26
    implicitHeight: 26
    background: Rectangle {
        radius: 13
        color: ib.checked ? theme.accentSoft : ib.down ? theme.surfacePressed : ib.hovered ? theme.surfaceHover : theme.surface
    }
    contentItem: Text {
        text: ib.text
        color: theme.text
        font.pixelSize: 14
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
