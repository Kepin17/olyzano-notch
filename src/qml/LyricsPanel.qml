import QtQuick
import QtQuick.Controls.Basic

// The right half of the Music page, like Spotify's lyrics view: only the words. The line being sung is big and
// bright, the ones around it dim, and the list follows the song by itself. A tap on a line jumps there.
Rectangle {
    id: panel
    radius: theme.radiusLg
    color: theme.card
    clip: true

    readonly property string state: lyrics.state
    readonly property bool synced: state === "synced"

    // Karaoke lives here: your voice through the speakers or headphones, with the music.
    RoundButton {
        id: sing
        z: 2
        visible: karaoke.supported
        anchors { top: parent.top; right: parent.right; margins: 8 }
        implicitWidth: 28
        implicitHeight: 28
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        Accessible.name: karaoke.active ? "Stop karaoke" : "Karaoke: sing along"
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.text: karaoke.active ? "Stop karaoke" : "Karaoke: your voice plays with the music"
        onClicked: karaoke.toggle()
        background: Rectangle {
            radius: width / 2
            color: karaoke.active ? Qt.alpha(theme.accent, 0.3) : sing.down ? Qt.rgba(1, 1, 1, 0.22) : sing.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
        }
        contentItem: Item {
            LineIcon {
                anchors.centerIn: parent
                glyph: "mic"
                sourceSize: Qt.size(15, 15)
                color: karaoke.active ? theme.accent : theme.textDim
            }
        }
    }

    // Lines with timing: the current one kept in the middle.
    ListView {
        id: view
        visible: panel.synced
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        model: panel.synced ? lyrics.lines : []
        currentIndex: Math.max(0, lyrics.currentIndex)
        interactive: false
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: height / 2 - 24
        preferredHighlightEnd: height / 2 + 24
        highlightMoveDuration: 380
        highlightMoveVelocity: -1
        spacing: 10
        header: Item { height: 8 }
        footer: Item { height: 8 }
        delegate: Item {
            id: row
            required property int index
            required property var modelData
            readonly property bool current: index === lyrics.currentIndex
            readonly property int distance: Math.abs(index - lyrics.currentIndex)
            width: view.width
            height: lineText.implicitHeight
            Text {
                id: lineText
                width: parent.width
                text: row.modelData.text !== "" ? row.modelData.text : "♪"
                wrapMode: Text.Wrap
                color: row.current ? theme.text : theme.textDim
                opacity: row.current ? 1 : Math.max(0.25, 0.65 - row.distance * 0.15)
                font.pixelSize: karaoke.active ? (row.current ? 25 : 18) : (row.current ? 19 : 15) // bigger to sing from
                font.weight: row.current ? Font.Bold : Font.Medium
                Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 200 } }
                Behavior on font.pixelSize { enabled: activities.animations; NumberAnimation { duration: 200 } }
            }
            MouseArea {
                anchors.fill: parent
                enabled: media.canSeek
                cursorShape: media.canSeek ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: media.seek(row.modelData.ms)
            }
        }
    }

    // Words without timing: just readable text.
    Flickable {
        visible: panel.state === "plain"
        anchors.fill: parent
        anchors.margins: 18
        contentHeight: plainText.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Text {
            id: plainText
            width: parent.width
            text: lyrics.plain
            wrapMode: Text.Wrap
            color: theme.textSoft
            font.pixelSize: 14
            lineHeight: 1.25
        }
        ScrollBar.vertical: ScrollBar { }
    }

    Text {
        visible: !panel.synced && panel.state !== "plain"
        anchors.centerIn: parent
        width: parent.width - 36
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: theme.textDim
        font.pixelSize: 13
        text: panel.state === "loading" ? "Looking for the lyrics…"
            : panel.state === "error" ? "Couldn't reach the lyrics service"
            : panel.state === "off" ? "Lyrics are off"
            : "No lyrics found for this song"
    }

    // Fades at the edges, so lines slide in and out instead of being cut.
    Rectangle {
        visible: panel.synced
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 36
        gradient: Gradient {
            GradientStop { position: 0; color: theme.card }
            GradientStop { position: 1; color: "transparent" }
        }
    }
    Rectangle {
        visible: panel.synced
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 36
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: theme.card }
        }
    }
}
