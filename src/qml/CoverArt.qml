import QtQuick
import QtQuick.Effects

// A small album cover with rounded corners; a music glyph while there is none.
Item {
    id: c
    property url source
    property real radius: 8
    readonly property bool ready: img.status === Image.Ready

    Rectangle {
        anchors.fill: parent
        radius: c.radius
        color: Qt.rgba(1, 1, 1, 0.1)
    }
    LineIcon {
        anchors.centerIn: parent
        visible: !c.ready
        glyph: "music"
        sourceSize: Qt.size(c.width * 0.5, c.width * 0.5)
        color: Qt.rgba(1, 1, 1, 0.6)
    }
    Image {
        id: img
        visible: false
        source: c.source
        cache: false // browsers reuse one temp file for every track's cover
        sourceSize: Qt.size(Math.max(64, c.width * 2), Math.max(64, c.height * 2))
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
    Item {
        id: mask
        anchors.fill: parent
        visible: false
        layer.enabled: true
        Rectangle { anchors.fill: parent; radius: c.radius }
    }
    MultiEffect {
        anchors.fill: parent
        visible: c.ready
        source: img
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1
    }
}
