import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AbstractButton {
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    id: tg
    implicitWidth: 42
    implicitHeight: 24
    padding: 0
    background: Rectangle {
        radius: height / 2
        color: tg.checked ? theme.accent : theme.surfacePressed
        border.width: tg.visualFocus ? 1 : 0
        border.color: theme.accent
        Behavior on color { enabled: activities.animations; ColorAnimation { duration: 150 } }
    }
    contentItem: Item {
        Rectangle {
            y: 3
            x: tg.checked ? parent.width - width - 3 : 3
            width: 18
            height: 18
            radius: 9
            color: tg.checked ? theme.onColor(theme.accent) : theme.text
            Behavior on x { enabled: activities.animations; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }
    }
}
