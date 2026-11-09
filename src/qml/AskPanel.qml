import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Ask your files, as a conversation: your question on the right, the reply on the left in the language you wrote in, with a marker [1] after each claim
// and the sources under it (the file and the page, or the recording and the minute, with the quote that proves it). A marker or a source opens the
// file at that place. `scopeKind`: all | files | recordings | file | recording (the last two with `scopeRef`).
ColumnLayout {
    id: panel
    property string scopeKind: "all"
    property string scopeRef: ""
    property bool pickScope: true            // the person can choose where to look
    property string hint: "Ask a question about your files and recordings"
    property var messages: []                // {role: "user" | "assistant", text, ...}
    signal openSource(var source)
    signal grew()                            // something was added: a page that scrolls goes to the end
    spacing: 10
    Layout.fillWidth: true

    readonly property string stageText: knowledge.askStage === "understanding" ? "Understanding your question…" : knowledge.askStage === "searching" ? "Looking through your files…"
                                      : knowledge.askStage === "reading" ? "Reading what was found…" : ""

    function add(m) { messages = messages.concat([m]); growTimer.restart() }
    Timer { id: growTimer; interval: 90; onTriggered: panel.grew() } // after the new bubble has its height: a page that scrolls goes to the end
    function submit() {
        const q = field.text.trim()
        if (q === "" || knowledge.asking)
            return
        add({ role: "user", text: q })
        field.text = ""
        knowledge.ask(q, { kind: panel.scopeKind, ref: panel.scopeRef })
    }
    // the text of a reply made safe for rich text, with each [n] a link to its source
    function rich(text) {
        const esc = text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br>")
        return esc.replace(/\[(\d{1,2})\]/g, '<a href="$1"><b>[$1]</b></a>')
    }
    function sourceOf(msg, mark) {
        for (const s of msg.sources || [])
            if (s.mark === mark)
                return s
        return null
    }

    property int seenReplies: 0 // the serial of the last reply shown (not bound: it is set when one is added)
    Component.onCompleted: seenReplies = knowledge.answerSerial
    Connections {
        target: knowledge
        // every change of the controller says "changed": a reply is new when its serial is
        function onAnswerSerialChanged() {
            if (knowledge.answerSerial === panel.seenReplies)
                return
            panel.seenReplies = knowledge.answerSerial
            if (knowledge.askError !== "") {
                panel.add({ role: "assistant", text: knowledge.askError, error: true, sources: [] })
                return
            }
            const a = knowledge.answer
            if (a && a.question !== undefined)
                panel.add({ role: "assistant", text: a.notFound ? a.notFoundText : a.text, notFound: a.notFound, sources: JSON.parse(JSON.stringify(a.sources)) /* a copy: the controller's list is replaced by the next answer */, figures: a.figuresUnchecked === true,
                            notFoundText: a.notFoundText, closestText: a.closestText, checkText: a.checkNumbersText, seconds: a.seconds })
        }
    }

    Segmented {
        visible: panel.pickScope && panel.messages.length === 0 && (panel.scopeKind === "all" || panel.scopeKind === "files" || panel.scopeKind === "recordings")
        Layout.fillWidth: true
        model: [["all", "Everything"], ["files", "Files"], ["recordings", "Recordings"]]
        value: panel.scopeKind
        onPicked: (v) => panel.scopeKind = v
    }
    Caption { visible: !knowledge.modelReady; Layout.fillWidth: true; text: "Asking needs the language model: download it in Settings → AI." }
    Caption { visible: knowledge.modelReady && knowledge.stats.docs === 0 && !knowledge.indexing; Layout.fillWidth: true; text: "Nothing is indexed yet. " + knowledge.status }
    Caption { visible: knowledge.indexing; Layout.fillWidth: true; text: knowledge.status + " — what is already indexed can be asked about." }
    Text {
        visible: panel.messages.length === 0 && knowledge.modelReady
        Layout.fillWidth: true
        Layout.topMargin: 6
        wrapMode: Text.Wrap
        color: theme.textDim
        font.pixelSize: 12
        text: "Ask in your own words. The reply comes in the language you write in, with the file, the page or the minute it comes from."
    }

    // the conversation
    Repeater {
        model: panel.messages
        delegate: Item {
            id: msgRow
            required property var modelData
            readonly property bool mine: modelData.role === "user"
            Layout.fillWidth: true
            implicitHeight: bubble.implicitHeight
            Rectangle {
                id: bubble
                x: msgRow.mine ? parent.width - width : 0
                width: msgRow.mine ? Math.min(parent.width * 0.8, Math.max(120, col.implicitWidth + 24))
                                    : (msgRow.modelData.sources || []).length > 0 ? parent.width * 0.96 : Math.min(parent.width * 0.96, Math.max(120, col.implicitWidth + 24))
                implicitHeight: col.implicitHeight + 20
                height: implicitHeight
                radius: theme.radiusMd
                color: msgRow.mine ? Qt.alpha(theme.accent, 0.16) : theme.surface
                border.width: 1
                border.color: msgRow.mine ? Qt.alpha(theme.accent, 0.4) : msgRow.modelData.error ? Qt.alpha(theme.danger, 0.5) : theme.hairline
                ColumnLayout {
                    id: col
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12; topMargin: 10 }
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: msgRow.mine ? msgRow.modelData.text : panel.rich(msgRow.modelData.text)
                        textFormat: msgRow.mine ? Text.PlainText : Text.StyledText
                        wrapMode: Text.Wrap
                        color: msgRow.modelData.error ? theme.danger : theme.text
                        linkColor: theme.accentText
                        font.pixelSize: 14
                        lineHeight: 1.2
                        onLinkActivated: (link) => { const s = panel.sourceOf(msgRow.modelData, parseInt(link)); if (s) panel.openSource(s) }
                        HoverHandler { cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    }
                    Text { visible: !msgRow.mine && msgRow.modelData.figures === true; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.warn; font.pixelSize: 11; text: msgRow.modelData.checkText || "" }
                    Text { visible: !msgRow.mine && msgRow.modelData.notFound === true && (msgRow.modelData.sources || []).length > 0; text: msgRow.modelData.closestText || ""; color: theme.textDim; font.pixelSize: 11 }
                    // the sources, under the reply
                    Repeater {
                        model: msgRow.mine ? [] : (msgRow.modelData.sources || [])
                        delegate: Rectangle {
                            id: srcRow
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: srcCol.implicitHeight + 12
                            radius: theme.radiusSm
                            color: srcHover.hovered ? theme.hoverFill : Qt.alpha(theme.text, 0.04)
                            HoverHandler { id: srcHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: panel.openSource(srcRow.modelData) }
                            Accessible.role: Accessible.Button
                            Accessible.name: "Open " + srcRow.modelData.name + (srcRow.modelData.where !== "" ? ", " + srcRow.modelData.where : "")
                            ColumnLayout {
                                id: srcCol
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 8 }
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { visible: srcRow.modelData.mark > 0; text: "[" + srcRow.modelData.mark + "]"; color: theme.accentText; font.pixelSize: 11; font.weight: Font.DemiBold }
                                    LineIcon { glyph: srcRow.modelData.kind === "recording" ? "mic" : "doc"; sourceSize: Qt.size(12, 12); color: theme.textDim }
                                    Text { Layout.fillWidth: true; text: srcRow.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                                    Text { visible: srcRow.modelData.where !== ""; text: srcRow.modelData.where; color: theme.accentText; font.pixelSize: 10 }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: srcRow.modelData.quote !== "" ? "“" + srcRow.modelData.quote + "”" : srcRow.modelData.snippet
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                    color: theme.textSoft
                                    font.pixelSize: 10
                                    font.italic: srcRow.modelData.quote !== ""
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // the reply is being made
    Rectangle {
        visible: knowledge.asking
        Layout.preferredWidth: typing.implicitWidth + 28
        implicitHeight: typing.implicitHeight + 20
        radius: theme.radiusMd
        color: theme.surface
        border.width: 1
        border.color: theme.hairline
        RowLayout {
            id: typing
            anchors.centerIn: parent
            spacing: 8
            PulseDot { }
            Text { text: panel.stageText; color: theme.textDim; font.pixelSize: 12 }
        }
    }

    // the question
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        TextField {
            id: field
            Layout.fillWidth: true
            implicitHeight: 34
            leftPadding: 12
            rightPadding: 12
            placeholderText: panel.hint
            placeholderTextColor: theme.placeholder
            color: theme.text
            selectionColor: theme.accent
            font.pixelSize: 13
            enabled: knowledge.modelReady
            background: Rectangle { radius: 17; color: theme.well; border.width: 1; border.color: field.activeFocus ? theme.accent : theme.hairline }
            onAccepted: panel.submit()
        }
        PillButton { text: knowledge.asking ? "Stop" : "Ask"; iconName: knowledge.asking ? "" : "ai"; checked: !knowledge.asking; enabled: knowledge.modelReady || knowledge.asking
            onClicked: knowledge.asking ? knowledge.cancelAsk() : panel.submit() }
        PillButton { visible: panel.messages.length > 0 && !knowledge.asking; text: "New"; onClicked: { panel.messages = []; knowledge.clearAnswer() } }
    }
}
