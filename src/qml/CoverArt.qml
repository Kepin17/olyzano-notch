import QtQuick
import QtQuick.Effects

// A small album cover with rounded corners; a music glyph while there is none.
Item {
    id: c
    property url source
    property real radius: theme.radiusSm
    readonly property bool ready: img.status === Image.Ready
    readonly property bool software: memtune.running // the software renderer cannot draw the mask (see CameraView)
    clip: c.software // the direct cover below is square; keep it inside the rounded backdrop

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
        visible: false // its texture only feeds the MultiEffect below
        source: coverCache.cover(c.source) // content-addressed: one decode shared by every cover on screen
        cache: false // the cover cache does the sharing, keyed by content, not by a reused temp file
        sourceSize: Qt.size(Math.max(64, c.width * 2), Math.max(64, c.height * 2))
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
    Image { // software mode: no shader for the rounded mask, so the cover is drawn straight (square corners, still the cover)
        anchors.fill: parent
        visible: c.software && c.ready
        source: coverCache.cover(c.source)
        cache: false
        sourceSize: img.sourceSize
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
        visible: c.ready && !c.software
        source: img
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1
    }
}
