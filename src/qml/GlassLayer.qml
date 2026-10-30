import QtQuick

// The glass look over a surface (only while Glass is on): a soft sheen falling from the top (HyperOS), a bright rim over the
// top edge and corners that fades down the sides (macOS), on a lifted, lighter frost (One UI, see Theme.island). Put it
// over a surface's fill with the same radius; it never takes the pointer.
Item {
    id: gl
    property real radius: 16
    property bool rim: true // false where the edge is fused to the screen
    visible: theme.glass
    enabled: false
    Rectangle { // the sheen
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, gl.radius - 1)
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(1, 1, 1, theme.glassSheen) }
            GradientStop { position: 0.55; color: Qt.rgba(1, 1, 1, 0) }
        }
    }
    Item { // only the top part of the rim is bright; the rest of the edge stays the surface's own outline
        visible: gl.rim
        width: parent.width
        height: Math.min(gl.height / 2, gl.radius * 1.3 + 2)
        clip: true
        Rectangle {
            width: gl.width
            height: gl.height
            radius: gl.radius
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, theme.glassRim)
        }
    }
}
