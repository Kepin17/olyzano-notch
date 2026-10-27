import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AbstractButton {
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    id: tg
    implicitWidth: 40
    implicitHeight: 22
    padding: 0
    background: Rectangle {
        radius: 11
        color: tg.checked ? theme.accent : theme.pressFill
        border.width: tg.checked ? 0 : 1
        border.color: theme.hairline
        Behavior on color { enabled: activities.animations; ColorAnimation { duration: 150 } }
    }
    contentItem: Item {
        Rectangle {
            y: 3
            x: tg.checked ? parent.width - width - 3 : 3
            width: 16
            height: 16
            radius: 8
            color: tg.checked ? theme.onColor(theme.accent) : theme.textSoft
            Behavior on x { enabled: activities.animations; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }
    }
}
