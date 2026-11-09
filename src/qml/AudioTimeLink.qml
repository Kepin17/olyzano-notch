import QtQuick

// "00:12": plays the recording from there (and shows the transcript following it).
Text {
    id: tl
    required property var page
    property real seconds: -1
    visible: seconds >= 0
    text: seconds >= 0 ? page.clock(seconds) : ""
    color: theme.accent
    font.pixelSize: 11
    font.family: "monospace"
    HoverHandler { id: hh; cursorShape: Qt.PointingHandCursor }
    opacity: hh.hovered ? 1 : 0.8
    TapHandler { onTapped: page.playAt(tl.seconds) }
}
