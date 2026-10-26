import QtQuick

// Widget mode (beta): the thin line at the top edge that still opens the panel at the top centre, while the card
// itself floats somewhere else. A window of its own, created for each screen; only shown in widget mode.
Window {
    id: strip
    width: 300
    height: 4
    visible: false
    color: "transparent"
    title: "Olyzano Notch edge"

    Rectangle { // a hint where it is
        anchors.horizontalCenter: parent.horizontalCenter
        width: 44
        height: 3
        radius: 1.5
        color: "#000000"
        border.width: 0.5
        border.color: Qt.rgba(1, 1, 1, 0.22)
        opacity: edgeHover.hovered ? 1 : 0.85
    }
    HoverHandler {
        id: edgeHover
        onHoveredChanged: if (hovered) dwell.restart(); else dwell.stop()
    }
    Timer { // the pointer has to rest there a moment, so flinging it up to a tab does not pop the panel
        id: dwell
        interval: 160
        onTriggered: desktop.openDocked(strip)
    }
}
