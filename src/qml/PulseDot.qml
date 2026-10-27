import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    implicitWidth: 8
    implicitHeight: 8
    radius: 4
    color: theme.accent
    SequentialAnimation on opacity {
        running: visible && activities.animations
        loops: Animation.Infinite
        NumberAnimation { to: 0.25; duration: 550; easing.type: Easing.InOutSine }
        NumberAnimation { to: 1; duration: 550; easing.type: Easing.InOutSine }
    }
}
