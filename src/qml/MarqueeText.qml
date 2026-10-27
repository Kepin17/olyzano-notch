import QtQuick

// One line of text that glides sideways when it is longer than the room: it waits, scrolls to the end, waits, and starts
// again (only while `running` and only when it overflows). A change of text fades in. The right edge fades into
// `fadeColor` while there is more to read.
Item {
    id: m
    property string text
    property color color: "white"
    property color fadeColor: theme.island
    property int pixelSize: 13
    property int weight: Font.Normal
    property bool running: true
    readonly property real overflow: Math.max(0, label.implicitWidth - width)
    implicitHeight: label.implicitHeight
    clip: true

    Text {
        id: label
        text: m.text
        color: m.color
        font.pixelSize: m.pixelSize
        font.weight: m.weight
        wrapMode: Text.NoWrap
        elide: Text.ElideNone
        x: 0
        onTextChanged: { x = 0; if (activities.animations) fadeIn.restart() }
    }
    NumberAnimation { id: fadeIn; target: label; property: "opacity"; from: 0.15; to: 1; duration: 180 }

    SequentialAnimation {
        running: m.running && m.visible && m.overflow > 1 && activities.animations // off: the long line is cut with its fade instead of gliding
        loops: Animation.Infinite
        onRunningChanged: if (!running) label.x = 0
        PauseAnimation { duration: 1600 }
        NumberAnimation { target: label; property: "x"; to: -m.overflow; duration: Math.max(600, m.overflow * 32); easing.type: Easing.InOutSine }
        PauseAnimation { duration: 1000 }
        PropertyAction { target: label; property: "x"; value: 0 }
    }
    Rectangle { // more to read on the right
        anchors.right: parent.right
        width: 16
        height: parent.height
        visible: m.overflow > 1 && label.x > -m.overflow + 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: m.fadeColor }
        }
    }
}
