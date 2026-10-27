import QtQuick
import QtQuick.Shapes

// The spectrum beside the song, drawn in the style chosen in Settings (activities.eqStyle: bars, thin, dots, wave,
// pulse, off). It follows what's actually playing (AudioLevels); a canned animation if capture isn't available.
// Holds the capture only while on screen and media is playing. `preview`: runs the canned animation (Settings).
Item {
    id: eq
    property int bars: 5
    property real barWidth: 3
    property real maxHeight: 16
    property color color: theme.media
    property bool preview: false
    readonly property string style: activities.eqStyle
    readonly property bool live: visible && style !== "off" && (preview || media.playing)
    property bool holding: false

    // how many shapes the style draws
    readonly property int count: style === "thin" ? 9 : bars
    readonly property real gap: barWidth * 0.7
    implicitWidth: style === "off" ? 0 : style === "pulse" ? maxHeight : style === "dots" ? bars * dotSize + (bars - 1) * gap : count * barWidth + (count - 1) * (style === "thin" ? barWidth * 0.5 : gap)
    implicitHeight: style === "off" ? 0 : maxHeight
    width: implicitWidth
    height: implicitHeight
    readonly property real dotSize: barWidth * 1.6

    // 0..1 of band i (a fraction between two bands is interpolated), boosted so everyday audio still dances;
    // the curve rises 1.8x at the bottom and flattens toward 1, so loud music no longer clips flat.
    property var canned: [0.3, 0.3, 0.3, 0.3, 0.3] // the animated stand-in when there is no capture
    function level(pos) {
        const n = eq.bars
        const p = Math.max(0, Math.min(n - 1, pos))
        const a = Math.floor(p), b = Math.min(n - 1, a + 1), f = p - a
        const real = audio.available && !eq.preview
        const va = real ? (audio.levels.length > a ? audio.levels[a] : 0) : canned[a]
        const vb = real ? (audio.levels.length > b ? audio.levels[b] : 0) : canned[b]
        const raw = va + (vb - va) * f
        return real ? raw * (1.8 - 0.8 * raw) : raw
    }
    readonly property real avg: { const l = audio.levels; canned; let t = 0; for (let i = 0; i < eq.bars; ++i) t += level(i); return t / eq.bars }

    function sync() {
        if (live && !holding && !preview) {
            audio.acquire()
            holding = true
        } else if ((!live || preview) && holding) {
            audio.release()
            holding = false
        }
    }
    onLiveChanged: sync()
    onPreviewChanged: sync()
    Component.onCompleted: sync()
    Component.onDestruction: if (holding) audio.release()

    // The canned animation: one value per band, wandering between a high and a low.
    Repeater {
        model: eq.bars
        Item {
            id: fake
            required property int index
            property real v: 0.3
            onVChanged: { const c = eq.canned.slice(); c[index] = v; eq.canned = c }
            SequentialAnimation on v {
                running: eq.live && (!audio.available || eq.preview)
                loops: Animation.Infinite
                NumberAnimation { to: 0.5 + (fake.index * 0.13) % 0.5; duration: 260 + fake.index * 60 }
                NumberAnimation { to: 0.2; duration: 300 + fake.index * 40 }
            }
        }
    }

    // bars and thin: vertical bars (thin: 9 of them, mirrored from the centre)
    Row {
        visible: eq.style === "bars" || eq.style === "thin"
        spacing: eq.style === "thin" ? eq.barWidth * 0.5 : eq.gap
        height: eq.maxHeight
        Repeater {
            model: eq.count
            Rectangle {
                id: bar
                required property int index
                readonly property real lv: eq.style === "thin" ? eq.level(Math.abs(index - 4) * 0.9) : eq.level(index)
                anchors.verticalCenter: parent.verticalCenter
                width: eq.style === "thin" ? Math.max(1.5, eq.barWidth * 0.6) : eq.barWidth
                radius: width / 2
                color: eq.color
                height: eq.live ? Math.max(width, lv * eq.maxHeight) : width
                // The levels are already smoothed on a 33 ms tick; this bridges two ticks so a bar never jumps.
                Behavior on height { enabled: activities.animations; NumberAnimation { duration: 70 } }
            }
        }
    }

    // dots: round dots that grow with the level
    Row {
        visible: eq.style === "dots"
        spacing: eq.gap
        height: eq.maxHeight
        Repeater {
            model: eq.bars
            Item {
                id: dot
                required property int index
                width: eq.dotSize
                height: eq.maxHeight
                Rectangle {
                    readonly property real d: eq.live ? eq.dotSize * (0.6 + 1.1 * eq.level(dot.index)) : eq.dotSize * 0.6
                    anchors.centerIn: parent
                    width: d; height: d; radius: d / 2
                    color: eq.color
                    opacity: eq.live ? 0.55 + 0.45 * eq.level(dot.index) : 0.6
                    Behavior on width { enabled: activities.animations; NumberAnimation { duration: 70 } }
                    Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 70 } }
                }
            }
        }
    }

    // wave: a smooth line through the levels
    Shape {
        visible: eq.style === "wave"
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: eq.color
            strokeWidth: Math.max(1.6, eq.barWidth * 0.7)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: 0
            startY: eq.height / 2
            PathCurve { x: eq.width * 0.0;  y: eq.height / 2 - (eq.live ? (eq.level(0) - 0.35) * eq.maxHeight * 0.6 : 0) }
            PathCurve { x: eq.width * 0.25; y: eq.height / 2 + (eq.live ? (eq.level(1) - 0.35) * eq.maxHeight * 0.55 : 0) }
            PathCurve { x: eq.width * 0.5;  y: eq.height / 2 - (eq.live ? (eq.level(2) - 0.35) * eq.maxHeight * 0.6 : 0) }
            PathCurve { x: eq.width * 0.75; y: eq.height / 2 + (eq.live ? (eq.level(3) - 0.35) * eq.maxHeight * 0.55 : 0) }
            PathCurve { x: eq.width;        y: eq.height / 2 - (eq.live ? (eq.level(4) - 0.35) * eq.maxHeight * 0.6 : 0) }
        }
    }

    // pulse: a circle that breathes with the sound
    Rectangle {
        visible: eq.style === "pulse"
        anchors.centerIn: parent
        readonly property real d: eq.live ? eq.maxHeight * (0.35 + 0.65 * Math.min(1, eq.avg * 1.2)) : eq.maxHeight * 0.35
        width: d; height: d; radius: d / 2
        color: eq.color
        opacity: eq.live ? 0.6 + 0.4 * Math.min(1, eq.avg) : 0.6
        Behavior on width { enabled: activities.animations; NumberAnimation { duration: 80 } }
        Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 80 } }
    }
}
