import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The assistant you talk to: ask about your tasks, recordings and files, say what to do (what changes something waits for Add), or just talk. It is
// the language model of this computer; nothing is sent anywhere.
Item {
    id: page
    required property var shell
    visible: shell.page === "assistant"
    Layout.fillWidth: true
    Layout.maximumWidth: 100000
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    implicitHeight: 500

    readonly property var examples: ["What is due this week?", "Summarize my last recording", "Add a task: send the report tomorrow 9", "What did we decide about the budget?"]
    function rich(text, sources) { // safe rich text: the words are escaped first; **bold** and [n] markers are made after
        let t = String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br>")
        t = t.replace(/\*\*(.+?)\*\*/g, "<b>$1</b>")
        return t.replace(/\[(\d{1,2})\]/g, (m, n) => (sources || []).some(s => String(s.mark) === n) ? '<a href="' + n + '"><b>[' + n + ']</b></a>' : m)
    }
    function openSource(s) {
        if (s.kind === "recording") { page.shell.audioOpen = { id: parseInt(s.ref), sec: s.startSec >= 0 ? s.startSec : 0 }; page.shell.page = "audio" }
        else notch.open(s.ref)
    }
    function send(text) { assistantChat.send(text); field.text = "" }

    Connections { target: assistantChat; function onSerialChanged() { toEnd.restart() } }
    Timer { id: toEnd; interval: 80; onTriggered: { const f = scroll.contentItem; f.contentY = Math.max(0, f.contentHeight - f.height + 12) } }
    Component.onCompleted: toEnd.restart()

    RowLayout {
        id: top
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 32
        Text { Layout.fillWidth: true; text: "Talk to Notch"; color: theme.textDim; font.pixelSize: 12 }
        PillButton { visible: assistantChat.messages.length > 0; text: "Clear"; iconName: "trash"; onClicked: assistantChat.clear() }
    }

    ScrollView {
        id: scroll
        anchors { left: parent.left; right: parent.right; top: top.bottom; bottom: bar.top; bottomMargin: 8 }
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth - 10
            spacing: 10

            FormCard { // the model is not there
                visible: !assistantChat.ready
                Text { text: "The assistant needs the language model"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                Caption { Layout.fillWidth: true; text: "Download it in Settings → AI (Qwen3 4B is enough; the Larger one answers better). Simple commands like “open Firefox” work without it." }
                PillButton { text: "Open Settings → AI"; onClicked: { page.shell.settingsCategory = "ai"; page.shell.page = "settings" } }
            }
            ColumnLayout { // the first time: what to ask
                visible: assistantChat.messages.length === 0
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8
                Text { text: "Ask me about your tasks, recordings and files, or tell me what to do."; color: theme.textDim; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater { model: page.examples; delegate: PillButton { required property string modelData; text: modelData; onClicked: page.send(modelData) } }
                }
            }
            Repeater {
                model: assistantChat.messages
                delegate: ColumnLayout {
                    id: msg
                    required property var modelData
                    required property int index
                    readonly property bool mine: modelData.role === "user"
                    readonly property var action: modelData.action
                    Layout.fillWidth: true
                    spacing: 4
                    Rectangle { // the words
                        visible: msg.modelData.text !== ""
                        Layout.alignment: msg.mine ? Qt.AlignRight : Qt.AlignLeft
                        Layout.preferredWidth: Math.min(bubble.implicitWidth + 24, page.width * 0.86)
                        Layout.preferredHeight: bubble.implicitHeight + 18
                        radius: theme.radiusMd
                        color: msg.modelData.error ? theme.dangerBg : msg.mine ? theme.accentSoft : theme.well
                        border.width: msg.mine ? 0 : 1
                        border.color: theme.hairline
                        Text {
                            id: bubble
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                            textFormat: Text.RichText
                            text: page.rich(msg.modelData.text, msg.modelData.sources)
                            wrapMode: Text.Wrap
                            color: msg.modelData.error ? theme.danger : theme.text
                            font.pixelSize: 13
                            linkColor: theme.accentText
                            onLinkActivated: (n) => { for (const s of msg.modelData.sources || []) if (String(s.mark) === n) page.openSource(s) }
                        }
                    }
                    Flow { // the sources under an answer
                        visible: !msg.mine && (msg.modelData.sources || []).length > 0
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: msg.modelData.sources || []
                            delegate: PillButton { required property var modelData; visible: modelData.mark > 0 || modelData.cited; text: (modelData.mark > 0 ? "[" + modelData.mark + "] " : "") + modelData.name + (modelData.where ? " · " + modelData.where : ""); onClicked: page.openSource(modelData) }
                        }
                    }
                    FormCard { // an action that waits for Add
                        visible: !!msg.action
                        highlight: !!msg.action && msg.action.state === "pending"
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { Layout.fillWidth: true; text: msg.action ? msg.action.text : ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.text; font.pixelSize: 13 }
                            PillButton { visible: !!msg.action && msg.action.state === "pending"; primary: true; text: "Add"; onClicked: assistantChat.approve(msg.index) }
                            PillButton { visible: !!msg.action && msg.action.state === "pending"; text: "Cancel"; onClicked: assistantChat.decline(msg.index) }
                        }
                        Text { visible: !!msg.action && msg.action.state === "done"; text: msg.action ? "✓ " + msg.action.result : ""; color: theme.success; font.pixelSize: 12 }
                        Text { visible: !!msg.action && msg.action.state === "declined"; text: "Not done"; color: theme.textDim; font.pixelSize: 12 }
                    }
                }
            }
            Text { visible: assistantChat.busy; text: assistantChat.stage; color: theme.textDim; font.pixelSize: 12 }
            Item { Layout.preferredHeight: 4 }
        }
    }

    RowLayout {
        id: bar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        spacing: 8
        TextArea {
            id: field
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(96, Math.max(38, contentHeight + 16))
            placeholderText: "Ask, or say what to do…"
            placeholderTextColor: theme.placeholder
            wrapMode: TextEdit.Wrap
            color: theme.text
            font.pixelSize: 13
            selectByMouse: true
            enabled: !assistantChat.busy
            background: Rectangle { radius: theme.radiusMd; color: theme.well; border.width: 1; border.color: field.activeFocus ? theme.accent : theme.hairline }
            Keys.onPressed: (e) => { if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && !(e.modifiers & Qt.ShiftModifier)) { page.send(text); e.accepted = true } }
        }
        PillButton { visible: assistantChat.busy; text: "Stop"; iconName: "stop"; onClicked: assistantChat.cancel() }
        PillButton { visible: !assistantChat.busy; primary: true; text: "Send"; enabled: field.text.trim() !== ""; onClicked: page.send(field.text) }
    }
}
