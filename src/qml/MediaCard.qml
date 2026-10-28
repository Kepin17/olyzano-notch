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
    implicitHeight: content.implicitHeight + 20

    readonly property bool hasArt: art.status === Image.Ready
    readonly property bool compact: width < 400
    readonly property color tint: cover.color
    readonly property color fg: hasArt ? "#ffffff" : theme.text
    readonly property color fgDim: hasArt ? Qt.rgba(1, 1, 1, 0.72) : theme.textDim
    readonly property color fgFaint: hasArt ? Qt.rgba(1, 1, 1, 0.5) : theme.textFaint
    property real position: 0 // ms, interpolated between D-Bus reads
    property bool scrubbing: false
    property bool volumeOpen: false
    readonly property real shown: media.lengthMs > 0 ? Math.max(0, Math.min(media.lengthMs, position)) : Math.max(0, position) // never past the end
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
        onTriggered: card.position = media.lengthMs > 0 ? Math.min(media.lengthMs, card.position + 250) : card.position + 250
    }
    Timer { id: settle; interval: 800; onTriggered: card.sync() }
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
            // One animation per song. Windows reports the title first and the cover a little later (3 to 11 events per song), so
            // only a new title starts it; a cover that arrives afterwards just replaces the picture.
            if (media.title !== card.lastTitle) {
                const firstSong = card.lastTitle === "" // the card was just built: nothing to change from
                card.lastTitle = media.title
                card.lastArt = media.artUrl
                if (!firstSong && activities.animations)
                    change.restart()
                if (!card.scrubbing) card.position = 0 // the player still reports the old song's position for a moment: start at 0 and ask again
                settle.restart()
                return
            }
            card.lastArt = media.artUrl
            card.sync()
        }
    }
    onVisibleChanged: { if (visible) sync(); else volumeOpen = false }

    // ---- a new song: the background fades up, the cover pops, the text slides in ----
    // The song shown. Plain values on purpose: as bindings (`: media.title`) they were already the new song when the handler
    // above compared them, so the animation below never started.
    property string lastTitle: ""
    property url lastArt
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
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        id: rb
        implicitWidth: 40
        implicitHeight: 40
        opacity: enabled ? 1 : 0.35
        background: Rectangle {
            radius: width / 2
            color: rb.down ? Qt.rgba(1, 1, 1, 0.22) : rb.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
            Behavior on color { enabled: activities.animations; ColorAnimation { duration: 100 } }
        }
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter // centred when the card is taller than its content
        anchors.margins: 10
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Item { // the cover
                id: coverBox
                Layout.preferredWidth: card.compact ? 60 : 72
                Layout.preferredHeight: card.compact ? 60 : 72
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
                    // The cover used to breathe (1.0 <-> 1.025, for ever) while the music played, which read as it shrinking back and
                    // forth. It now moves once, when the song changes (the pop, see `change`), and is still the rest of the time.
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
                // The card keeps the same height for every song: room for a two-line title and for the album line is always
                // reserved, so the panel does not resize as the title, album and length arrive one after another.
                FontMetrics { id: titleFont; font: titleText.font }
                FontMetrics { id: albumFont; font: albumText.font }
                Text {
                    id: titleText
                    Layout.fillWidth: true
                    Layout.minimumHeight: titleFont.lineSpacing * (card.compact ? 1 : 2)
                    verticalAlignment: Text.AlignTop
                    textFormat: Text.PlainText // a name can look like markup
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
                    id: albumText
                    Layout.fillWidth: true
                    Layout.minimumHeight: albumFont.lineSpacing // reserved even without an album
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
            // live streams have no length; the room stays, so the card does not grow when a song's length arrives
            opacity: media.lengthMs > 0 ? 1 : 0
            enabled: media.lengthMs > 0
            spacing: 0
            Item {
                id: bar
                Layout.fillWidth: true
                implicitHeight: 14
                readonly property real ratio: media.lengthMs > 0 ? card.shown / media.lengthMs : 0
                readonly property bool active: seekArea.containsMouse || card.scrubbing
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: bar.active ? 6 : 4
                    radius: height / 2
                    color: card.hasArt ? Qt.rgba(1, 1, 1, 0.22) : theme.surfacePressed
                    Behavior on height { enabled: activities.animations; NumberAnimation { duration: 100 } }
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
                    Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 120 } }
                    Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 120 } }
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
                    text: card.fmt(card.shown)
                    color: card.fgDim
                    font.pixelSize: 11
                    font.features: { "tnum": 1 }
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "-" + card.fmt(media.lengthMs - card.shown)
                    color: card.fgDim
                    font.pixelSize: 11
                    font.features: { "tnum": 1 }
                }
            }
        }

        // transport: previous, a big play / pause, next
        RowLayout {
            Layout.fillWidth: true
            spacing: card.compact ? 4 : 10
            Item { // the volume: tap the speaker and a bar comes up (like a video player)
                id: volumeBox
                z: 20
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: 32
                RoundButton {
                    id: speaker
                    visible: quick.audioAvailable
                    anchors.centerIn: parent
                    implicitWidth: 30
                    implicitHeight: 30
                    Accessible.name: "Volume"
                    onClicked: { card.volumeOpen = !card.volumeOpen; quick.updateVolume(); volumeClose.restart(); card.interacted() }
                    background: Rectangle {
                        radius: width / 2
                        color: card.volumeOpen ? Qt.alpha(card.tint, 0.3) : speaker.down ? Qt.rgba(1, 1, 1, 0.22) : speaker.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                    }
                    contentItem: Item {
                        LineIcon {
                            anchors.centerIn: parent
                            glyph: quick.muted || quick.volume === 0 ? "volume-off" : "volume"
                            sourceSize: Qt.size(16, 16)
                            color: card.volumeOpen ? card.tint : card.fgDim
                        }
                    }
                    WheelHandler { onWheel: (e) => { quick.setVolume(quick.volume + (e.angleDelta.y > 0 ? 5 : -5)); volumeClose.restart(); card.interacted() } }
                }
                Rectangle { // the bar that comes up above the speaker
                    id: volumePop
                    width: 40
                    height: 142
                    radius: 20
                    x: (volumeBox.width - width) / 2
                    y: -height - 6
                    visible: opacity > 0
                    opacity: card.volumeOpen ? 1 : 0
                    scale: card.volumeOpen ? 1 : 0.9
                    transformOrigin: Item.Bottom
                    color: Qt.rgba(0.05, 0.05, 0.06, 0.94)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.12)
                    Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 140 } }
                    Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 9
                        text: quick.muted ? "0" : quick.volume
                        color: "#ffffff"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.features: { "tnum": 1 }
                    }
                    Item { // the track
                        id: track
                        x: (parent.width - width) / 2
                        y: 28
                        width: 20
                        height: parent.height - 28 - 38
                        readonly property real ratio: quick.muted ? 0 : Math.max(0, Math.min(1, quick.volume / 100))
                        Accessible.name: "Volume"
                        Accessible.role: Accessible.Slider
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 4
                            height: parent.height
                            radius: 2
                            color: Qt.rgba(1, 1, 1, 0.22)
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: parent.height * track.ratio
                                radius: 2
                                color: card.tint
                            }
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: parent.height * (1 - track.ratio) - height / 2
                            width: 12; height: 12; radius: 6
                            color: "#ffffff"
                        }
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.PointingHandCursor
                            function setTo(y) { quick.setVolume(Math.round(Math.max(0, Math.min(1, 1 - (y - 4) / track.height)) * 100)) }
                            onPressed: (m) => { setTo(m.y); volumeClose.restart() }
                            onPositionChanged: (m) => { if (pressed) { setTo(m.y); volumeClose.restart() } }
                            onReleased: card.interacted()
                            onWheel: (w) => { quick.setVolume(quick.volume + (w.angleDelta.y > 0 ? 5 : -5)); volumeClose.restart(); card.interacted() }
                        }
                    }
                    LineIcon { // mute
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height - height - 11
                        glyph: quick.muted || quick.volume === 0 ? "volume-off" : "volume"
                        sourceSize: Qt.size(15, 15)
                        color: quick.muted ? theme.danger : "#ffffff"
                        TapHandler { onTapped: { quick.toggleMute(); volumeClose.restart(); card.interacted() } }
                    }
                }
                Timer { id: volumeClose; interval: 3500; onTriggered: card.volumeOpen = false }
            }
            Item { Layout.fillWidth: true }
            RoundButton { // back 10 seconds
                implicitWidth: card.compact ? 28 : 34
                implicitHeight: 34
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
                implicitWidth: card.compact ? 34 : 40
                implicitHeight: card.compact ? 34 : 40
                Accessible.name: "Previous"
                enabled: media.canPrevious
                onClicked: media.previous()
                contentItem: Item { Skip { anchors.centerIn: parent; back: true } }
            }
            AbstractButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                id: play
                implicitWidth: card.compact ? 44 : 50
                implicitHeight: card.compact ? 44 : 50
                Accessible.name: media.playing ? "Pause" : "Play"
                onClicked: media.playPause()
                background: Item {
                    Rectangle { // a soft glow of the cover's colour
                        anchors.centerIn: parent
                        width: parent.width + 14; height: width; radius: width / 2
                        color: card.tint
                        opacity: media.playing ? 0.34 : 0.14
                        Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 200 } }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: play.down ? "#d8d8d8" : "#ffffff"
                        scale: play.down ? 0.95 : play.hovered ? 1.04 : 1
                        Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 100 } }
                    }
                }
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        visible: media.playing
                        Rectangle { width: 5; height: 18; radius: 2; color: "#16161a" }
                        Rectangle { width: 5; height: 18; radius: 2; color: "#16161a" }
                    }
                    Triangle {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 2
                        visible: !media.playing
                        width: 16
                        height: 18
                        color: "#16161a"
                    }
                }
            }
            RoundButton {
                implicitWidth: card.compact ? 34 : 40
                implicitHeight: card.compact ? 34 : 40
                Accessible.name: "Next"
                enabled: media.canNext
                onClicked: media.next()
                contentItem: Item { Skip { anchors.centerIn: parent } }
            }
            RoundButton { // forward 10 seconds
                implicitWidth: card.compact ? 28 : 34
                implicitHeight: 34
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
            Item { // the words: shows the lyrics beside the player (karaoke is a button on the lyrics panel)
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Layout.minimumWidth: 32
                Layout.alignment: Qt.AlignVCenter
                RoundButton {
                    id: words
                    anchors.centerIn: parent
                    implicitWidth: 30
                    implicitHeight: 30
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    Accessible.name: lyrics.enabled ? "Hide the lyrics" : "Show the lyrics"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: lyrics.enabled ? "Hide the lyrics" : "Show the lyrics"
                    onClicked: lyrics.enabled = !lyrics.enabled
                    background: Rectangle {
                        radius: width / 2
                        color: lyrics.enabled ? Qt.alpha(card.tint, 0.3) : words.down ? Qt.rgba(1, 1, 1, 0.22) : words.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                    }
                    contentItem: Item {
                        LineIcon {
                            anchors.centerIn: parent
                            glyph: "lyrics"
                            sourceSize: Qt.size(16, 16)
                            color: lyrics.enabled ? card.tint : card.fgDim
                        }
                    }
                }
            }
        }

        RowLayout { // the one small thing left: which player, when there are several
            visible: media.playerCount > 1
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            RoundButton {
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
