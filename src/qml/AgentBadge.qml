import QtQuick

// A small pill: a file mentioned with @, SHELL on a command the person ran, an exit status, a /command in the list.
Rectangle {
    id: badge
    property string text: ""
    property string glyph: ""        // a line icon from icons/
    property string tone: "neutral"  // neutral | accent | ok | bad | warn
    property bool closable: false
    property bool mono: false
    signal closed()
    readonly property color fg: tone === "accent" ? theme.accentText : tone === "ok" ? "#30d158" : tone === "bad" ? "#ff453a"
                                : tone === "warn" ? "#ff9f0a" : theme.textDim
    implicitHeight: 20
    implicitWidth: row.implicitWidth + 14
    radius: 10
    color: Qt.alpha(fg, tone === "neutral" ? 0.12 : 0.16)
    border.width: 1
    border.color: Qt.alpha(fg, tone === "neutral" ? 0.25 : 0.4)
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4
        LineIcon { visible: badge.glyph !== ""; anchors.verticalCenter: parent.verticalCenter; glyph: badge.glyph; sourceSize: Qt.size(11, 11); color: badge.fg }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: badge.text
            color: badge.fg
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.family: badge.mono ? "monospace" : Qt.application.font.family
        }
        Item {
            visible: badge.closable
            width: 12
            height: 12
            anchors.verticalCenter: parent.verticalCenter
            Text { anchors.centerIn: parent; text: "×"; color: closeArea.containsMouse ? theme.text : badge.fg; font.pixelSize: 13 }
            MouseArea { id: closeArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: badge.closed() }
        }
    }
}
