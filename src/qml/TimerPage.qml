import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Timer, stopwatch and Pomodoro. The running one also shows in the closed notch (see Main.qml's live pill).
ColumnLayout {
    id: page
    required property var shell
    visible: shell.page === "timer"
    Layout.fillWidth: true
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 12

    readonly property bool active: activities.timerKind !== ""
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

    // ---- Running ----
    ColumnLayout {
        visible: page.active
        Layout.fillWidth: true
        spacing: 10
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: activities.timerLabel + (activities.timerKind === "pomodoro" ? " · round " + activities.pomodoroRound : "")
            color: activities.timerKind === "pomodoro" && activities.timerLabel === "Break" ? theme.success : theme.textDim
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: page.clock(page.now)
            color: activities.timerRunning ? theme.text : theme.textDim
            font.pixelSize: 44
            font.weight: Font.Light
            font.family: "monospace"
        }
        Rectangle {
            visible: activities.timerTotalMs > 0
            Layout.fillWidth: true
            implicitHeight: 4
            radius: 2
            color: theme.surface
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, 1 - page.now / Math.max(1, activities.timerTotalMs)))
                height: parent.height
                radius: 2
                color: theme.accent
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            PillButton {
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

    // ---- Start one ----
    ColumnLayout {
        visible: !page.active
        Layout.fillWidth: true
        spacing: 8
        SectionLabel { text: "Timer" }
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

        SectionLabel { text: "Pomodoro"; Layout.topMargin: 6 }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Caption {
                Layout.fillWidth: true
                text: activities.focusMinutes + " min focus, " + activities.breakMinutes + " min break, round after round."
            }
            PillButton { text: "Start"; checked: true; onClicked: activities.startPomodoro() }
        }
        RowLayout {
            spacing: 6
            Caption { text: "Focus" }
            Repeater {
                model: [25, 50]
                PillButton {
                    required property int modelData
                    implicitHeight: 24
                    text: modelData + "m"
                    checked: activities.focusMinutes === modelData
                    onClicked: activities.focusMinutes = modelData
                }
            }
            Caption { text: "Break"; leftPadding: 8 }
            Repeater {
                model: [5, 10, 15]
                PillButton {
                    required property int modelData
                    implicitHeight: 24
                    text: modelData + "m"
                    checked: activities.breakMinutes === modelData
                    onClicked: activities.breakMinutes = modelData
                }
            }
        }

        SectionLabel { text: "Stopwatch"; Layout.topMargin: 6 }
        RowLayout {
            Layout.fillWidth: true
            Caption { Layout.fillWidth: true; text: "Counts up until you stop it." }
            PillButton { text: "Start"; checked: true; onClicked: activities.startStopwatch() }
        }
        Item { Layout.preferredHeight: 2 }
    }
}
