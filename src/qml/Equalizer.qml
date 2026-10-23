import QtQuick

// Bars that follow what's actually playing (AudioLevels); a canned animation if capture isn't
// available. Holds the capture only while on screen and media is playing.
Row {
    id: eq
    property int bars: 5
    property real barWidth: 3
    property real maxHeight: 16
    property color color: "#7fb5cf"
    readonly property bool live: visible && media.playing
    property bool holding: false

    spacing: barWidth * 0.7
    height: maxHeight

    function sync() {
        if (live && !holding) {
            audio.acquire()
            holding = true
        } else if (!live && holding) {
            audio.release()
            holding = false
        }
    }
    onLiveChanged: sync()
    Component.onCompleted: sync()
    Component.onDestruction: if (holding) audio.release()

    Repeater {
        model: eq.bars
        Rectangle {
            id: bar
            required property int index
            property real fake: 0.3
            // Boosted: everyday audio sits well below full scale, bars should still dance.
            readonly property real level: audio.available && audio.levels.length > index
                                          ? Math.min(1, audio.levels[index] * 1.8) : fake
            anchors.verticalCenter: parent.verticalCenter
            width: eq.barWidth
            radius: width / 2
            color: eq.color
            height: eq.live ? Math.max(eq.barWidth, level * eq.maxHeight) : eq.barWidth
            Behavior on height { NumberAnimation { duration: 80 } }
            SequentialAnimation on fake {
                running: eq.live && !audio.available
                loops: Animation.Infinite
                NumberAnimation { to: 0.5 + (bar.index * 0.13) % 0.5; duration: 260 + bar.index * 60 }
                NumberAnimation { to: 0.2; duration: 300 + bar.index * 40 }
            }
        }
    }
}
