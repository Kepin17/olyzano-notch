import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AbstractButton {
    id: tg
    implicitWidth: 42
    implicitHeight: 24
    padding: 0
    background: Rectangle {
        radius: 12
        color: tg.checked ? theme.success : theme.surfacePressed
        Behavior on color { ColorAnimation { duration: 150 } }
    }
    contentItem: Item {
        Rectangle {
            y: 2
            x: tg.checked ? parent.width - width - 2 : 2
            width: 20
            height: 20
            radius: 10
            color: "white"
            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
        }
    }
}
