import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes

// Now-playing card (MPRIS), modelled on the iOS Dynamic Island player.
Rectangle {
    id: card
    radius: theme.radiusLg
    color: theme.card
    implicitHeight: content.implicitHeight + 28

    readonly property color tint: theme.media
    property real position: 0 // ms, interpolated between D-Bus reads
    property bool scrubbing: false
    // The user did something with the card: keep it open (Main stops the auto-hide).
    signal interacted()

    function sync() {
        if (!scrubbing)
            position = media.positionMs()
    }
    function fmt(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
        const pad = n => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(sec) : m + ":" + pad(sec)
    }

    Timer {
        interval: 250
        repeat: true
        running: card.visible && media.playing && !card.scrubbing
        onTriggered: card.position = Math.min(media.lengthMs, card.position + 250)
    }
    Timer {
        interval: 3000
        repeat: true
        running: card.visible
        triggeredOnStart: true
        onTriggered: card.sync()
    }
    Connections {
        target: media
        function onChanged() { card.sync() }
    }
    onVisibleChanged: if (visible) sync()

    // Solid triangle pointing right; mirror for "previous".
    component Triangle: Shape {
        id: tri
        property color color: theme.text
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: tri.color
            strokeWidth: -1
            startX: 0
            startY: 0
            PathLine { x: tri.width; y: tri.height / 2 }
            PathLine { x: 0; y: tri.height }
            PathLine { x: 0; y: 0 }
        }
    }

    component Skip: Row {
        property bool back: false
        spacing: -2
        transform: Scale { xScale: back ? -1 : 1; origin.x: 12 }
        Triangle { width: 12; height: 14 }
        Triangle { width: 12; height: 14 }
    }

    component RoundButton: AbstractButton {
        id: rb
        implicitWidth: 40
        implicitHeight: 40
        opacity: enabled ? 1 : 0.35
        background: Rectangle {
            radius: width / 2
            color: rb.down ? theme.surfaceHover : rb.hovered ? theme.surface : "transparent"
            Behavior on color { ColorAnimation { duration: 100 } }
        }
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter // centred when the card is taller than its content
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                id: cover
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: theme.radiusMd
                color: theme.surface
                Image {
                    id: art
                    anchors.fill: parent
                    visible: false
                    source: media.artUrl
                    cache: false // browsers reuse one temp file for every track's cover
                    sourceSize: Qt.size(112, 112)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                Item {
                    id: coverMask
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true
                    Rectangle { anchors.fill: parent; radius: cover.radius }
                }
                MultiEffect {
                    anchors.fill: parent
                    visible: art.status === Image.Ready
                    source: art
                    maskEnabled: true
                    maskSource: coverMask
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1
                }
                Image {
                    anchors.centerIn: parent
                    width: 32
                    height: 32
                    visible: art.status !== Image.Ready
                    source: "image://mime/audio-x-generic"
                    sourceSize: Qt.size(64, 64)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: media.title || media.identity
                    color: theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: media.artist || media.identity
                    color: theme.textDim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            Equalizer {
                Layout.alignment: Qt.AlignVCenter
                color: card.tint
                maxHeight: 20
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: media.lengthMs > 0 // live streams have no length
            spacing: 10
            Text {
                text: card.fmt(card.position)
                color: theme.textDim
                font.pixelSize: 11
                font.features: { "tnum": 1 }
            }
            Item {
                id: bar
                Layout.fillWidth: true
                implicitHeight: 16
                readonly property real ratio: media.lengthMs > 0 ? Math.min(1, card.position / media.lengthMs) : 0
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: seekArea.containsMouse || card.scrubbing ? 7 : 5
                    radius: height / 2
                    color: theme.surfacePressed
                    Behavior on height { NumberAnimation { duration: 100 } }
                    Rectangle {
                        width: parent.width * bar.ratio
                        height: parent.height
                        radius: height / 2
                        color: card.tint
                    }
                }
                MouseArea {
                    id: seekArea
                    anchors.fill: parent
                    enabled: media.canSeek
                    hoverEnabled: true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    function scrubTo(x) { card.position = Math.max(0, Math.min(1, x / width)) * media.lengthMs }
                    onPressed: (m) => { card.scrubbing = true; scrubTo(m.x) }
                    onPositionChanged: (m) => { if (pressed) scrubTo(m.x) }
                    onReleased: {
                        media.seek(card.position)
                        card.scrubbing = false
                        card.interacted()
                    }
                }
            }
            Text {
                text: "-" + card.fmt(media.lengthMs - card.position)
                color: theme.textDim
                font.pixelSize: 11
                font.features: { "tnum": 1 }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            RoundButton {
                Accessible.name: "Open " + media.identity
                onClicked: media.raise()
                contentItem: Text {
                    text: "↗"
                    color: theme.textDim
                    font.pixelSize: 17
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Item { Layout.fillWidth: true }
            RoundButton {
                Accessible.name: "Previous"
                enabled: media.canPrevious
                onClicked: media.previous()
                contentItem: Item { Skip { anchors.centerIn: parent; back: true } }
            }
            RoundButton {
                implicitWidth: 52
                implicitHeight: 52
                Accessible.name: media.playing ? "Pause" : "Play"
                onClicked: media.playPause()
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        visible: media.playing
                        Rectangle { width: 6; height: 22; radius: 2; color: theme.text }
                        Rectangle { width: 6; height: 22; radius: 2; color: theme.text }
                    }
                    Triangle {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 2
                        visible: !media.playing
                        width: 20
                        height: 22
                    }
                }
            }
            RoundButton {
                Accessible.name: "Next"
                enabled: media.canNext
                onClicked: media.next()
                contentItem: Item { Skip { anchors.centerIn: parent } }
            }
            Item { Layout.fillWidth: true }
            RoundButton {
                Accessible.name: "Switch player"
                enabled: media.playerCount > 1
                opacity: enabled ? 1 : 0 // only meaningful with several players
                onClicked: media.cyclePlayer()
                contentItem: Text {
                    text: "⇄"
                    color: theme.textDim
                    font.pixelSize: 17
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            // Karaoke: your voice through the speakers or headphones, with the music.
            RoundButton {
                id: mic
                visible: karaoke.supported
                Accessible.name: karaoke.active ? "Stop karaoke" : "Karaoke: sing along"
                onClicked: karaoke.toggle()
                background: Rectangle {
                    radius: width / 2
                    color: karaoke.active ? theme.accentSoft : mic.down ? theme.surfaceHover : mic.hovered ? theme.surface : "transparent"
                }
                contentItem: Item {
                    LineIcon {
                        anchors.centerIn: parent
                        glyph: "mic"
                        sourceSize: Qt.size(18, 18)
                        color: karaoke.active ? theme.accent : theme.textDim
                    }
                }
            }
        }
        Text {
            visible: karaoke.active || karaoke.error !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: karaoke.error !== "" ? theme.danger : theme.textDim
            font.pixelSize: 11
            text: karaoke.error !== "" ? karaoke.error : "Your voice plays with the music. Use headphones, or a speaker will echo."
        }
    }
}
