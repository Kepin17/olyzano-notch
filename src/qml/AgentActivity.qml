import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One action of the agent as one line ("Edited  src/a.ts  +12 −3"). The diff, command or output open on a click and are built
// only while open, so a long task costs a line per action. A call that waits for Apply / Run opens by itself, so it is never
// approved unseen, and its buttons sit on the line.
Rectangle {
    id: act
    property var call: ({})
    property string msgId: ""
    property int userOpen: -1 // -1: follow the call (open while it waits for a decision), 0: closed, 1: open
    readonly property bool waiting: call.status === "awaiting_approval"
    readonly property bool expanded: userOpen >= 0 ? userOpen === 1 : waiting
    readonly property bool isCommand: call.name === "run_command" || !!call.command
    readonly property bool hasDetail: !!(call.diffText || call.output || call.command || call.summary)

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + 10
    radius: theme.radiusSm
    color: expanded ? theme.surface : "transparent"
    border.width: waiting || expanded ? 1 : 0
    border.color: waiting ? Qt.alpha(theme.accent, 0.6) : theme.hairline

    component Mono: TextArea {
        Layout.fillWidth: true
        implicitHeight: contentHeight + topPadding + bottomPadding
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.PlainText
        color: theme.text
        font.family: "monospace"
        font.pixelSize: 12
        padding: 0
        background: null
    }

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 5; leftMargin: 8; rightMargin: 8 }
        spacing: 6
        RowLayout { // the line
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            spacing: 8
            MouseArea {
                Layout.fillWidth: true
                Layout.fillHeight: true
                enabled: act.hasDetail
                cursorShape: act.hasDetail ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: act.userOpen = act.expanded ? 0 : 1
                RowLayout {
                    anchors.fill: parent
                    spacing: 8
                    LineIcon { glyph: act.call.icon || "gear"; sourceSize: Qt.size(14, 14); color: act.call.tone === "bad" ? "#ff453a" : act.call.tone === "ok" ? "#30d158" : theme.textDim }
                    Text { text: act.call.verb || act.call.name || ""; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                    Text {
                        Layout.fillWidth: true
                        text: act.call.target || ""
                        color: theme.textDim
                        font.pixelSize: 12
                        font.family: act.isCommand ? "monospace" : Qt.application.font.family
                        elide: Text.ElideMiddle
                    }
                    AgentBadge { visible: !!act.call.stat; text: act.call.stat || ""; tone: act.call.tone || "neutral"; mono: true }
                    AgentBadge { visible: act.call.status === "running"; text: "Running…"; tone: "accent" }
                    Text { visible: act.hasDetail; text: act.expanded ? "⌃" : "⌄"; color: theme.textFaint; font.pixelSize: 11 }
                }
            }
            PillButton { // waiting for a decision: on the line, so it works even when the details are folded
                visible: act.waiting
                text: act.isCommand ? "Run" : "Apply"
                checked: true
                implicitHeight: 24
                enabled: !codingAgent.busy && codingAgent.mode !== "plan"
                onClicked: codingAgent.approveToolCall(act.msgId, act.call.id)
            }
            PillButton {
                visible: act.waiting
                text: "Reject"
                implicitHeight: 24
                enabled: !codingAgent.busy
                onClicked: codingAgent.rejectToolCall(act.msgId, act.call.id)
            }
        }
        Loader { // built only while open
            Layout.fillWidth: true
            active: act.expanded
            visible: active
            sourceComponent: ColumnLayout {
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    visible: act.waiting
                    text: act.isCommand ? "Unsandboxed command — review before running." : "Review the change before applying."
                    color: theme.textDim
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
                Text { Layout.fillWidth: true; visible: !!act.call.summary && !act.isCommand; text: act.call.summary || ""; color: theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap }
                Mono { visible: !!text; text: act.call.command || "" }
                Mono { visible: !!text; text: act.call.diffText || "" }
                Mono { visible: !!text; text: act.call.output || "" }
                PillButton {
                    visible: (act.call.output || "").startsWith("[…")
                    text: "Copy all output"
                    iconName: "copy"
                    implicitHeight: 24
                    onClicked: { notch.copyText(codingAgent.fullText(act.msgId, act.call.id)) }
                }
                Item { Layout.preferredHeight: 4 }
            }
        }
    }
}
