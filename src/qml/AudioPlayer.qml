import QtQuick
import QtMultimedia

// The player behind the bar of a recording. QtMultimedia is loaded only when a meeting is opened (the page loads this file lazily),
// so Notch does not carry it around otherwise.
Item {
    id: p
    property url source
    property real rate: 1
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property real position: player.position / 1000 // seconds
    readonly property real duration: player.duration / 1000
    readonly property string error: player.error !== MediaPlayer.NoError ? player.errorString : ""
    function toggle() { playing ? player.pause() : player.play() }
    function seek(seconds) { player.position = Math.max(0, seconds * 1000) }
    function playFrom(seconds) { seek(seconds); player.play() }
    function stop() { player.stop() }
    MediaPlayer {
        id: player
        source: p.source
        playbackRate: p.rate
        audioOutput: AudioOutput {}
    }
    Component.onDestruction: player.stop()
}
