import QtQuick
import QtQuick.Controls.Basic
import QtCore
import QtQuick.Dialogs
import QtQuick.Layouts

// One recording: a short header (title, player), what is going on with it, and under tabs the notes, the transcript and the tasks. What is under
// the tabs takes the rest of the height; the transcript list is what scrolls, following the audio.
Item {
    id: detail
    required property var page
    readonly property var info: page.info
    readonly property string chapters: info.hasNotes ? audioIntel.chaptersText("youtube") : "" // "" when the notes give fewer than three
    property string pending: ""          // what the save dialog is for: srt | vtt | txt | chapters-youtube | chapters-list
    property bool withSounds: true       // music and other sounds as [markers] in the exported transcript

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // title
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            IconButton { iconName: "back"; Accessible.name: "All recordings"; onClicked: page.closeDetail() }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    implicitHeight: 26
                    leftPadding: 6
                    topPadding: 0
                    bottomPadding: 0
                    text: detail.info.title || ""
                    color: theme.text
                    selectionColor: theme.accent
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    maximumLength: 120
                    background: Rectangle { radius: theme.radiusSm; color: nameField.activeFocus ? theme.well : "transparent"; border.width: nameField.activeFocus ? 1 : 0; border.color: theme.accent }
                    onEditingFinished: audioIntel.rename(audioIntel.current, text)
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    elide: Text.ElideRight
                    color: theme.textDim
                    font.pixelSize: 11
                    textFormat: Text.PlainText
                    text: [detail.info.created, detail.info.durationText, (detail.info.language || "").toUpperCase(),
                           detail.info.speakerCount > 0 ? detail.info.speakerCount + (detail.info.speakerCount === 1 ? " speaker" : " speakers") : ""].filter(x => !!x).join(" · ")
                }
            }
            Rectangle { // the state, as a dot
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: page.statusColor(detail.info.status)
                ToolTip.visible: dotHover.hovered
                ToolTip.text: page.statusText(detail.info.status)
                HoverHandler { id: dotHover }
            }
            Text { visible: page.confirmDelete; text: "Delete this recording?"; color: theme.danger; font.pixelSize: 12 }
            PillButton { visible: page.confirmDelete; text: "Cancel"; onClicked: page.confirmDelete = false }
            PillButton { visible: page.confirmDelete; text: "Delete"; checked: true; onClicked: { page.confirmDelete = false; page.removeCurrent() } }
            IconButton { visible: !page.confirmDelete; iconName: "trash"; Accessible.name: "Delete this recording"; onClicked: page.confirmDelete = true }
        }

        Rectangle { // something that did not work (a recording that is being processed cannot be deleted, for one)
            visible: audioIntel.lastError !== ""
            Layout.fillWidth: true
            implicitHeight: errText.implicitHeight + 14
            radius: theme.radiusSm + 2
            color: theme.dangerBg
            Text { id: errText; anchors { fill: parent; margins: 7; rightMargin: 34 } wrapMode: Text.Wrap; textFormat: Text.PlainText; color: theme.danger; font.pixelSize: 12; text: audioIntel.lastError }
            IconButton { anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 4 } iconName: "close"; implicitWidth: 24; implicitHeight: 24; Accessible.name: "Dismiss"; onClicked: audioIntel.clearError() }
        }

        AudioPlayerBar { Layout.fillWidth: true; page: detail.page; visible: !!page.player.item }
        Text { visible: !!page.player.item && page.player.item.error !== ""; Layout.fillWidth: true; color: theme.warn; font.pixelSize: 11; wrapMode: Text.Wrap; text: "The audio cannot be played here: " + (page.player.item ? page.player.item.error : "") }

        AudioJobStrip { Layout.fillWidth: true; page: detail.page }

        AudioTabs {
            visible: !!detail.info.hasTranscript
            Layout.fillWidth: true
            model: [["summary", "Summary"], ["transcript", "Transcript" + (detail.info.segments > 0 ? " · " + detail.info.segments : "")],
                    ["actions", "Tasks" + (detail.info.actionCount > 0 ? " · " + detail.info.actionCount : "")], ["ask", "Ask"]]
            value: page.tab
            onPicked: (v) => page.tab = v
            IconButton { visible: page.tab === "transcript"; iconName: "search"; Accessible.name: "Find in the transcript"; implicitWidth: 26; implicitHeight: 26; onClicked: page.findOpen = !page.findOpen }
            IconButton { visible: page.tab === "transcript"; iconName: "download"; Accessible.name: "Export the transcript (SRT, VTT, text)"; implicitWidth: 26; implicitHeight: 26; onClicked: exportMenu.popup() }
            IconButton { visible: page.tab === "summary" && !!detail.info.hasNotes && detail.chapters !== ""; iconName: "doc"; Accessible.name: "Chapters for a video description"; implicitWidth: 26; implicitHeight: 26; onClicked: chapterMenu.popup() }
            IconButton { visible: page.tab === "summary" && !!detail.info.hasNotes; iconName: "copy"; Accessible.name: "Copy the notes as Markdown"; implicitWidth: 26; implicitHeight: 26; onClicked: { audioIntel.copyNotes(); page.shell.say("Notes copied as Markdown", 1400) } }
        }

        Item {
            id: area
            visible: !!detail.info.hasTranscript
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            AudioNotes { anchors.fill: parent; page: detail.page; visible: opacity > 0; opacity: page.tab === "summary" ? 1 : 0; Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 120 } } }
            AudioTranscript { anchors.fill: parent; page: detail.page; visible: opacity > 0; opacity: page.tab === "transcript" ? 1 : 0; Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 120 } } }
            AudioActions { anchors.fill: parent; page: detail.page; visible: opacity > 0; opacity: page.tab === "actions" ? 1 : 0; Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 120 } } }
            ScrollView {
                id: askArea
                anchors.fill: parent
                visible: page.tab === "ask"
                contentWidth: availableWidth
                clip: true
                AskPanel {
                    width: askArea.availableWidth - 10
                    scopeKind: "recording"
                    scopeRef: String(audioIntel.current)
                    hint: "Ask about this recording"
                    onGrew: Qt.callLater(() => { const f = askArea.contentItem; f.contentY = Math.max(0, f.contentHeight - f.height + 18) })
                    onOpenSource: (s) => {
                        if (s.kind === "recording" && parseInt(s.ref) === audioIntel.current) detail.page.playAt(s.startSec)
                        else if (s.kind === "recording") { detail.page.shell.audioOpen = { id: parseInt(s.ref), sec: s.startSec } }
                        else notch.open(s.ref)
                    }
                }
            }
        }
        Item { visible: !detail.info.hasTranscript; Layout.fillWidth: true; Layout.fillHeight: true }
    }

    DarkMenu {
        id: exportMenu
        MenuItem { text: "Save as SRT (subtitles)"; onTriggered: detail.save("srt") }
        MenuItem { text: "Save as WebVTT"; onTriggered: detail.save("vtt") }
        MenuItem { text: "Save as plain text"; onTriggered: detail.save("txt") }
        MenuItem { text: "Include music and sounds"; checkable: true; checked: detail.withSounds; onTriggered: detail.withSounds = checked }
    }
    DarkMenu {
        id: chapterMenu
        MenuItem { text: "Copy chapters (YouTube)"; onTriggered: { audioIntel.copyChapters("youtube"); detail.page.shell.say("Chapters copied", 1400) } }
        MenuItem { text: "Copy chapters (list)"; onTriggered: { audioIntel.copyChapters("list"); detail.page.shell.say("Chapters copied", 1400) } }
        MenuItem { text: "Save chapters as text"; onTriggered: detail.save("chapters-youtube") }
    }
    function save(kind) {
        pending = kind
        saveDialog.currentFile = StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/"
                                 + (kind.indexOf("chapters") === 0 ? "chapters.txt" : audioIntel.suggestedFileName(kind))
        saveDialog.open()
    }
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        title: "Save"
        onVisibleChanged: detail.page.shell.dialogs += visible ? 1 : -1
        Component.onDestruction: if (visible) detail.page.shell.dialogs -= 1 // a page closed under an open dialog must not leave the notch below the windows
        onAccepted: {
            const why = detail.pending.indexOf("chapters") === 0 ? audioIntel.saveChapters("youtube", selectedFile.toString())
                                                                 : audioIntel.exportTranscript(detail.pending, detail.withSounds, selectedFile.toString())
            detail.page.shell.say(why === "" ? "Saved" : why, why === "" ? 1400 : 3500)
        }
    }
}
