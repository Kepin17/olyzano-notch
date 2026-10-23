import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Menu {
    padding: 6
    background: Rectangle {
        implicitWidth: 220
        radius: 12
        color: theme.surface
        border.color: theme.surfaceHover
    }
    delegate: MenuItem {
        id: mi
        implicitHeight: 30
        contentItem: Text {
            text: mi.text
            color: mi.enabled ? theme.text : theme.textDim
            font.pixelSize: 12
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
            leftPadding: 4
        }
        background: Rectangle {
            radius: 8
            color: mi.highlighted && mi.enabled ? theme.surfaceHover : "transparent"
        }
    }
}
