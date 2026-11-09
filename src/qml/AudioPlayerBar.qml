import QtQuick
import QtQuick.Layouts

// Play / pause, the waveform (click to jump, hover for the time), the time, and the speed in one button.
RowLayout {
    id: bar
    required property var page
    readonly property var item: page.player.item
    spacing: 10
    implicitHeight: 34

    Rectangle { // play
        Layout.preferredWidth: 32
        Layout.preferredHeight: 32
        radius: 16
        color: playHover.hovered ? Qt.lighter(theme.accent, 1.1) : theme.accent
        HoverHandler { id: playHover; cursorShape: Qt.PointingHandCursor }
        LineIcon { anchors.centerIn: parent; glyph: bar.item && bar.item.playing ? "pause" : "play"; sourceSize: Qt.size(14, 14); color: theme.onColor(theme.accent) }
        TapHandler { onTapped: { if (bar.item) { page.follow(); bar.item.toggle() } } }
        Accessible.role: Accessible.Button
        Accessible.name: bar.item && bar.item.playing ? "Pause" : "Play"
    }

    Item { // the waveform
        id: wave
        Layout.fillWidth: true
        Layout.preferredHeight: 30
        readonly property real total: page.info.duration > 0 ? page.info.duration : (bar.item ? bar.item.duration : 0)
        readonly property real fraction: total > 0 && bar.item ? Math.min(1, bar.item.position / total) : 0
        readonly property int bars: Math.max(20, Math.floor(width / 3))
        readonly property var peaks: audioIntel.waveform
        Row {
            anchors.fill: parent
            spacing: 1
            Repeater {
                model: wave.bars
                Rectangle {
                    required property int index
                    readonly property real at: (index + 0.5) / wave.bars
                    readonly property real level: wave.peaks.length > 0 ? wave.peaks[Math.min(wave.peaks.length - 1, Math.floor(at * wave.peaks.length))] : 0.12
                    width: Math.max(1, (wave.width - (wave.bars - 1)) / wave.bars)
                    height: Math.max(3, wave.height * Math.min(1, level))
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 1
                    color: at <= wave.fraction ? theme.accent : Qt.alpha(theme.textDim, 0.45)
                }
            }
        }
        Rectangle { visible: wave.fraction > 0 && wave.fraction < 1; x: wave.width * wave.fraction - 1; width: 2; height: parent.height; radius: 1; color: theme.text }
        Rectangle { // the time under the pointer
            visible: waveMouse.containsMouse
            x: Math.max(0, Math.min(wave.width - width, waveMouse.mouseX - width / 2))
            y: -22
            width: hoverText.implicitWidth + 12
            height: 18
            radius: 9
            color: theme.surface
            border.width: 1
            border.color: theme.hairline
            Text { id: hoverText; anchors.centerIn: parent; text: wave.total > 0 ? page.clock(wave.total * waveMouse.mouseX / wave.width) : ""; color: theme.text; font.pixelSize: 10; font.family: "monospace" }
        }
        MouseArea {
            id: waveMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: (m) => { if (bar.item && wave.total > 0) { page.follow(); bar.item.seek(wave.total * m.x / width) } }
        }
    }

    Text {
        color: theme.textDim
        font.pixelSize: 11
        font.family: "monospace"
        text: (bar.item ? page.clock(bar.item.position) : "00:00") + " / " + (page.info.durationText || "00:00")
    }
    Rectangle { // the speed: one button that goes round
        Layout.preferredWidth: 38
        Layout.preferredHeight: 22
        radius: 11
        color: speedHover.hovered ? theme.hoverFill : "transparent"
        border.width: 1
        border.color: theme.hairline
        HoverHandler { id: speedHover; cursorShape: Qt.PointingHandCursor }
        Text { anchors.centerIn: parent; text: page.rate + "×"; color: page.rate !== 1 ? theme.accent : theme.textDim; font.pixelSize: 11; font.weight: Font.DemiBold }
        TapHandler { onTapped: page.rate = page.rate >= 2 ? 1 : page.rate === 1 ? 1.25 : page.rate === 1.25 ? 1.5 : 2 }
        Accessible.role: Accessible.Button
        Accessible.name: "Playback speed"
    }
}
