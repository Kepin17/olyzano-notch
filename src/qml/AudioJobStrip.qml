import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// What is going on with this recording, in one thin strip: working (with the stage and the percent, and the stages when you ask),
// stopped, or not processed yet.
ColumnLayout {
    id: strip
    required property var page
    property bool open: false
    readonly property bool working: page.jobHere
    readonly property bool failed: page.info.status === "failed" && !audioIntel.busy
    readonly property bool idle: page.info.status === "ready" && !audioIntel.busy
    visible: working || failed || idle || audioIntel.warnings.length > 0 && !audioIntel.busy
    Layout.fillWidth: true
    spacing: 4

    Rectangle {
        visible: strip.working || strip.failed || strip.idle
        Layout.fillWidth: true
        implicitHeight: main.implicitHeight + 14
        radius: theme.radiusSm + 2
        color: strip.failed ? theme.dangerBg : theme.well
        border.width: 1
        border.color: strip.failed ? Qt.alpha(theme.danger, 0.4) : theme.hairline
        RowLayout {
            id: main
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
            spacing: 10
            BusyIndicator { visible: strip.working; running: visible; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
            Rectangle { visible: !strip.working; Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: strip.failed ? theme.danger : theme.textFaint }
            StageText { // working: the stage changes with a fade, and the percent glides up
                visible: strip.working
                Layout.fillWidth: true
                text: page.activeStage ? page.activeStage.label : "Working"
                color: theme.text
                pixelSize: 12
            }
            Text {
                visible: strip.working
                property real shownPct: audioIntel.jobProgress * 100
                Behavior on shownPct { enabled: activities.animations; NumberAnimation { duration: 480; easing.type: Easing.OutCubic } }
                text: Math.round(shownPct) + "%"
                color: theme.textDim
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.family: "monospace"
            }
            Text {
                visible: !strip.working
                Layout.fillWidth: true
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: strip.failed ? theme.danger : theme.text
                font.pixelSize: 12
                text: strip.failed ? (page.info.error || "It stopped") + (/[.!?]$/.test(page.info.error || "") ? " " : ". ") + "What is done is kept." : "Not processed yet"
                TapHandler { enabled: strip.working; onTapped: strip.open = !strip.open }
            }
            PillButton { visible: strip.working; text: strip.open ? "Hide" : "Steps"; onClicked: strip.open = !strip.open }
            PillButton { visible: strip.working; text: "Stop"; onClicked: audioIntel.cancel() }
            PillButton { visible: strip.failed; text: "Continue"; primary: true; onClicked: audioIntel.process(audioIntel.current) }
            PillButton { visible: strip.idle; text: "Process"; primary: true; onClicked: audioIntel.process(audioIntel.current) }
        }
        LiveBar { // the progress, along the bottom edge: it glides, and a light runs along it
            visible: strip.working
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 1; rightMargin: 1; bottomMargin: 1 }
            height: 3
            value: audioIntel.jobProgress
        }
    }
    ColumnLayout { // the stages, when asked for
        visible: strip.working && strip.open
        Layout.fillWidth: true
        Layout.leftMargin: 4
        spacing: 2
        Repeater {
            model: audioIntel.stages
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    id: stepDot
                    Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3
                    color: modelData.state === "done" ? theme.success : modelData.state === "active" ? theme.accent : theme.textFaint
                    Behavior on color { enabled: activities.animations; ColorAnimation { duration: 260 } }
                    SequentialAnimation on scale { // the step that is going on breathes
                        running: modelData.state === "active" && activities.animations
                        loops: Animation.Infinite
                        NumberAnimation { to: 1.6; duration: 600; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
                    }
                    onColorChanged: if (modelData.state === "done" && activities.animations) pop.restart() // a step is done: a small pop
                    SequentialAnimation { id: pop; NumberAnimation { target: stepDot; property: "scale"; to: 1.9; duration: 130 } NumberAnimation { target: stepDot; property: "scale"; to: 1.0; duration: 220; easing.type: Easing.OutBack } }
                }
                Text { Layout.fillWidth: true; text: modelData.label; color: modelData.state === "pending" ? theme.textFaint : theme.textDim; font.pixelSize: 11 }
                Text { visible: modelData.state === "active"; text: Math.round(modelData.progress * 100) + "%"; color: theme.textDim; font.pixelSize: 11 }
            }
        }
    }
    Text {
        visible: audioIntel.warnings.length > 0 && !audioIntel.busy
        Layout.fillWidth: true
        color: theme.warn
        font.pixelSize: 11
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: audioIntel.warnings.join("  ")
    }
}
