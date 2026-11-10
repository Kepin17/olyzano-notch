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
            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: strip.failed ? theme.danger : theme.text
                font.pixelSize: 12
                text: strip.working ? (page.activeStage ? page.activeStage.label : "Working") + " · " + Math.round(audioIntel.jobProgress * 100) + "%"
                      : strip.failed ? (page.info.error || "It stopped") + (/[.!?]$/.test(page.info.error || "") ? " " : ". ") + "What is done is kept." : "Not processed yet"
                TapHandler { enabled: strip.working; onTapped: strip.open = !strip.open }
            }
            PillButton { visible: strip.working; text: strip.open ? "Hide" : "Steps"; onClicked: strip.open = !strip.open }
            PillButton { visible: strip.working; text: "Stop"; onClicked: audioIntel.cancel() }
            PillButton { visible: strip.failed; text: "Continue"; primary: true; onClicked: audioIntel.process(audioIntel.current) }
            PillButton { visible: strip.idle; text: "Process"; primary: true; onClicked: audioIntel.process(audioIntel.current) }
        }
        Rectangle { // the progress, along the bottom edge
            visible: strip.working
            anchors { left: parent.left; bottom: parent.bottom; leftMargin: 1; bottomMargin: 1 }
            height: 2
            radius: 1
            width: Math.max(0, (parent.width - 2) * audioIntel.jobProgress)
            color: theme.accent
            Behavior on width { enabled: activities.animations; NumberAnimation { duration: 200 } }
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
                Rectangle { Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: modelData.state === "done" ? theme.success : modelData.state === "active" ? theme.accent : theme.textFaint }
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
