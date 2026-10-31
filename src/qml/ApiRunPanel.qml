import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Run a collection or folder: every request one after the other, each judged by its tests (or its status). The list fills in as it goes,
// the line at the bottom says how many passed, and a click on a row shows what that request got back.
Popup {
    id: run
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 40 : 640, 680)
    height: 440
    padding: 14
    property string targetId: ""
    property string targetName: ""
    property bool stopOnFailure: false
    property int picked: -1
    readonly property var sum: apiTester.runSummary
    readonly property var items: apiTester.runItems
    readonly property var shown: picked >= 0 ? apiTester.runResponse(picked) : ({})

    background: Rectangle { radius: theme.radiusMd; color: theme.card; border.width: 1; border.color: theme.outline }

    function start() {
        picked = -1
        apiTester.runCollection(targetId, stopOnFailure)
    }
    onClosed: apiTester.cancelRun()

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: "Run “" + run.targetName + "”"
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            PillButton {
                text: "Stop at the first failure"
                checked: run.stopOnFailure
                enabled: !apiTester.running
                onClicked: run.stopOnFailure = !run.stopOnFailure
            }
        }

        Rectangle { // progress
            Layout.fillWidth: true
            implicitHeight: 4
            radius: 2
            color: theme.well
            Rectangle {
                height: parent.height
                radius: 2
                width: parent.width * (run.sum.total > 0 ? (run.sum.passed + run.sum.failed) / run.sum.total : 0)
                color: run.sum.failed > 0 ? theme.danger : theme.success
                Behavior on width { enabled: activities.animations; NumberAnimation { duration: 140 } }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 150
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: run.items
            ScrollBar.vertical: ScrollBar { padding: 2 }
            delegate: Rectangle {
                required property int index
                required property var modelData
                width: list.width - 8
                height: 30
                radius: theme.radiusSm
                color: run.picked === index ? Qt.alpha(theme.accent, 0.14) : rowHov.hovered ? theme.hoverFill : "transparent"
                HoverHandler { id: rowHov; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: run.picked = index }
                RowLayout {
                    anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                    spacing: 8
                    Text { // the result
                        Layout.preferredWidth: 14
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.state === "pass" ? "✓" : modelData.state === "fail" ? "✗" : modelData.state === "running" ? "…" : "·"
                        color: modelData.state === "pass" ? theme.success : modelData.state === "fail" ? theme.danger : theme.textDim
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                    Text { text: modelData.method; color: theme.textDim; font.pixelSize: 10; font.weight: Font.Bold; Layout.preferredWidth: 46 }
                    Text { Layout.fillWidth: true; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 12 }
                    Text {
                        visible: modelData.testsTotal > 0
                        text: modelData.testsPassed + "/" + modelData.testsTotal + " tests"
                        color: modelData.testsPassed < modelData.testsTotal ? theme.danger : theme.textDim
                        font.pixelSize: 10
                    }
                    Text {
                        visible: modelData.state === "pass" || modelData.state === "fail"
                        text: modelData.error !== "" ? "error" : String(modelData.status)
                        color: modelData.state === "fail" ? theme.danger : theme.textSoft
                        font.family: "monospace"
                        font.pixelSize: 11
                        Layout.preferredWidth: 38
                        horizontalAlignment: Text.AlignRight
                    }
                    Text {
                        visible: modelData.durationMs > 0
                        text: modelData.durationMs + " ms"
                        color: theme.textFaint
                        font.family: "monospace"
                        font.pixelSize: 10
                        Layout.preferredWidth: 54
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }

        Rectangle { // what the chosen request got back
            visible: run.picked >= 0
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            radius: 8
            color: theme.well
            border.width: 1
            border.color: theme.hairline
            ScrollView {
                anchors { fill: parent; margins: 8 }
                contentWidth: availableWidth
                clip: true
                Text {
                    width: parent.availableWidth
                    text: run.shown.error ? run.shown.error : ((run.shown.status || "") + " " + (run.shown.statusText || "") + "\n" + (run.shown.bodyPretty || ""))
                    textFormat: Text.PlainText
                    color: run.shown.error ? theme.danger : theme.textSoft
                    font.family: "monospace"
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                color: run.sum.failed > 0 ? theme.danger : run.sum.pending === 0 && run.sum.total > 0 ? theme.success : theme.textDim
                font.pixelSize: 12
                font.weight: Font.DemiBold
                text: run.sum.total === 0 ? ""
                      : apiTester.running ? ("Running… " + (run.sum.passed + run.sum.failed) + " of " + run.sum.total)
                      : run.sum.passed + " passed · " + run.sum.failed + " failed" + (run.sum.pending > 0 ? " · " + run.sum.pending + " not run" : "")
                        + " · " + (run.sum.totalMs / 1000).toFixed(1) + " s"
            }
            PillButton { visible: apiTester.running; text: "Stop"; iconName: "stop"; onClicked: apiTester.cancelRun() }
            PillButton { visible: !apiTester.running; text: "Run again"; iconName: "play"; onClicked: run.start() }
            PillButton { text: "Close"; checked: true; onClicked: run.close() }
        }
    }
}
