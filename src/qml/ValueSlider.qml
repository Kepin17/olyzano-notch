import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A slider with its name and its value written above it.
ColumnLayout {
    id: vs
    property string title
    property string valueText
    property alias from: s.from
    property alias to: s.to
    property alias value: s.value
    property alias stepSize: s.stepSize
    signal moved(real value)
    Layout.fillWidth: true
    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        Caption { Layout.fillWidth: true; text: vs.title }
        Text { text: vs.valueText; color: theme.textSoft; font.pixelSize: 11; font.features: { "tnum": 1 } }
    }
    Slider {
        id: s
        Layout.fillWidth: true
        implicitHeight: 24
        onMoved: vs.moved(value)
        Accessible.name: vs.title
        background: Rectangle {
            x: s.leftPadding
            y: s.topPadding + s.availableHeight / 2 - height / 2
            width: s.availableWidth
            height: 4
            radius: 2
            color: theme.surface
            Rectangle { width: s.visualPosition * parent.width; height: parent.height; radius: 2; color: theme.accent }
        }
        handle: Rectangle {
            x: s.leftPadding + s.visualPosition * (s.availableWidth - width)
            y: s.topPadding + s.availableHeight / 2 - height / 2
            width: 18
            height: 18
            radius: 9
            color: theme.dark ? "#ffffff" : theme.accent
            scale: s.pressed ? 1.12 : 1
            Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 100 } }
        }
    }
}
