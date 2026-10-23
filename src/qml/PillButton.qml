import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AbstractButton {
    id: pb
    implicitWidth: pbLabel.implicitWidth + 22
    implicitHeight: 26
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        radius: height / 2
        color: pb.checked ? theme.accent : pb.down ? theme.surfacePressed : pb.hovered ? theme.surfaceHover : theme.surface
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    contentItem: Text {
        id: pbLabel
        text: pb.text
        color: pb.checked ? "white" : theme.text // on the accent fill, always white
        font.pixelSize: 12
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
