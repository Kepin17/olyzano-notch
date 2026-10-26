import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes

// Now-playing card (MPRIS / Windows media sessions): the cover big, on a blurred copy of itself, with the colour of
// the cover on the progress, the spectrum and the play button's glow. A new song fades the background and slides the
// text in. Half width (next to the lyrics) it keeps the same look with a smaller cover.
Rectangle {
    id: card
    radius: theme.radiusLg
    color: theme.card
    clip: true
    implicitHeight: content.implicitHeight + 32

    readonly property bool hasArt: art.status === Image.Ready
    readonly property bool compact: width < 400
    readonly property color tint: cover.color
    readonly property color fg: hasArt ? "#ffffff" : theme.text
    readonly property color fgDim: hasArt ? Qt.rgba(1, 1, 1, 0.72) : theme.textDim
    readonly property color fgFaint: hasArt ? Qt.rgba(1, 1, 1, 0.5) : theme.textFaint
    property real position: 0 // ms, interpolated between D-Bus reads
    property bool scrubbing: false
    // The user did something with the card: keep it open (Main stops the auto-hide).
    signal interacted()

    function sync() {
        if (!scrubbing)
            position = media.positionMs()
    }
    function jump(ms) { // skip within the song
        position = Math.max(0, Math.min(media.lengthMs, position + ms))
        media.seek(position)
        interacted()
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
        function onChanged() {
            card.sync()
            if (media.title !== card.lastTitle || media.artUrl !== card.lastArt) {
                card.lastTitle = media.title
                card.lastArt = media.artUrl
                change.restart()
            }
        }
    }
    onVisibleChanged: if (visible) sync()

    // ---- a new song: the background fades up, the cover pops, the text slides in ----
    property string lastTitle: media.title
    property url lastArt: media.artUrl
    property real fade: 1        // the blurred background's opacity
    property real pop: 1         // the cover's scale
    property real slide: 1       // 0..1: the text has arrived
    ParallelAnimation {
        id: change
        NumberAnimation { target: card; property: "fade"; from: 0.1; to: 1; duration: 420; easing.type: Easing.OutCubic }
        NumberAnimation { target: card; property: "pop"; from: 0.9; to: 1; duration: 380; easing.type: Easing.OutBack }
        NumberAnimation { target: card; property: "slide"; from: 0; to: 1; duration: 320; easing.type: Easing.OutCubic }
    }

    // ---- the background: the cover, blurred and darkened ----
    Image { // the picture itself (the cover and the background both look at it)
        id: art
        visible: false
        source: media.artUrl
        cache: false // browsers reuse one temp file for every track's cover
        sourceSize: Qt.size(200, 200)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
    ArtColor {
        id: cover
        source: media.artUrl
        fallback: theme.media
    }
    Item { // the blurred cover, drawn larger than the card (a blur fades at its edges, and the card clips them away)
        id: backdrop
        anchors.fill: parent
        anchors.margins: -70
        visible: card.hasArt
        opacity: card.fade
        Image {
            id: bgArt
            anchors.fill: parent
            visible: false
            source: media.artUrl
            cache: false
            sourceSize: Qt.size(160, 160)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        MultiEffect {
            anchors.fill: parent
            source: bgArt
            autoPaddingEnabled: false
            blurEnabled: true
            blurMax: 48
            blur: 1.0
            brightness: -0.32
            saturation: -0.05
        }
    }
    Rectangle { // keeps the text readable on any cover
        anchors.fill: parent
        visible: card.hasArt
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.18) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.5) }
        }
    }
    Rectangle { // no cover: a faint glow of the accent from the corner
        anchors.fill: parent
        visible: !card.hasArt
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.alpha(card.tint, 0.16) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Solid triangle pointing right; mirror for "previous".
    component Triangle: Shape {
        id: tri
        property color color: card.fg
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
            color: rb.down ? Qt.rgba(1, 1, 1, 0.22) : rb.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
            Behavior on color { ColorAnimation { duration: 100 } }
        }
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter // centred when the card is taller than its content
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Item { // the cover
                id: coverBox
                Layout.preferredWidth: card.compact ? 76 : 96
                Layout.preferredHeight: card.compact ? 76 : 96
                scale: card.pop
                Rectangle { // a soft shadow under it
                    x: 0; y: 5; width: parent.width; height: parent.height
                    radius: 14
                    color: "#000000"
                    opacity: 0.28
                }
                Item {
                    id: face
                    anchors.fill: parent
                    // it breathes a little while the music plays
                    SequentialAnimation on scale {
                        running: media.playing && card.visible
                        loops: Animation.Infinite
                        NumberAnimation { to: 1.025; duration: 1800; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 1800; easing.type: Easing.InOutSine }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 14
                        color: theme.surface
                    }
                    Item {
                        id: coverMask
                        anchors.fill: parent
                        visible: false
                        layer.enabled: true
                        Rectangle { anchors.fill: parent; radius: 14 }
                    }
                    MultiEffect {
                        anchors.fill: parent
                        visible: card.hasArt
                        source: art
                        maskEnabled: true
                        maskSource: coverMask
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1
                    }
                    Image {
                        anchors.centerIn: parent
                        width: 36
                        height: 36
                        visible: !card.hasArt
                        source: "image://mime/audio-x-generic"
                        sourceSize: Qt.size(72, 72)
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                opacity: card.slide
                transform: Translate { y: (card.slide - 1) * 12 } // the new song comes down from above
                Text {
                    Layout.fillWidth: true
                    text: media.title || media.identity
                    color: card.fg
                    font.pixelSize: card.compact ? 15 : 17
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    maximumLineCount: card.compact ? 1 : 2
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: media.artist || media.identity
                    color: card.fgDim
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: media.album !== ""
                    text: media.album
                    color: card.fgFaint
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            Equalizer {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 4
                color: card.tint
                maxHeight: 20
            }
        }

        // progress: a thin line that thickens under the pointer, with a thumb only then
        ColumnLayout {
            Layout.fillWidth: true
            visible: media.lengthMs > 0 // live streams have no length
            spacing: 0
            Item {
                id: bar
                Layout.fillWidth: true
                implicitHeight: 18
                readonly property real ratio: media.lengthMs > 0 ? Math.min(1, card.position / media.lengthMs) : 0
                readonly property bool active: seekArea.containsMouse || card.scrubbing
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: bar.active ? 6 : 4
                    radius: height / 2
                    color: card.hasArt ? Qt.rgba(1, 1, 1, 0.22) : theme.surfacePressed
                    Behavior on height { NumberAnimation { duration: 100 } }
                    Rectangle {
                        width: parent.width * bar.ratio
                        height: parent.height
                        radius: height / 2
                        color: card.tint
                    }
                }
                Rectangle { // the thumb
                    x: bar.width * bar.ratio - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 13; height: 13; radius: 6.5
                    color: "#ffffff"
                    opacity: bar.active ? 1 : 0
                    scale: bar.active ? 1 : 0.4
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                    Behavior on scale { NumberAnimation { duration: 120 } }
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
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: card.fmt(card.position)
                    color: card.fgDim
                    font.pixelSize: 11
                    font.features: { "tnum": 1 }
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "-" + card.fmt(media.lengthMs - card.position)
                    color: card.fgDim
                    font.pixelSize: 11
                    font.features: { "tnum": 1 }
                }
            }
        }

        // transport: previous, a big play / pause, next
        RowLayout {
            Layout.fillWidth: true
            spacing: card.compact ? 8 : 14
            Item { Layout.fillWidth: true }
            RoundButton { // back 10 seconds
                implicitWidth: 40
                implicitHeight: 40
                Accessible.name: "Back 10 seconds"
                enabled: media.canSeek && media.lengthMs > 0
                onClicked: card.jump(-10000)
                contentItem: Text {
                    text: "−10"
                    color: card.fgDim
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            RoundButton {
                implicitWidth: 46
                implicitHeight: 46
                Accessible.name: "Previous"
                enabled: media.canPrevious
                onClicked: media.previous()
                contentItem: Item { Skip { anchors.centerIn: parent; back: true } }
            }
            AbstractButton {
                id: play
                implicitWidth: 60
                implicitHeight: 60
                Accessible.name: media.playing ? "Pause" : "Play"
                onClicked: media.playPause()
                background: Item {
                    Rectangle { // a soft glow of the cover's colour
                        anchors.centerIn: parent
                        width: parent.width + 14; height: width; radius: width / 2
                        color: card.tint
                        opacity: media.playing ? 0.34 : 0.14
                        Behavior on opacity { NumberAnimation { duration: 200 } }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: play.down ? "#d8d8d8" : "#ffffff"
                        scale: play.down ? 0.95 : play.hovered ? 1.04 : 1
                        Behavior on scale { NumberAnimation { duration: 100 } }
                    }
                }
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        visible: media.playing
                        Rectangle { width: 6; height: 22; radius: 2; color: "#16161a" }
                        Rectangle { width: 6; height: 22; radius: 2; color: "#16161a" }
                    }
                    Triangle {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 2
                        visible: !media.playing
                        width: 20
                        height: 22
                        color: "#16161a"
                    }
                }
            }
            RoundButton {
                implicitWidth: 46
                implicitHeight: 46
                Accessible.name: "Next"
                enabled: media.canNext
                onClicked: media.next()
                contentItem: Item { Skip { anchors.centerIn: parent } }
            }
            RoundButton { // forward 10 seconds
                implicitWidth: 40
                implicitHeight: 40
                Accessible.name: "Forward 10 seconds"
                enabled: media.canSeek && media.lengthMs > 0
                onClicked: card.jump(10000)
                contentItem: Text {
                    text: "+10"
                    color: card.fgDim
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Item { Layout.fillWidth: true }
        }

        // the small things, quiet, in one row
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Item { Layout.fillWidth: true }
            RoundButton {
                implicitWidth: 32
                implicitHeight: 32
                Accessible.name: "Open " + media.identity
                onClicked: media.raise()
                contentItem: Text {
                    text: "↗"
                    color: card.fgDim
                    font.pixelSize: 16
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            RoundButton {
                visible: media.playerCount > 1 // only meaningful with several players
                implicitWidth: 32
                implicitHeight: 32
                Accessible.name: "Switch player"
                onClicked: media.cyclePlayer()
                contentItem: Text {
                    text: "⇄"
                    color: card.fgDim
                    font.pixelSize: 16
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            // Karaoke: your voice through the speakers or headphones, with the music.
            RoundButton {
                id: mic
                visible: karaoke.supported
                implicitWidth: 32
                implicitHeight: 32
                Accessible.name: karaoke.active ? "Stop karaoke" : "Karaoke: sing along"
                onClicked: karaoke.toggle()
                background: Rectangle {
                    radius: width / 2
                    color: karaoke.active ? Qt.alpha(card.tint, 0.3) : mic.down ? Qt.rgba(1, 1, 1, 0.22) : mic.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                }
                contentItem: Item {
                    LineIcon {
                        anchors.centerIn: parent
                        glyph: "mic"
                        sourceSize: Qt.size(16, 16)
                        color: karaoke.active ? card.tint : card.fgDim
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
        Text {
            visible: karaoke.active || karaoke.error !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: karaoke.error !== "" ? theme.danger : card.fgDim
            font.pixelSize: 11
            text: karaoke.error !== "" ? karaoke.error : "Your voice plays with the music. Use headphones, or a speaker will echo."
        }
    }
}
