import QtQuick

// The same player as AudioPlayer.qml, but through the computer's default sound output (audioIntel.playback): QtMultimedia does not list
// every output on Linux and may play into one that is not the one you hear.
Item {
    id: p
    property url source
    property real rate: 1
    property real length: 0     // seconds, from the recording
    property bool mixdown: false
    readonly property var pb: audioIntel.playback
    readonly property bool playing: pb ? pb.playing : false
    readonly property real position: pb ? pb.position : 0
    readonly property real duration: length
    readonly property string error: pb ? pb.error : "The audio output is not available"
    function toggle() { if (pb) pb.toggle() }
    function seek(seconds) { if (pb) pb.seek(seconds) }
    function playFrom(seconds) { if (pb) pb.playFrom(seconds) }
    function stop() { if (pb) pb.stop() }
    function sync() { if (pb && source.toString() !== "") pb.setSource(source.toString(), length, mixdown) }
    onSourceChanged: sync()
    onLengthChanged: sync()
    onMixdownChanged: sync()
    onRateChanged: if (pb) pb.rate = rate
    Component.onCompleted: { sync(); if (pb) pb.rate = rate }
    Component.onDestruction: stop()
}
