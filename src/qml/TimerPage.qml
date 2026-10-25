import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes

// Timer, Pomodoro and stopwatch. While none runs: pick one of the three and set it up; while one runs: a ring
// with the time, and its controls. The running one also shows in the closed notch (see Main.qml's live pill).
ColumnLayout {
    id: page
    required property var shell
    visible: shell.page === "timer"
    Layout.fillWidth: true
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 12

    readonly property bool active: activities.timerKind !== ""
    property string mode: "timer" // what is being set up: timer | pomodoro | stopwatch
    property double now: 0 // ticks the display; the time itself comes from activities.timerMs()
    Timer {
        interval: 250
        repeat: true
        running: page.visible && page.active
        triggeredOnStart: true
        onTriggered: page.now = activities.timerMs()
    }
    Connections {
        target: activities
        function onTimerChanged() { page.now = activities.timerMs() }
    }

    function clock(ms) {
        const total = Math.round(ms / 1000)
        const h = Math.floor(total / 3600), m = Math.floor(total / 60) % 60, s = total % 60
        const two = n => (n < 10 ? "0" : "") + n
        return (h > 0 ? h + ":" + two(m) : two(m)) + ":" + two(s)
    }

    // ---- Running: a ring with the time inside, the controls under it ----
    ColumnLayout {
        id: running
        visible: page.active
        Layout.fillWidth: true
        spacing: 10

        readonly property bool onBreak: activities.timerKind === "pomodoro" && activities.timerLabel === "Break"
        readonly property color tint: onBreak ? theme.success : theme.accent
        // Left to run (a timer, a Pomodoro round); a stopwatch sweeps once a minute.
        readonly property real fraction: activities.timerTotalMs > 0 ? Math.max(0, Math.min(1, page.now / activities.timerTotalMs))
                                                                       : (page.now % 60000) / 60000

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: activities.timerLabel + (activities.timerKind === "pomodoro" ? " · round " + activities.pomodoroRound : "")
            color: running.onBreak ? theme.success : theme.textDim
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        Item {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 168
            implicitHeight: 168
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                layer.enabled: true
                layer.samples: 4
                ShapePath { // the track
                    strokeColor: theme.surface
                    strokeWidth: 8
                    fillColor: "transparent"
                    PathAngleArc { centerX: 84; centerY: 84; radiusX: 78; radiusY: 78; startAngle: -90; sweepAngle: 360 }
                }
                ShapePath { // what is left
                    strokeColor: activities.timerRunning ? running.tint : theme.textDim
                    strokeWidth: 8
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc { centerX: 84; centerY: 84; radiusX: 78; radiusY: 78; startAngle: -90; sweepAngle: 359.9 * running.fraction }
                }
            }
            Column {
                anchors.centerIn: parent
                spacing: 2
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: page.clock(page.now)
                    color: activities.timerRunning ? theme.text : theme.textDim
                    font.pixelSize: 34
                    font.weight: Font.Light
                    font.family: "monospace"
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !activities.timerRunning
                    text: "paused"
                    color: theme.textDim
                    font.pixelSize: 11
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            PillButton {
                implicitWidth: 96
                text: activities.timerRunning ? "Pause" : "Resume"
                checked: true
                onClicked: activities.timerRunning ? activities.pause() : activities.resume()
            }
            PillButton {
                visible: activities.timerKind !== "stopwatch"
                text: "+1 min"
                onClicked: activities.addMinute()
            }
            PillButton { text: "Stop"; onClicked: activities.stop() }
        }
    }

    // ---- Set one up ----
    ColumnLayout {
        visible: !page.active
        Layout.fillWidth: true
        Layout.minimumHeight: 150 // the page keeps its height when you switch between the three
        spacing: 12

        Row {
            spacing: 6
            Repeater {
                model: [["timer", "Timer"], ["pomodoro", "Pomodoro"], ["stopwatch", "Stopwatch"]]
                PillButton {
                    required property var modelData
                    text: modelData[1]
                    checked: page.mode === modelData[0]
                    onClicked: page.mode = modelData[0]
                }
            }
        }

        // Timer: one tap on a length, or your own
        ColumnLayout {
            visible: page.mode === "timer"
            Layout.fillWidth: true
            spacing: 10
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [1, 3, 5, 10, 15, 30, 45, 60]
                    PillButton {
                        required property int modelData
                        text: modelData + " min"
                        onClicked: activities.startTimer(modelData * 60, "")
                    }
                }
            }
            Caption { text: "Or your own" }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: minutesField
                    Layout.preferredWidth: 90
                    implicitHeight: 30
                    leftPadding: 12
                    placeholderText: "Minutes"
                    placeholderTextColor: theme.placeholder
                    validator: IntValidator { bottom: 1; top: 600 }
                    color: theme.text
                    selectionColor: theme.accent
                    font.pixelSize: 13
                    background: Rectangle { radius: 15; color: theme.well; border.width: 1; border.color: minutesField.activeFocus ? theme.accent : theme.outline }
                    onAccepted: startCustom.clicked()
                }
                TextField {
                    id: labelField
                    Layout.fillWidth: true
                    implicitHeight: 30
                    leftPadding: 12
                    placeholderText: "Name (optional), e.g. Tea"
                    placeholderTextColor: theme.placeholder
                    color: theme.text
                    selectionColor: theme.accent
                    font.pixelSize: 13
                    background: Rectangle { radius: 15; color: theme.well; border.width: 1; border.color: labelField.activeFocus ? theme.accent : theme.outline }
                    onAccepted: startCustom.clicked()
                }
                PillButton {
                    id: startCustom
                    text: "Start"
                    checked: true
                    enabled: minutesField.text !== ""
                    onClicked: {
                        activities.startTimer(parseInt(minutesField.text) * 60, labelField.text)
                        minutesField.text = ""
                        labelField.text = ""
                    }
                }
            }
        }

        // Pomodoro: focus and break lengths, then go
        ColumnLayout {
            visible: page.mode === "pomodoro"
            Layout.fillWidth: true
            spacing: 10
            RowLayout {
                spacing: 6
                Caption { text: "Focus"; Layout.preferredWidth: 44 }
                Repeater {
                    model: [25, 50]
                    PillButton {
                        required property int modelData
                        text: modelData + " min"
                        checked: activities.focusMinutes === modelData
                        onClicked: activities.focusMinutes = modelData
                    }
                }
            }
            RowLayout {
                spacing: 6
                Caption { text: "Break"; Layout.preferredWidth: 44 }
                Repeater {
                    model: [5, 10, 15]
                    PillButton {
                        required property int modelData
                        text: modelData + " min"
                        checked: activities.breakMinutes === modelData
                        onClicked: activities.breakMinutes = modelData
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Caption { Layout.fillWidth: true; text: "Round after round until you stop it." }
                PillButton { text: "Start"; checked: true; implicitWidth: 96; onClicked: activities.startPomodoro() }
            }
        }

        // Stopwatch
        RowLayout {
            visible: page.mode === "stopwatch"
            Layout.fillWidth: true
            Caption { Layout.fillWidth: true; text: "Counts up until you stop it." }
            PillButton { text: "Start"; checked: true; implicitWidth: 96; onClicked: activities.startStopwatch() }
        }
        Item { Layout.preferredHeight: 2 }
    }
}
