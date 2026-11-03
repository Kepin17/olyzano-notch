import QtQuick
import QtQuick.Shapes

// The notch's cat. Drawn from a few shapes (no pictures), and it shows a mood: calm, sleepy, happy, excited, love, worried, sad,
// surprised, confused, listening, thinking, music, munch. A mood is one row of data in `moods`; moving between two eases the
// numbers (`animated`). Nothing here runs all the time: a blink every few seconds and a glance now and then, both stopped while
// hidden or when `lively` is off. A sleeping cat only breathes out a "z" now and then.
Item {
    id: cat
    property string mood: "calm"
    property real size: 24
    property bool animated: true // eased changes of expression
    property bool lively: true   // blinks, looks around, munches, nods: off = a still picture
    readonly property real u: size / 24
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    Accessible.ignored: true

    // eye: dot | wide | arc (^ ^) | line (closed) | heart.  smile -1..1.  mouthOpen 0..1.  earUp 0..1.  blush 0..1.
    // look: where the eyes point.  effect: shown beside the head.
    readonly property var moods: ({
        calm:      { eye: "dot",   open: 1.0, smile: 0.4,  mouthOpen: 0,   earUp: 0.5, blush: 0,   lookX: 0,   lookY: 0,    effect: "" },
        sleepy:    { eye: "line",  open: 0,   smile: 0.2,  mouthOpen: 0,   earUp: 0.2, blush: 0,   lookX: 0,   lookY: 0,    effect: "z" },
        happy:     { eye: "arc",   open: 1,   smile: 1,    mouthOpen: 0,   earUp: 0.7, blush: 0.9, lookX: 0,   lookY: 0,    effect: "" },
        excited:   { eye: "wide",  open: 1,   smile: 1,    mouthOpen: 0.8, earUp: 1,   blush: 0.6, lookX: 0,   lookY: 0,    effect: "sparkle" },
        love:      { eye: "heart", open: 1,   smile: 0.9,  mouthOpen: 0,   earUp: 0.65, blush: 1,  lookX: 0,   lookY: 0,    effect: "" },
        worried:   { eye: "dot",   open: 0.8, smile: -0.7, mouthOpen: 0,   earUp: 0.1, blush: 0,   lookX: 0,   lookY: 0.5,  effect: "sweat" },
        sad:       { eye: "dot",   open: 0.55, smile: -1,  mouthOpen: 0,   earUp: 0,   blush: 0,   lookX: 0,   lookY: 0.7,  effect: "tear" },
        surprised: { eye: "wide",  open: 1,   smile: 0,    mouthOpen: 1,   earUp: 1,   blush: 0,   lookX: 0,   lookY: 0,    effect: "!" },
        confused:  { eye: "dot",   open: 1,   smile: -0.2, mouthOpen: 0,   earUp: 0.3, blush: 0,   lookX: 0.8, lookY: -0.2, effect: "?" },
        listening: { eye: "wide",  open: 1,   smile: 0.3,  mouthOpen: 0,   earUp: 1,   blush: 0,   lookX: 0,   lookY: 0,    effect: "wave" },
        thinking:  { eye: "dot",   open: 1,   smile: 0,    mouthOpen: 0,   earUp: 0.6, blush: 0,   lookX: 0.6, lookY: -0.8, effect: "dots" },
        music:     { eye: "arc",   open: 1,   smile: 0.9,  mouthOpen: 0,   earUp: 0.7, blush: 0.4, lookX: 0,   lookY: 0,    effect: "note" },
        munch:     { eye: "dot",   open: 1,   smile: 0.5,  mouthOpen: 0,   earUp: 0.5, blush: 0.3, lookX: 0,   lookY: 0.3,  effect: "" }
    })
    readonly property var m: moods[mood] || moods.calm

    // the numbers that ease
    property real open: m.open
    property real smile: m.smile
    property real mouthOpen: m.mouthOpen
    property real earUp: m.earUp
    property real blush: m.blush
    property real lookX: m.lookX
    property real lookY: m.lookY
    Behavior on open { enabled: cat.animated; NumberAnimation { duration: 160 } }
    Behavior on smile { enabled: cat.animated; NumberAnimation { duration: 160 } }
    Behavior on mouthOpen { enabled: cat.animated; NumberAnimation { duration: 160 } }
    Behavior on earUp { enabled: cat.animated; NumberAnimation { duration: 160 } }
    Behavior on blush { enabled: cat.animated; NumberAnimation { duration: 200 } }
    Behavior on lookX { enabled: cat.animated; NumberAnimation { duration: 140 } }
    Behavior on lookY { enabled: cat.animated; NumberAnimation { duration: 140 } }

    readonly property color fur: "#f3e2c7"
    readonly property color furShade: "#e2c9a3"
    readonly property color ink: "#2b2118"
    readonly property color pink: "#f2a3b3"
    readonly property bool eyesOpenKind: m.eye === "dot" || m.eye === "wide"

    // small extras that only move when something asks for it
    property bool blinking: false
    property real glanceX: 0
    property real munchOpen: 0
    property real nod: 0 // degrees
    property real hop: 0 // lift in units

    readonly property bool running: lively && visible && size > 0
    Timer { // a blink every few seconds
        id: blinkTimer
        running: cat.running && cat.eyesOpenKind
        repeat: false
        interval: 3000 + Math.random() * 4000
        onTriggered: { cat.blinking = true; closeEyes.start() }
    }
    Timer { id: closeEyes; interval: 120; onTriggered: { cat.blinking = false; blinkTimer.interval = 3000 + Math.random() * 4000; blinkTimer.restart() } }
    Timer { // a glance to one side, when nothing else is going on
        id: glanceTimer
        running: cat.running && cat.mood === "calm"
        interval: 8000 + Math.random() * 7000
        onTriggered: { cat.glanceX = Math.random() < 0.5 ? -0.8 : 0.8; back.start(); interval = 8000 + Math.random() * 7000 }
    }
    Timer { id: back; interval: 1400; onTriggered: cat.glanceX = 0 }
    Timer { // chewing
        running: cat.running && cat.mood === "munch"
        repeat: true
        interval: 260
        onTriggered: cat.munchOpen = cat.munchOpen > 0 ? 0 : 0.7
        onRunningChanged: if (!running) cat.munchOpen = 0
    }
    Timer { // a little nod to the beat
        running: cat.running && cat.mood === "music"
        repeat: true
        interval: 480
        onTriggered: cat.nod = cat.nod > 0 ? -4 : 4
        onRunningChanged: if (!running) cat.nod = 0
    }
    Behavior on nod { enabled: cat.animated; NumberAnimation { duration: 200; easing.type: Easing.InOutSine } }
    Behavior on hop { enabled: cat.animated; NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
    onMoodChanged: if (animated && lively && (mood === "excited" || mood === "happy" || mood === "surprised")) { hop = 2; hopBack.restart() }
    Timer { id: hopBack; interval: 130; onTriggered: cat.hop = 0 }

    // the z of a sleeper comes and goes
    property bool zShown: true
    Timer { running: cat.running && cat.mood === "sleepy"; repeat: true; interval: 2600; onTriggered: cat.zShown = !cat.zShown }

    Item {
        id: body
        anchors.fill: parent
        transformOrigin: Item.Bottom
        rotation: cat.nod
        y: -cat.hop * cat.u

        // ears: the tip rises with earUp
        Shape {
            anchors.fill: parent
            ShapePath { // left
                strokeColor: "transparent"; fillColor: cat.fur
                startX: 3.2 * cat.u; startY: 10 * cat.u
                PathLine { x: 4.4 * cat.u; y: (4 - 2.8 * cat.earUp) * cat.u }
                PathLine { x: 10.4 * cat.u; y: 7 * cat.u }
            }
            ShapePath { // right
                strokeColor: "transparent"; fillColor: cat.fur
                startX: 20.8 * cat.u; startY: 10 * cat.u
                PathLine { x: 19.6 * cat.u; y: (4 - 2.8 * cat.earUp) * cat.u }
                PathLine { x: 13.6 * cat.u; y: 7 * cat.u }
            }
            ShapePath { // inside, left
                strokeColor: "transparent"; fillColor: cat.pink
                startX: 4.8 * cat.u; startY: 9.2 * cat.u
                PathLine { x: 5.2 * cat.u; y: (6 - 2 * cat.earUp) * cat.u }
                PathLine { x: 8.6 * cat.u; y: 7.6 * cat.u }
            }
            ShapePath { // inside, right
                strokeColor: "transparent"; fillColor: cat.pink
                startX: 19.2 * cat.u; startY: 9.2 * cat.u
                PathLine { x: 18.8 * cat.u; y: (6 - 2 * cat.earUp) * cat.u }
                PathLine { x: 15.4 * cat.u; y: 7.6 * cat.u }
            }
        }
        // head
        Rectangle {
            x: 2 * cat.u; y: 6.4 * cat.u; width: 20 * cat.u; height: 16.4 * cat.u
            radius: 8 * cat.u
            color: cat.fur
            border.width: 0
        }
        // cheeks
        Repeater {
            model: [5.4, 18.6]
            Rectangle {
                required property real modelData
                x: (modelData - 1.8) * cat.u; y: 15.2 * cat.u; width: 3.6 * cat.u; height: 2.2 * cat.u; radius: 1.1 * cat.u
                color: cat.pink
                opacity: 0.7 * cat.blush
                visible: opacity > 0.01
            }
        }
        // eyes
        Repeater {
            model: [8, 16]
            Item {
                id: eye
                required property real modelData
                x: (modelData - 2.2) * cat.u; y: 11 * cat.u; width: 4.4 * cat.u; height: 4.4 * cat.u
                readonly property real h: cat.blinking ? 0.12 : Math.max(0.12, cat.open)
                // dot / wide
                Rectangle {
                    visible: cat.m.eye === "dot" || cat.m.eye === "wide"
                    readonly property real d: (cat.m.eye === "wide" ? 3.8 : 3) * cat.u
                    width: d; height: d * eye.h; radius: width / 2
                    x: (eye.width - width) / 2 + (cat.lookX + cat.glanceX) * 0.9 * cat.u
                    y: (eye.height - height) / 2 + cat.lookY * 0.8 * cat.u
                    color: cat.ink
                    Rectangle { // the spark in a wide eye
                        visible: cat.m.eye === "wide" && !cat.blinking
                        x: parent.width * 0.5; y: parent.height * 0.18; width: 1.1 * cat.u; height: 1.1 * cat.u; radius: width / 2; color: "white"
                    }
                }
                // closed, asleep
                Rectangle {
                    visible: cat.m.eye === "line"
                    x: 0.4 * cat.u; y: 2.1 * cat.u; width: 3.6 * cat.u; height: 0.9 * cat.u; radius: height / 2; color: cat.ink
                }
                // happy: a bow
                Shape {
                    visible: cat.m.eye === "arc"
                    anchors.fill: parent
                    ShapePath {
                        strokeColor: cat.ink; strokeWidth: 1.1 * cat.u; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                        startX: 0.5 * cat.u; startY: 3.1 * cat.u
                        PathQuad { x: 3.9 * cat.u; y: 3.1 * cat.u; controlX: 2.2 * cat.u; controlY: 0 }
                    }
                }
                // in love
                Text {
                    visible: cat.m.eye === "heart"
                    anchors.centerIn: parent
                    text: "♥"
                    color: "#ff5d7a"
                    font.pixelSize: Math.max(6, 5.6 * cat.u)
                }
            }
        }
        // nose
        Rectangle { x: 11 * cat.u; y: 15.4 * cat.u; width: 2 * cat.u; height: 1.3 * cat.u; radius: 0.65 * cat.u; color: "#e0728a" }
        // mouth: a curve under the nose, and an opening when it speaks or chews
        Rectangle {
            readonly property real o: Math.max(cat.mouthOpen, cat.munchOpen)
            visible: o > 0.05
            x: 10.8 * cat.u; y: 17.3 * cat.u; width: 2.4 * cat.u; height: 3 * cat.u * o; radius: width / 2
            color: "#7a2b3a"
        }
        Shape {
            anchors.fill: parent
            visible: Math.max(cat.mouthOpen, cat.munchOpen) < 0.3
            ShapePath {
                strokeColor: cat.ink; strokeWidth: 0.9 * cat.u; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                startX: 9.4 * cat.u; startY: 17.4 * cat.u
                PathQuad { x: 12 * cat.u; y: 17.4 * cat.u; controlX: 10.6 * cat.u; controlY: (17.4 + 2.2 * cat.smile) * cat.u }
                PathQuad { x: 14.6 * cat.u; y: 17.4 * cat.u; controlX: 13.4 * cat.u; controlY: (17.4 + 2.2 * cat.smile) * cat.u }
            }
        }
        // whiskers, only where there is room for them
        Repeater {
            model: cat.size >= 30 ? [[-1, -1], [-1, 1], [1, -1], [1, 1]] : []
            Rectangle {
                required property var modelData
                width: 4.6 * cat.u; height: 0.45 * cat.u; radius: height / 2
                x: modelData[0] < 0 ? 0.2 * cat.u : 19.2 * cat.u
                y: (16.6 + modelData[1] * 1.3) * cat.u
                rotation: modelData[0] * modelData[1] * 8
                color: cat.furShade
            }
        }
    }

    // beside the head
    Text {
        id: fx
        readonly property string e: cat.m.effect
        visible: (e === "z" ? cat.zShown : e !== "" && e !== "sweat" && e !== "tear" && e !== "wave")
        x: 17.6 * cat.u; y: (e === "z" ? -0.5 : 0) * cat.u
        color: e === "!" ? "#ff9f0a" : e === "?" ? "#64d2ff" : e === "note" ? "#bf5af2" : e === "sparkle" ? "#ffd60a" : "#9aa0a6"
        font.pixelSize: Math.max(6, (e === "dots" ? 6 : 7.5) * cat.u)
        font.bold: true
        text: e === "z" ? "z" : e === "!" ? "!" : e === "?" ? "?" : e === "note" ? "♪" : e === "sparkle" ? "✦" : e === "dots" ? "…" : ""
    }
    Rectangle { // a drop of worry
        visible: cat.m.effect === "sweat"
        x: 19.4 * cat.u; y: 7.2 * cat.u; width: 1.7 * cat.u; height: 2.4 * cat.u; radius: width / 2; color: "#64b5ff"
    }
    Rectangle { // a tear
        visible: cat.m.effect === "tear"
        x: 6.9 * cat.u; y: 15.6 * cat.u; width: 1.5 * cat.u; height: 2.6 * cat.u; radius: width / 2; color: "#64b5ff"
    }
    Text { // listening
        visible: cat.m.effect === "wave"
        x: 18.6 * cat.u; y: 4 * cat.u
        text: "))"
        color: "#64d2ff"
        font.pixelSize: Math.max(6, 8 * cat.u)
        font.bold: true
    }
}
