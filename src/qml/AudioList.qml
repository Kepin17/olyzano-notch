import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The recordings: record or import, what is being worked on, the list (or the first steps when there is none), and the history.
ScrollView {
    id: view
    required property var page
    contentWidth: availableWidth
    bottomPadding: 18 // the panel's rounded edge covers the last pixels of the view
    clip: true
    readonly property bool importing: !!page.pending.path || page.pendingMany.length > 0
    readonly property bool ready: audioIntel.readiness.asr
    readonly property var speech: page.modelOf("whisper-small")

    ColumnLayout {
        id: col
        width: view.availableWidth - 10
        spacing: 12

        // ---------------- top: the title and the two ways in ----------------
        RowLayout {
            visible: !view.importing
            Layout.fillWidth: true
            spacing: 8
            ColumnLayout {
                spacing: 0
                Text { text: "Recordings"; color: theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text { text: "Transcripts and notes, made on this computer"; color: theme.textDim; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            PillButton { visible: meetingList.count > 0 || page.showHistory; text: "History"; checked: page.showHistory; onClicked: page.showHistory = !page.showHistory }
            PillButton { text: "Import"; iconName: "upload"; onClicked: page.openFiles() }
            PillButton { text: "Live"; iconName: "mic"; enabled: !audioIntel.recording; onClicked: audioIntel.startLive(); Accessible.description: "Record and transcribe while the meeting happens" }
            PillButton { text: "Record"; iconName: "mic"; checked: true; enabled: !audioIntel.recording; onClicked: audioIntel.startRecording() }
        }

        // ---------------- an error ----------------
        Rectangle {
            visible: audioIntel.lastError !== ""
            Layout.fillWidth: true
            implicitHeight: errText.implicitHeight + 18
            radius: theme.radiusSm + 2
            color: theme.dangerBg
            Text { id: errText; anchors { fill: parent; margins: 9; rightMargin: 36 } wrapMode: Text.Wrap; textFormat: Text.PlainText; color: theme.danger; font.pixelSize: 12; text: audioIntel.lastError }
            IconButton { anchors { right: parent.right; top: parent.top; margins: 4 } iconName: "close"; Accessible.name: "Dismiss"; onClicked: audioIntel.clearError() }
        }

        // ---------------- importing: one file, or several one after the other ----------------
        Rectangle {
            visible: view.importing
            Layout.fillWidth: true
            implicitHeight: importBody.implicitHeight + 28
            radius: theme.radiusMd
            color: theme.well
            border.width: 1
            border.color: theme.hairline
            ColumnLayout {
                id: importBody
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: page.pendingMany.length > 0 ? page.pendingMany.length + " recordings, one after the other" : "Import a recording"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                    IconButton { iconName: "close"; Accessible.name: "Cancel"; onClicked: { page.pending = ({}); page.pendingMany = [] } }
                }
                Text { // one file: what it is
                    visible: page.pendingMany.length === 0
                    Layout.fillWidth: true
                    color: theme.textDim
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    text: (page.pending.name || "") + (page.pending.sizeText ? " · " + page.pending.sizeText : "") + (page.pending.durationText ? " · " + page.pending.durationText : "") + (page.pending.memoryText ? " · uses " + page.pending.memoryText : "")
                }
                Text { visible: !!page.pending.warning; Layout.fillWidth: true; color: theme.danger; font.pixelSize: 12; wrapMode: Text.Wrap; text: page.pending.warning || "" }
                TextField {
                    id: titleField
                    visible: page.pendingMany.length === 0
                    Layout.fillWidth: true
                    implicitHeight: 32
                    leftPadding: 12
                    text: page.pendingTitle
                    placeholderText: "Title"
                    placeholderTextColor: theme.placeholder
                    color: theme.text
                    selectionColor: theme.accent
                    font.pixelSize: 13
                    maximumLength: 120
                    background: Rectangle { radius: 16; color: theme.surface; border.width: 1; border.color: titleField.activeFocus ? theme.accent : theme.outline }
                    onTextEdited: page.pendingTitle = text
                }
                ListView { // several files: which
                    visible: page.pendingMany.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 120)
                    clip: true
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    model: page.pendingMany
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width - 8
                        spacing: 8
                        Text { Layout.fillWidth: true; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: modelData.ok ? theme.text : theme.textFaint; font.pixelSize: 12 }
                        Text { text: modelData.ok ? (modelData.durationText || "") : "skipped"; color: modelData.ok ? theme.textDim : theme.danger; font.pixelSize: 11 }
                    }
                }
                GridLayout { // the options, two by two when there is room
                    Layout.fillWidth: true
                    columns: width > 520 ? 2 : 1
                    columnSpacing: 16
                    rowSpacing: 8
                    ColumnLayout { Layout.fillWidth: true; spacing: 4
                        Caption { text: "Language" }
                        LanguagePicker { value: page.pendingLanguage; onPicked: (c) => page.pendingLanguage = c; onMoreRequested: { page.shell.settingsCategory = "languages"; page.shell.page = "settings" } } }
                    ColumnLayout { Layout.fillWidth: true; spacing: 4
                        Caption { text: "Quality" }
                        Segmented { Layout.fillWidth: true; model: [["standard", "Standard"], ["high", "High quality"]]; value: page.pendingQuality; onPicked: (v) => page.pendingQuality = v } }
                    ColumnLayout { Layout.fillWidth: true; spacing: 4
                        Caption { text: "People speaking" }
                        Segmented { Layout.fillWidth: true; model: page.speakerCounts; value: page.pendingSpeakers; onPicked: (v) => page.pendingSpeakers = v } }
                    ColumnLayout { Layout.fillWidth: true; spacing: 4
                        Caption { text: "Notes style" }
                        Segmented { Layout.fillWidth: true; model: page.templates; value: page.pendingTemplate; onPicked: (v) => page.pendingTemplate = v } }
                }
                Caption { visible: page.pendingMany.length > 0; Layout.fillWidth: true; text: "Titles come from the file names. The same options are used for all of them." }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    PillButton { visible: page.pendingMany.length === 0; text: "Import only"; enabled: !!page.pending.ok; onClicked: page.importPending(false) }
                    PillButton { text: page.pendingMany.length > 0 ? "Process " + page.manyOk + " recordings" : "Import and process"; checked: true; enabled: page.pendingMany.length > 0 ? page.manyOk > 0 : !!page.pending.ok; onClicked: page.importPending(true) }
                }
            }
        }

        // ---------------- recording now ----------------
        Rectangle {
            visible: audioIntel.recording && !view.importing
            Layout.fillWidth: true
            implicitHeight: 54
            radius: theme.radiusMd
            color: theme.dangerBg
            border.width: 1
            border.color: Qt.alpha(theme.danger, 0.5)
            RowLayout {
                anchors { fill: parent; leftMargin: 14; rightMargin: 8 }
                spacing: 10
                Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; radius: 5; color: theme.danger; SequentialAnimation on opacity { running: audioIntel.recording && activities.animations; loops: Animation.Infinite; NumberAnimation { to: 0.3; duration: 600 } NumberAnimation { to: 1; duration: 600 } } }
                Text { text: page.clock(audioIntel.recordSeconds); color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: "monospace" }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 4; radius: 2; color: theme.surface
                    Rectangle { width: parent.width * Math.min(1, audioIntel.recordLevel * 1.4); height: parent.height; radius: 2; color: theme.success } }
                PillButton { text: "Stop and process"; checked: true; onClicked: audioIntel.stopRecording(true) }
                PillButton { text: "Stop"; onClicked: audioIntel.stopRecording(false) }
                IconButton { iconName: "trash"; Accessible.name: "Throw the recording away"; onClicked: audioIntel.discardRecording() }
            }
        }

        // ---------------- a batch being worked on ----------------
        Rectangle {
            visible: !view.importing && (page.batchRunning || (audioIntel.batchTotal > 1 && audioIntel.batchDone > 0))
            Layout.fillWidth: true
            implicitHeight: batchRow.implicitHeight + 16
            radius: theme.radiusSm + 2
            color: theme.well
            border.width: 1
            border.color: theme.hairline
            RowLayout {
                id: batchRow
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                spacing: 10
                BusyIndicator { visible: page.batchRunning; running: visible; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: theme.text
                    font.pixelSize: 12
                    text: page.batchRunning ? (audioIntel.batchTotal > 1 ? "Recording " + Math.min(audioIntel.batchTotal, audioIntel.batchDone + 1) + " of " + audioIntel.batchTotal : "Working")
                                              + (page.activeStage ? " · " + page.activeStage.label + " " + Math.round(page.activeStage.progress * 100) + "%" : "")
                                            : "Batch finished: " + audioIntel.batchDone + " of " + audioIntel.batchTotal
                }
                PillButton { visible: page.batchRunning; text: audioIntel.queued > 0 ? "Stop all" : "Stop"; onClicked: audioIntel.cancel() }
            }
            Rectangle {
                visible: page.batchRunning
                anchors { left: parent.left; bottom: parent.bottom; leftMargin: 1; bottomMargin: 1 }
                height: 2
                radius: 1
                color: theme.accent
                width: Math.max(0, (parent.width - 2) * Math.min(1, audioIntel.batchTotal > 1 ? (audioIntel.batchDone + audioIntel.jobProgress) / audioIntel.batchTotal : audioIntel.jobProgress))
            }
        }

        // ---------------- first steps: nothing here yet ----------------
        Rectangle {
            id: first
            visible: !view.importing && !audioIntel.recording && meetingList.count === 0 && searchField.text === "" && !page.showHistory
            Layout.fillWidth: true
            implicitHeight: firstBody.implicitHeight + 40
            radius: theme.radiusMd
            color: "transparent"
            border.width: 1
            border.color: page.dropping ? theme.accent : theme.hairline
            Rectangle { anchors.fill: parent; radius: parent.radius; color: Qt.alpha(theme.accent, 0.08); visible: page.dropping }
            ColumnLayout {
                id: firstBody
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 20 }
                spacing: 12
                Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 44; Layout.preferredHeight: 44; radius: 22; color: Qt.alpha(theme.accent, 0.15)
                    LineIcon { anchors.centerIn: parent; glyph: "mic"; sourceSize: Qt.size(20, 20); color: theme.accent } }
                Text { Layout.alignment: Qt.AlignHCenter; text: "Turn a conversation into notes"; color: theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; color: theme.textDim; font.pixelSize: 12
                    text: "Record a meeting, a lecture or a call, or drop an audio file here. You get a transcript, who said what, and notes with decisions and tasks, all on this computer." }
                ColumnLayout { // the three steps
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 4
                    spacing: 8
                    Repeater {
                        model: [
                            { n: "1", t: "Download the speech model", done: view.ready },
                            { n: "2", t: "Record, or import an audio file", done: meetingList.count > 0 },
                            { n: "3", t: "Read the transcript and the notes", done: false }
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            spacing: 10
                            Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 10; color: modelData.done ? theme.accent : "transparent"; border.width: 1; border.color: modelData.done ? theme.accent : theme.textFaint
                                LineIcon { visible: modelData.done; anchors.centerIn: parent; glyph: "check"; sourceSize: Qt.size(11, 11); color: theme.onColor(theme.accent) }
                                Text { visible: !modelData.done; anchors.centerIn: parent; text: modelData.n; color: theme.textDim; font.pixelSize: 10 } }
                            Text { text: modelData.t; color: modelData.done ? theme.textDim : theme.text; font.pixelSize: 12; font.strikeout: false }
                        }
                    }
                }
                // the speech model, when it is the next step
                ColumnLayout {
                    visible: audioIntel.readiness.compiled && !view.ready
                    Layout.fillWidth: true
                    spacing: 6
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8
                        PillButton {
                            visible: view.speech.state !== "downloading" && view.speech.state !== "verifying"
                            text: (view.speech.partial ? "Continue the download" : "Download") + " · " + view.speech.sizeText
                            iconName: "download"
                            checked: true
                            onClicked: audioIntel.downloadModel("whisper-small")
                        }
                        PillButton { visible: view.speech.state === "downloading"; text: "Cancel"; onClicked: audioIntel.cancelDownload("whisper-small") }
                    }
                    Rectangle { visible: view.speech.state === "downloading"; Layout.fillWidth: true; Layout.preferredHeight: 3; radius: 1.5; color: theme.surface
                        Rectangle { width: parent.width * view.speech.progress; height: parent.height; radius: 1.5; color: theme.accent } }
                    Caption { visible: view.speech.state === "downloading"; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: view.speech.receivedText + (view.speech.speedText !== "" ? " · " + view.speech.speedText : "") + (view.speech.etaText !== "" ? " · " + view.speech.etaText : "") }
                    Text { visible: view.speech.error !== ""; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 11; text: view.speech.error }
                    Caption { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "Downloaded once from Hugging Face, only when you press the button. More models: Settings → Audio AI." }
                }
                Caption { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "Only record when everyone who is heard agrees." }
            }
        }

        // ---------------- search ----------------
        TextField {
            id: searchField
            visible: !view.importing && !page.showHistory && (meetingList.count > 0 || text !== "")
            Layout.fillWidth: true
            implicitHeight: 32
            leftPadding: 34
            placeholderText: "Search titles, transcripts and notes"
            placeholderTextColor: theme.placeholder
            color: theme.text
            selectionColor: theme.accent
            font.pixelSize: 13
            background: Rectangle { radius: 16; color: theme.well; border.width: 1; border.color: searchField.activeFocus ? theme.accent : theme.hairline
                LineIcon { anchors { left: parent.left; leftMargin: 11; verticalCenter: parent.verticalCenter } glyph: "search"; sourceSize: Qt.size(14, 14); color: theme.textDim } }
            onTextEdited: audioIntel.search(text)
        }

        // ---------------- the recordings ----------------
        ListView {
            id: meetingList
            interactive: false // the page scrolls
            visible: count > 0 && !page.showHistory && !view.importing
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            spacing: 6
            model: audioIntel.meetings
            delegate: Rectangle {
                id: card
                required property int meetingId
                required property string title
                required property string durationText
                required property string dateText
                required property string language
                required property int speakers
                required property string status
                required property string snippet
                required property double progress
                required property string stage
                width: ListView.view.width
                height: cardBody.implicitHeight + 22
                radius: theme.radiusMd
                color: hover.hovered ? theme.hoverFill : theme.well
                border.width: 1
                border.color: hover.hovered ? theme.outline : theme.hairline
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: page.openMeeting(card.meetingId) }
                RowLayout {
                    id: cardBody
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 14; rightMargin: 12 }
                    spacing: 12
                    Rectangle { Layout.alignment: Qt.AlignTop; Layout.topMargin: 5; Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: page.statusColor(card.status) }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text { Layout.fillWidth: true; text: card.title; textFormat: Text.PlainText; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Text {
                            Layout.fillWidth: true
                            color: card.status === "failed" ? theme.danger : theme.textDim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            text: card.status === "processing" ? (card.stage !== "" ? card.stage : "Working") + " · " + Math.round(card.progress * 100) + "%"
                                  : card.status === "queued" ? "Waiting for its turn" : card.status === "failed" ? "Stopped · tap to continue"
                                  : [card.dateText, card.durationText, card.language ? card.language.toUpperCase() : "", card.speakers > 0 ? card.speakers + (card.speakers === 1 ? " speaker" : " speakers") : ""].filter(x => x !== "").join(" · ")
                        }
                        Text { visible: card.snippet !== "" && card.status === "completed"; Layout.fillWidth: true; text: card.snippet; textFormat: Text.PlainText; color: theme.textSoft; font.pixelSize: 12; elide: Text.ElideRight }
                        Rectangle { visible: card.status === "processing"; Layout.fillWidth: true; Layout.preferredHeight: 2; radius: 1; color: theme.surface
                            Rectangle { width: parent.width * card.progress; height: parent.height; radius: 1; color: theme.accent } }
                    }
                    LineIcon { Layout.alignment: Qt.AlignVCenter; glyph: "chevron"; sourceSize: Qt.size(12, 12); color: theme.textFaint; opacity: hover.hovered ? 1 : 0 }
                }
            }
        }
        Text { visible: !view.importing && !page.showHistory && meetingList.count === 0 && searchField.text !== ""; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; color: theme.textDim; font.pixelSize: 12; text: "Nothing matches." }

        // ---------------- what was processed, newest first ----------------
        ColumnLayout {
            visible: page.showHistory && !view.importing
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Text { text: "PROCESSING HISTORY"; color: theme.textDim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
                Item { Layout.fillWidth: true }
                Text { visible: audioIntel.history.length > 0; text: "Clear"; color: theme.accent; font.pixelSize: 12; HoverHandler { cursorShape: Qt.PointingHandCursor } TapHandler { onTapped: audioIntel.clearHistory() } }
            }
            Text { visible: audioIntel.history.length === 0; Layout.fillWidth: true; color: theme.textDim; font.pixelSize: 12; text: "Nothing has been processed yet." }
            Repeater {
                model: audioIntel.history
                delegate: Rectangle {
                    id: hrow
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: hbody.implicitHeight + 16
                    radius: theme.radiusSm + 2
                    color: hhover.hovered ? theme.hoverFill : theme.well
                    HoverHandler { id: hhover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: { page.showHistory = false; page.openMeeting(hrow.modelData.meetingId) } }
                    RowLayout {
                        id: hbody
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 12; rightMargin: 10 }
                        spacing: 10
                        Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: hrow.modelData.state === "done" ? theme.success : hrow.modelData.state === "running" ? theme.accent : hrow.modelData.state === "stopped" ? theme.textDim : theme.danger }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text { Layout.fillWidth: true; text: hrow.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 12 }
                            Text { Layout.fillWidth: true; color: theme.textDim; font.pixelSize: 11; textFormat: Text.PlainText; elide: Text.ElideRight
                                text: hrow.modelData.when + " · " + (hrow.modelData.state === "done" ? "Done in " + hrow.modelData.took : hrow.modelData.state === "running" ? "Working" : hrow.modelData.state === "stopped" ? "Stopped" : hrow.modelData.text) }
                        }
                    }
                }
            }
        }
        Item { Layout.preferredHeight: 4 }
    }
}
