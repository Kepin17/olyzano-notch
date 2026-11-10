import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Documents: study notes from a PDF. Choose a PDF (one from the shelf, or any file), choose how the notes should be, and a local language model
// writes them; a laid-out PDF and a Markdown file go onto the shelf, and what was made stays in the list below.
Item {
    id: page
    required property var shell
    visible: shell.page === "docs"
    Layout.fillWidth: true
    Layout.maximumWidth: 100000
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    implicitHeight: 500

    property var picked: null          // {kind: "item" | "path", id, path}
    property var info: ({})            // what docSummary.inspect said of it
    property var shelf: []             // the PDFs on the shelf
    property bool dropping: false
    property string style: "study"
    property string length: "medium"
    property string language: "auto"
    property string ocr: "auto"
    property string mode: "notes"       // notes: study notes from a PDF | ask: questions about your files and recordings
    property string figures: vision.ready ? "describe" : "skip" // figures, charts and diagrams: described by the picture model when it is downloaded
    property string firstPage: "1"
    property string lastPage: ""
    readonly property bool ready: audioIntel.readiness.summary
    readonly property bool working: docSummary.busy

    function refresh() { shelf = notch.pdfItems() }
    function adopt(i) {
        info = i
        firstPage = "1"
        lastPage = i.ok ? String(Math.min(i.pages, 150)) : ""
        docSummary.clearError()
    }
    function pickItem(id) { picked = { kind: "item", id: id }; adopt(docSummary.inspect(id)) }
    function pickPath(url) { picked = { kind: "path", path: url }; adopt(docSummary.inspectPath(url)) }
    function clearPick() { picked = null; info = ({}); docSummary.clearError() }
    function begin() {
        const options = { style: style, length: length, language: language, ocr: ocr, figures: figures, firstPage: parseInt(firstPage) || 1, lastPage: parseInt(lastPage) || 0 }
        if (picked.kind === "item") docSummary.start(picked.id, options)
        else docSummary.startPath(picked.path, options)
    }
    function modelOf(id) {
        const list = audioIntel.models
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id)
                return list[i]
        return ({ state: "missing", progress: 0, error: "", sizeText: "", title: id, receivedText: "", speedText: "", etaText: "", partial: false })
    }
    onVisibleChanged: if (visible) { refresh(); pdfTools.refresh() }
    Component.onCompleted: { refresh(); takeRequest() }
    // "Summarize as study notes…" on a file of the shelf comes here with that file
    function takeRequest() {
        if (shell.docsPick === "") return
        const id = shell.docsPick, tab = shell.docsTab, action = shell.docsAction
        shell.docsPick = ""; shell.docsTab = ""; shell.docsAction = ""
        if (tab === "tools") { mode = "tools"; pdfTools.pick(id); if (action !== "" && pdfTools.actions.some(a => a[0] === action)) pdfTools.action = action }
        else pickItem(id)
    }
    Connections { target: shell; function onDocsPickChanged() { page.takeRequest() } }
    Connections { target: docSummary; function onFinished(ok) { page.refresh() } }

    FileDialog {
        id: fileDialog
        onVisibleChanged: page.shell.dialogs += visible ? 1 : -1
        Component.onDestruction: if (visible) page.shell.dialogs -= 1 // a page closed under an open dialog must not leave the notch below the windows
        title: "Choose a PDF"
        fileMode: FileDialog.OpenFile
        nameFilters: ["PDF (*.pdf)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: page.pickPath(selectedFile.toString())
    }

    function openSource(s) {
        if (s.kind === "recording") { shell.audioOpen = { id: parseInt(s.ref), sec: s.startSec }; shell.page = "audio" }
        else notch.open(s.ref)
    }

    AudioTabs {
        id: modeTabs
        anchors { left: parent.left; right: parent.right; top: parent.top; rightMargin: 10 }
        model: [["notes", "Study notes"], ["ask", "Ask your files"], ["tools", "Convert"]]
        value: page.mode
        onPicked: (v) => page.mode = v
    }

    ScrollView {
        id: askScroll
        visible: page.mode === "ask"
        anchors { left: parent.left; right: parent.right; top: modeTabs.bottom; topMargin: 8; bottom: parent.bottom }
        contentWidth: availableWidth
        bottomPadding: 18
        clip: true
        AskPanel {
            width: askScroll.availableWidth - 10
            scopeKind: !!page.picked && page.picked.kind === "item" ? "file" : "all"
            scopeRef: !!page.picked && page.picked.kind === "item" ? page.picked.id : ""
            hint: !!page.picked && page.picked.kind === "item" ? "Ask about " + (page.info.name || "this PDF") : "Ask about your files and recordings"
            onOpenSource: (s) => page.openSource(s)
            onGrew: Qt.callLater(() => { const f = askScroll.contentItem; f.contentY = Math.max(0, f.contentHeight - f.height + 18) })
        }
    }

    ScrollView {
        id: toolsScroll
        visible: page.mode === "tools"
        anchors { left: parent.left; right: parent.right; top: modeTabs.bottom; topMargin: 8; bottom: parent.bottom }
        contentWidth: availableWidth
        bottomPadding: 18
        clip: true
        PdfTools { id: pdfTools; shell: page.shell; width: toolsScroll.availableWidth - 10 }
    }

    ScrollView {
        id: scroll
        visible: page.mode === "notes"
        anchors { left: parent.left; right: parent.right; top: modeTabs.bottom; topMargin: 8; bottom: parent.bottom }
        contentWidth: availableWidth
        bottomPadding: 18 // the panel's rounded edge covers the last pixels of the view
        clip: true
        ColumnLayout {
            id: col
            width: scroll.availableWidth - 10
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 0
                    Text { text: "Documents"; color: theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text { text: "Study notes from a PDF, made on this computer"; color: theme.textDim; font.pixelSize: 11 }
                }
                Item { Layout.fillWidth: true }
                PillButton { visible: !page.working && !!page.picked && page.picked.kind === "item" && page.mode === "notes"; text: taskSuggest.busy ? "Looking…" : "Find tasks"; iconName: "check"; enabled: !taskSuggest.busy
                    onClicked: taskSuggest.findInPdf(page.picked.id); Accessible.description: "Find deadlines and things to do in this PDF and offer them on the Tasks page" }
                PillButton { visible: !page.working && !!page.picked; text: "Another PDF"; onClicked: page.clearPick() }
                PillButton { visible: !page.working && !page.picked; text: "Choose a PDF…"; iconName: "upload"; primary: true; onClicked: fileDialog.open() }
            }

            Caption { visible: page.mode === "notes" && taskSuggest.status !== ""; Layout.fillWidth: true; text: taskSuggest.status + (taskSuggest.count > 0 ? " See them on the Tasks page." : "") }

            // ---------------- working ----------------
            Rectangle {
                visible: page.working
                Layout.fillWidth: true
                implicitHeight: workBody.implicitHeight + 28
                radius: theme.radiusMd
                color: theme.well
                border.width: 1
                border.color: theme.hairline
                ColumnLayout {
                    id: workBody
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                    spacing: 8
                    Text { Layout.fillWidth: true; text: docSummary.fileName; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        BusyIndicator { running: page.working; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                        Text { Layout.fillWidth: true; text: docSummary.stageText + " · " + Math.round(docSummary.progress * 100) + "%"; color: theme.text; font.pixelSize: 12 }
                        PillButton { text: "Stop"; onClicked: docSummary.cancel() }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 4; radius: 2; color: theme.surface
                        Rectangle { width: parent.width * docSummary.progress; height: parent.height; radius: 2; color: theme.accent; Behavior on width { enabled: activities.animations; NumberAnimation { duration: 200 } } } }
                    Caption { Layout.fillWidth: true; text: "This takes a few minutes. You can leave this page: the notes appear on your shelf when they are ready." }
                }
            }

            // ---------------- messages ----------------
            Rectangle {
                visible: !page.working && docSummary.lastError !== ""
                Layout.fillWidth: true
                implicitHeight: errText.implicitHeight + 18
                radius: theme.radiusSm + 2
                color: theme.dangerBg
                Text { id: errText; anchors { fill: parent; margins: 9 } wrapMode: Text.Wrap; textFormat: Text.PlainText; color: theme.danger; font.pixelSize: 12; text: docSummary.lastError }
            }
            Text {
                visible: !page.working && docSummary.warnings.length > 0 && docSummary.lastError === ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: theme.warn
                font.pixelSize: 11
                text: docSummary.warnings.join("\n")
            }

            // ---------------- choose a PDF ----------------
            Rectangle { // drop zone
                visible: !page.working && !page.picked
                Layout.fillWidth: true
                implicitHeight: 96
                radius: theme.radiusMd
                color: page.dropping ? Qt.alpha(theme.accent, 0.1) : "transparent"
                border.width: 1
                border.color: page.dropping ? theme.accent : theme.hairline
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 6
                    Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 34; Layout.preferredHeight: 34; radius: 17; color: Qt.alpha(theme.accent, 0.15)
                        LineIcon { anchors.centerIn: parent; glyph: "doc"; sourceSize: Qt.size(17, 17); color: theme.accent } }
                    Text { Layout.alignment: Qt.AlignHCenter; text: "Drop a PDF here, or choose one"; color: theme.text; font.pixelSize: 13 }
                    Caption { Layout.alignment: Qt.AlignHCenter; text: "Scanned pages are read with OCR" }
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: fileDialog.open() }
            }
            ColumnLayout {
                visible: !page.working && !page.picked && page.shelf.length > 0
                Layout.fillWidth: true
                spacing: 6
                Text { text: "PDFS ON YOUR SHELF"; color: theme.textDim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
                Repeater {
                    model: page.shelf
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: rowBody.implicitHeight + 16
                        radius: theme.radiusSm + 2
                        color: rowHover.hovered ? theme.hoverFill : theme.well
                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: page.pickItem(row.modelData.id) }
                        RowLayout {
                            id: rowBody
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 10 }
                            spacing: 10
                            LineIcon { glyph: "doc"; sourceSize: Qt.size(16, 16); color: theme.textDim }
                            Text { Layout.fillWidth: true; text: row.modelData.name; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.text; font.pixelSize: 12 }
                            Text { text: row.modelData.sizeText + " · " + row.modelData.addedText; color: theme.textDim; font.pixelSize: 11 }
                            LineIcon { glyph: "chevron"; sourceSize: Qt.size(11, 11); color: theme.textFaint; opacity: rowHover.hovered ? 1 : 0 }
                        }
                    }
                }
            }

            // ---------------- the choices ----------------
            Rectangle {
                visible: !page.working && !!page.picked
                Layout.fillWidth: true
                implicitHeight: opts.implicitHeight + 28
                radius: theme.radiusMd
                color: theme.well
                border.width: 1
                border.color: theme.hairline
                ColumnLayout {
                    id: opts
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        LineIcon { glyph: "doc"; sourceSize: Qt.size(20, 20); color: theme.accent }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text { Layout.fillWidth: true; text: page.info.name || ""; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; visible: !!page.info.ok; color: theme.textDim; font.pixelSize: 11; text: page.info.pages + (page.info.pages === 1 ? " page" : " pages") + " · about " + page.info.estimateMinutes + " min on this computer" + (page.info.pictureShare > 0.4 ? " · mostly pictures, read by OCR (" + page.info.ocrLanguages + ")" : "") }
                        }
                    }
                    Text { visible: !page.info.ok; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 12; text: page.info.warning || "" }
                    ColumnLayout {
                        visible: !!page.info.ok
                        Layout.fillWidth: true
                        spacing: 10
                        Text { visible: !!page.info.warning; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.warn; font.pixelSize: 11; text: page.info.warning || "" }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Caption { text: "Notes" }
                            Segmented { Layout.fillWidth: true; model: [["study", "Study notes"], ["brief", "Brief"], ["cards", "Flash cards"], ["cheat", "Cheat sheet"]]; value: page.style; onPicked: (v) => page.style = v } }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 520 ? 2 : 1
                            columnSpacing: 16
                            rowSpacing: 10
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Caption { text: "Length" }
                                Segmented { Layout.fillWidth: true; model: [["short", "Short"], ["medium", "Medium"], ["long", "Detailed"]]; value: page.length; onPicked: (v) => page.length = v } }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Caption { text: "Language" }
                                LanguagePicker { value: page.language; onPicked: (c) => page.language = c; onMoreRequested: { page.shell.settingsCategory = "languages"; page.shell.page = "settings" } } }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Caption { text: "Pages that are pictures" }
                                Segmented { Layout.fillWidth: true; model: [["auto", "Read them"], ["always", "Read all"], ["never", "Skip"]]; value: page.ocr; onPicked: (v) => page.ocr = v } }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                visible: !!page.info.ok && (page.info.figurePages > 0 || !vision.ready)
                                Caption { text: "Figures, charts and diagrams" }
                                Segmented { Layout.fillWidth: true; enabled: vision.ready; model: [["skip", "Skip"], ["describe", "Describe (AI)"]]; value: vision.ready ? page.figures : "skip"; onPicked: (v) => page.figures = v }
                                Caption { Layout.fillWidth: true; visible: vision.ready && page.figures === "describe" && page.info.figurePages > 0
                                          text: page.info.figurePages + (page.info.figurePages === 1 ? " page has" : " pages have") + " figures: the picture model looks at them first (about " + Math.ceil(page.info.figurePages * 1.2) + " min more on the processor)" }
                                Caption { Layout.fillWidth: true; visible: !vision.ready; text: "Download the picture model in Settings → AI to have charts and diagrams described." } }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Caption { text: "Pages" }
                                RowLayout {
                                    spacing: 6
                                    TextField { id: fromField; Layout.preferredWidth: 64; implicitHeight: 30; horizontalAlignment: Text.AlignHCenter; text: page.firstPage; inputMethodHints: Qt.ImhDigitsOnly; color: theme.text; selectionColor: theme.accent; font.pixelSize: 12
                                        background: Rectangle { radius: 15; color: theme.surface; border.width: 1; border.color: fromField.activeFocus ? theme.accent : theme.outline }
                                        onTextEdited: page.firstPage = text }
                                    Text { text: "–"; color: theme.textDim }
                                    TextField { id: toField; Layout.preferredWidth: 64; implicitHeight: 30; horizontalAlignment: Text.AlignHCenter; text: page.lastPage; inputMethodHints: Qt.ImhDigitsOnly; color: theme.text; selectionColor: theme.accent; font.pixelSize: 12
                                        background: Rectangle { radius: 15; color: theme.surface; border.width: 1; border.color: toField.activeFocus ? theme.accent : theme.outline }
                                        onTextEdited: page.lastPage = text }
                                }
                            }
                        }
                        // what the notes need: the language model and its program
                        ColumnLayout {
                            visible: !page.ready && audioIntel.readiness.summaryMissing.length > 0
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.hairline }
                            Text { text: "Two downloads first"; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                            Caption { Layout.fillWidth: true; text: "The language model that writes the notes, and the small program that runs it (the same ones as the notes of recordings). Both stay on this computer." }
                            Repeater {
                                model: audioIntel.readiness.summaryMissing
                                delegate: ColumnLayout {
                                    id: nrow
                                    required property string modelData
                                    readonly property var info: { audioIntel.models; return page.modelOf(modelData) }
                                    Layout.fillWidth: true
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Text { Layout.fillWidth: true; text: nrow.info.title + " · " + nrow.info.sizeText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 12 }
                                        PillButton { visible: nrow.info.state !== "downloading" && nrow.info.state !== "verifying"; text: nrow.info.partial ? "Continue" : "Download"; iconName: "download"; primary: true; onClicked: audioIntel.downloadModel(nrow.modelData) }
                                        PillButton { visible: nrow.info.state === "downloading"; text: "Cancel"; onClicked: audioIntel.cancelDownload(nrow.modelData) }
                                    }
                                    Rectangle { visible: nrow.info.state === "downloading"; Layout.fillWidth: true; Layout.preferredHeight: 3; radius: 1.5; color: theme.surface
                                        Rectangle { width: parent.width * nrow.info.progress; height: parent.height; radius: 1.5; color: theme.accent } }
                                    Caption { visible: nrow.info.state === "downloading"; Layout.fillWidth: true; text: nrow.info.receivedText + (nrow.info.speedText !== "" ? " · " + nrow.info.speedText : "") + (nrow.info.etaText !== "" ? " · " + nrow.info.etaText : "") }
                                    Text { visible: nrow.info.error !== ""; Layout.fillWidth: true; text: nrow.info.error; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 11 }
                                }
                            }
                        }
                        Caption { Layout.fillWidth: true; text: "Nothing is sent anywhere. The notes only say what the document says; they can miss what is in pictures, tables and formulas." }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            PillButton { text: "Make the notes"; primary: true; enabled: page.ready; onClicked: page.begin() }
                        }
                    }
                }
            }

            // ---------------- what was made ----------------
            ColumnLayout {
                visible: !page.working && docSummary.history.length > 0
                Layout.fillWidth: true
                spacing: 6
                Text { text: "RECENT NOTES"; color: theme.textDim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
                Repeater {
                    model: docSummary.history
                    delegate: Rectangle {
                        id: made
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: madeBody.implicitHeight + 18
                        radius: theme.radiusSm + 2
                        color: theme.well
                        RowLayout {
                            id: madeBody
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 8 }
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text { Layout.fillWidth: true; text: made.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                                Text { Layout.fillWidth: true; color: theme.textDim; font.pixelSize: 11; elide: Text.ElideRight
                                    text: made.modelData.whenText + " · " + made.modelData.pages + (made.modelData.pages === 1 ? " page" : " pages") + " · " + made.modelData.style + (made.modelData.language ? " · " + made.modelData.language.toUpperCase() : "") }
                            }
                            PillButton { visible: made.modelData.pdfThere; text: "PDF"; iconName: "doc"; onClicked: notch.open(made.modelData.pdfId) }
                            PillButton { visible: made.modelData.mdThere; text: "Markdown"; onClicked: notch.open(made.modelData.mdId) }
                            Text { visible: !made.modelData.pdfThere && !made.modelData.mdThere; text: "No longer on the shelf"; color: theme.textFaint; font.pixelSize: 11 }
                            IconButton { iconName: "close"; implicitWidth: 24; implicitHeight: 24; Accessible.name: "Remove from the list"; onClicked: docSummary.removeHistory(made.index) }
                        }
                    }
                }
            }
            Item { Layout.preferredHeight: 4 }
        }
    }

    DropArea { // a PDF dropped anywhere on the page
        anchors.fill: parent
        onEntered: (d) => { if (d.hasUrls) page.dropping = true }
        onExited: page.dropping = false
        onDropped: (d) => {
            page.dropping = false
            if (d.hasUrls && d.urls.length > 0 && !page.working)
                page.pickPath(d.urls[0].toString())
        }
    }
}
