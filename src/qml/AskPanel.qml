import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Ask your files: a question, and an answer made from the passages found, each with the file and the page (or the recording and the minute) it
// stands on and the quote that proves it. `scopeKind`: all | files | recordings | file | recording (the last two with `scopeRef`).
ColumnLayout {
    id: panel
    property string scopeKind: "all"
    property string scopeRef: ""
    property bool pickScope: true            // the person can choose where to look
    property string hint: "Ask a question about your files and recordings"
    signal openSource(var source)
    spacing: 10
    Layout.fillWidth: true

    readonly property var answer: knowledge.answer
    readonly property bool has: !!answer && answer.question !== undefined
    readonly property string stageText: knowledge.askStage === "understanding" ? "Understanding your question…" : knowledge.askStage === "searching" ? "Looking through your files…"
                                      : knowledge.askStage === "reading" ? "Reading what was found…" : ""

    function submit() {
        if (field.text.trim() === "" || knowledge.asking)
            return
        knowledge.ask(field.text, { kind: panel.scopeKind, ref: panel.scopeRef })
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
    }
    Segmented {
        visible: panel.pickScope && (panel.scopeKind === "all" || panel.scopeKind === "files" || panel.scopeKind === "recordings")
        Layout.fillWidth: true
        model: [["all", "Everything"], ["files", "Files"], ["recordings", "Recordings"]]
        value: panel.scopeKind
        onPicked: (v) => panel.scopeKind = v
    }
    Caption { visible: !knowledge.modelReady; Layout.fillWidth: true; text: "Asking needs the language model: download it in Settings → AI." }
    Caption { visible: knowledge.modelReady && knowledge.stats.docs === 0 && !knowledge.indexing; Layout.fillWidth: true; text: "Nothing is indexed yet. " + knowledge.status }
    Caption { visible: knowledge.indexing; Layout.fillWidth: true; text: knowledge.status + " — what is already indexed can be asked about." }

    // working
    RowLayout {
        visible: knowledge.asking
        spacing: 8
        PulseDot { }
        Text { text: panel.stageText; color: theme.textDim; font.pixelSize: 12 }
    }
    Text { visible: knowledge.askError !== ""; Layout.fillWidth: true; text: knowledge.askError; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 12 }

    // the answer
    Rectangle {
        visible: panel.has && !knowledge.asking
        Layout.fillWidth: true
        implicitHeight: answerBody.implicitHeight + 24
        radius: theme.radiusMd
        color: theme.surface
        border.width: 1
        border.color: panel.answer.notFound ? theme.hairline : Qt.alpha(theme.accent, 0.45)
        ColumnLayout {
            id: answerBody
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
            spacing: 8
            Text { Layout.fillWidth: true; text: panel.has ? panel.answer.question : ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.textDim; font.pixelSize: 11; font.italic: true }
            Text {
                Layout.fillWidth: true
                text: !panel.has ? "" : panel.answer.notFound ? "I could not find this in " + panel.answer.scopeText + "." : panel.answer.text
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: theme.text
                font.pixelSize: 14
                lineHeight: 1.2
            }
            Caption { visible: panel.has && panel.answer.notFound && panel.answer.sources.length > 0; Layout.fillWidth: true; text: "The closest passages:" }
            Text { visible: panel.has && !panel.answer.notFound && panel.answer.figuresUnchecked === true; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.warn; font.pixelSize: 11
                   text: "Check the numbers: some of them are not in the sources below." }
            Caption { visible: panel.has && !panel.answer.notFound; Layout.fillWidth: true; text: "From " + (panel.has ? panel.answer.scopeText : "") + " · " + (panel.has ? panel.answer.seconds.toFixed(1) : "") + " s · check the sources below" }
        }
    }
    Repeater {
        model: panel.has && !knowledge.asking ? panel.answer.sources : []
        delegate: Rectangle {
            id: row
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: src.implicitHeight + 18
            radius: theme.radiusMd
            color: srcHover.hovered ? theme.hoverFill : "transparent"
            border.width: 1
            border.color: theme.hairline
            HoverHandler { id: srcHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: panel.openSource(row.modelData) }
            Accessible.role: Accessible.Button
            Accessible.name: "Open " + row.modelData.name + (row.modelData.where !== "" ? ", " + row.modelData.where : "")
            ColumnLayout {
                id: src
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    LineIcon { glyph: row.modelData.kind === "recording" ? "mic" : "doc"; sourceSize: Qt.size(13, 13); color: theme.textDim }
                    Text { Layout.fillWidth: true; text: row.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Text { visible: row.modelData.where !== ""; text: row.modelData.where; color: theme.accentText; font.pixelSize: 11 }
                }
                Text {
                    Layout.fillWidth: true
                    text: row.modelData.quote !== "" ? "“" + row.modelData.quote + "”" : row.modelData.snippet
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                    color: theme.textSoft
                    font.pixelSize: 11
                    font.italic: row.modelData.quote !== ""
                    lineHeight: 1.15
                }
            }
        }
    }
}
