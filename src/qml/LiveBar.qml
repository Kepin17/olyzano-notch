import QtQuick

// A progress line that feels alive: the fill glides to each new value, and while it is working a soft light runs along it, so a long stage never looks stuck.
Item {
    id: bar
    property real value: 0      // 0..1
    property bool working: true // the light runs while true
    property color color: theme.accent
    property color trackColor: "transparent"
    implicitHeight: 3

    Rectangle { anchors.fill: parent; radius: height / 2; color: bar.trackColor }
    Rectangle {
        id: fill
        height: parent.height
        width: Math.max(0, Math.min(1, bar.value)) * bar.width
        radius: height / 2
        color: bar.color
        clip: true
        Behavior on width { enabled: activities.animations; NumberAnimation { duration: 480; easing.type: Easing.OutCubic } }
        Rectangle {
            id: shine
            width: Math.min(56, fill.width)
            height: parent.height
            visible: bar.working && activities.animations && fill.width > 6
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.5) }
                GradientStop { position: 1; color: "transparent" }
            }
            SequentialAnimation on x {
                running: shine.visible
                loops: Animation.Infinite
                NumberAnimation { from: -shine.width; to: fill.width; duration: 1200; easing.type: Easing.InOutSine }
                PauseAnimation { duration: 400 }
            }
        }
    }
}
