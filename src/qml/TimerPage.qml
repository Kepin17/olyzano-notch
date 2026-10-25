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
    Layout.maximumWidth: 100000 // a fixed-size child (the ring) would otherwise cap the whole page
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 14

    readonly property bool active: activities.timerKind !== ""
    property string mode: "timer" // what is being set up: timer | pomodoro | stopwatch
    property int custom: 15 // minutes, for "your own"
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
        Layout.maximumWidth: 100000
        spacing: 10

        readonly property bool onBreak: activities.timerKind === "pomodoro" && activities.timerLabel === "Break"
        readonly property color tint: onBreak ? theme.success : theme.accent
        // Left to run (a timer, a Pomodoro round); a stopwatch sweeps once a minute.
        readonly property real fraction: activities.timerTotalMs > 0 ? Math.max(0, Math.min(1, page.now / activities.timerTotalMs))
                                                                       : (page.now % 60000) / 60000
        property real shown: fraction
        Behavior on shown { enabled: activities.timerKind !== "stopwatch"; NumberAnimation { duration: 260 } }

        Rectangle { // what it is: the name, and the round
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: chipText.implicitWidth + 22
            implicitHeight: 24
            radius: height / 2
            color: running.onBreak ? Qt.alpha(theme.success, 0.16) : theme.accentSoft
            Text {
                id: chipText
                anchors.centerIn: parent
                text: activities.timerLabel + (activities.timerKind === "pomodoro" ? " · round " + activities.pomodoroRound : "")
                color: running.onBreak ? theme.success : theme.accent
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }
        Item {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 188
            implicitHeight: 188
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                layer.enabled: true
                layer.samples: 4
                ShapePath { // the track
                    strokeColor: theme.surface
                    strokeWidth: 10
                    fillColor: "transparent"
                    PathAngleArc { centerX: 94; centerY: 94; radiusX: 86; radiusY: 86; startAngle: -90; sweepAngle: 360 }
                }
                ShapePath { // a soft glow under what is left
                    strokeColor: Qt.alpha(running.tint, activities.timerRunning ? 0.20 : 0)
                    strokeWidth: 18
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc { centerX: 94; centerY: 94; radiusX: 86; radiusY: 86; startAngle: -90; sweepAngle: 359.9 * running.shown }
                }
                ShapePath { // what is left
                    strokeColor: activities.timerRunning ? running.tint : theme.textDim
                    strokeWidth: 10
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc { centerX: 94; centerY: 94; radiusX: 86; radiusY: 86; startAngle: -90; sweepAngle: 359.9 * running.shown }
                }
            }
            Column {
                anchors.centerIn: parent
                spacing: 2
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: page.clock(page.now)
                    color: activities.timerRunning ? theme.text : theme.textDim
                    font.pixelSize: 40
                    font.weight: Font.Light
                    font.family: "monospace"
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: activities.timerRunning ? (activities.timerKind === "stopwatch" ? "counting" : "left") : "paused"
                    color: theme.textDim
                    font.pixelSize: 11
                }
            }
        }
        Row { // Pomodoro: four rounds to a set
            Layout.alignment: Qt.AlignHCenter
            visible: activities.timerKind === "pomodoro"
            spacing: 6
            Repeater {
                model: 4
                Rectangle {
                    required property int index
                    width: 8; height: 8; radius: 4
                    color: index < ((activities.pomodoroRound - 1) % 4) + 1 ? running.tint : theme.surfacePressed
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 14
            CircleAction { iconName: "stop"; Accessible.name: "Stop"; onClicked: activities.stop() }
            CircleAction {
                primary: true
                iconName: activities.timerRunning ? "pause" : "play"
                Accessible.name: activities.timerRunning ? "Pause" : "Resume"
                onClicked: activities.timerRunning ? activities.pause() : activities.resume()
            }
            CircleAction {
                visible: activities.timerKind !== "stopwatch"
                iconName: "plus"
                Accessible.name: "Add a minute"
                onClicked: activities.addMinute()
                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "+1 min"
            }
        }
        Item { Layout.preferredHeight: 2 }
    }

    // ---- Set one up ----
    ColumnLayout {
        visible: !page.active
        Layout.fillWidth: true
        Layout.minimumHeight: 200 // the page keeps its height when you switch between the three
        spacing: 14

        Segmented {
            Layout.fillWidth: true
            model: [["timer", "Timer"], ["pomodoro", "Pomodoro"], ["stopwatch", "Stopwatch"]]
            value: page.mode
            onPicked: v => page.mode = v
        }

        // Timer: one tap on a length, or your own
        ColumnLayout {
            visible: page.mode === "timer"
            Layout.fillWidth: true
            spacing: 12
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: 8
                rowSpacing: 8
                Repeater {
                    model: [1, 3, 5, 10, 15, 30, 45, 60]
                    Rectangle {
                        id: preset
                        required property int modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: 54
                        radius: theme.radiusMd
                        color: presetTap.pressed ? theme.surfacePressed : presetHover.hovered ? theme.surfaceHover : theme.surface
                        border.width: theme.washi || presetHover.hovered ? 1 : 0
                        border.color: presetHover.hovered ? theme.accent : theme.outline
                        Behavior on color { ColorAnimation { duration: 120 } }
                        HoverHandler { id: presetHover }
                        TapHandler { id: presetTap; onTapped: activities.startTimer(preset.modelData * 60, "") }
                        Column {
                            anchors.centerIn: parent
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: preset.modelData; color: theme.text; font.pixelSize: 19; font.weight: Font.DemiBold }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "min"; color: theme.textDim; font.pixelSize: 10 }
                        }
                    }
                }
            }
            Caption { text: "Or your own" }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle { // − 15 +
                    implicitWidth: 138
                    implicitHeight: 34
                    radius: height / 2
                    color: theme.well
                    border.width: 1
                    border.color: theme.outline
                    RowLayout {
                        anchors.fill: parent
                        spacing: 0
                        AbstractButton {
                            Layout.preferredWidth: 38; Layout.fillHeight: true
                            autoRepeat: true
                            autoRepeatDelay: 350; autoRepeatInterval: 70
                            onClicked: page.custom = Math.max(1, page.custom - (page.custom > 60 ? 5 : 1))
                            contentItem: LineIcon { glyph: "minus"; color: theme.text; sourceSize: Qt.size(16, 16) }
                            background: null
                        }
                        Text {
                            Layout.fillWidth: true
                            text: page.custom + " min"
                            color: theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                        }
                        AbstractButton {
                            Layout.preferredWidth: 38; Layout.fillHeight: true
                            autoRepeat: true
                            autoRepeatDelay: 350; autoRepeatInterval: 70
                            onClicked: page.custom = Math.min(600, page.custom + (page.custom >= 60 ? 5 : 1))
                            contentItem: LineIcon { glyph: "plus"; color: theme.text; sourceSize: Qt.size(16, 16) }
                            background: null
                        }
                    }
                }
                TextField {
                    id: labelField
                    Layout.fillWidth: true
                    implicitHeight: 34
                    leftPadding: 14
                    placeholderText: "Name (optional), e.g. Tea"
                    placeholderTextColor: theme.placeholder
                    color: theme.text
                    selectionColor: theme.accent
                    font.pixelSize: 13
                    background: Rectangle { radius: 17; color: theme.well; border.width: 1; border.color: labelField.activeFocus ? theme.accent : theme.outline }
                    onAccepted: startCustom.clicked()
                }
                PillButton {
                    id: startCustom
                    text: "Start"
                    iconName: "play"
                    checked: true
                    implicitHeight: 34
                    implicitWidth: 92
                    onClicked: {
                        activities.startTimer(page.custom * 60, labelField.text)
                        labelField.text = ""
                    }
                }
            }
        }

        // Pomodoro: focus and break lengths, then go
        ColumnLayout {
            visible: page.mode === "pomodoro"
            Layout.fillWidth: true
            spacing: 12
            RowLayout {
                spacing: 10
                Caption { text: "Focus"; Layout.preferredWidth: 44 }
                Segmented {
                    Layout.preferredWidth: 200
                    model: [[25, "25 min"], [50, "50 min"]]
                    value: activities.focusMinutes
                    onPicked: v => activities.focusMinutes = v
                }
            }
            RowLayout {
                spacing: 10
                Caption { text: "Break"; Layout.preferredWidth: 44 }
                Segmented {
                    Layout.preferredWidth: 270
                    model: [[5, "5 min"], [10, "10 min"], [15, "15 min"]]
                    value: activities.breakMinutes
                    onPicked: v => activities.breakMinutes = v
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Caption { Layout.fillWidth: true; text: "Round after round until you stop it." }
                PillButton { text: "Start"; iconName: "play"; checked: true; implicitHeight: 34; implicitWidth: 104; onClicked: activities.startPomodoro() }
            }
        }

        // Stopwatch
        RowLayout {
            visible: page.mode === "stopwatch"
            Layout.fillWidth: true
            Caption { Layout.fillWidth: true; text: "Counts up until you stop it." }
            PillButton { text: "Start"; iconName: "play"; checked: true; implicitHeight: 34; implicitWidth: 104; onClicked: activities.startStopwatch() }
        }
        Item { Layout.preferredHeight: 2 }
    }
}
