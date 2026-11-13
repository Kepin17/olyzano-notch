import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore

// After a recording: the email to the people who were in it, the agenda of the next meeting, and the tasks onto the Tasks list, all from its notes.
ScrollView {
    id: view
    required property var page
    property string subject: ""
    property string email: ""
    property string agenda: ""
    property bool polished: false
    property string tasksNote: ""
    property string pending: ""     // what the dialog saves: email | agenda
    readonly property bool hasNotes: !!audioIntel.info.hasNotes
    contentWidth: availableWidth
    clip: true

    function write() { if (!hasNotes || audioIntel.current === 0) return; audioIntel.followUp(audioIntel.current, "email"); audioIntel.followUp(audioIntel.current, "agenda") }
    Connections {
        target: audioIntel
        function onFollowUpReady(id, kind, subject, text, polished) {
            if (id !== audioIntel.current)
                return
            if (kind === "email") { view.subject = subject; view.email = text; view.polished = polished }
            else view.agenda = text
        }
        function onCurrentChanged() { view.subject = ""; view.email = ""; view.agenda = ""; view.tasksNote = ""; view.polished = false; if (view.visible) view.write() }
    }
    onVisibleChanged: if (visible && email === "") write()
    function mailto() {
        const body = email.length > 1500 ? email.substring(0, 1500) + "\n…" : email
        Qt.openUrlExternally("mailto:?subject=" + encodeURIComponent(subject) + "&body=" + encodeURIComponent(body))
    }
    function save(kind) {
        pending = kind
        saveDialog.currentFile = StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/" + (kind === "email" ? "follow-up-email.txt" : "next-meeting-agenda.txt")
        saveDialog.open()
    }
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        title: "Save"
        onVisibleChanged: view.page.shell.dialogs += visible ? 1 : -1
        Component.onDestruction: if (visible) view.page.shell.dialogs -= 1
        onAccepted: {
            const why = audioIntel.saveText(view.pending === "email" ? view.subject + "\n\n" + view.email : view.agenda, selectedFile.toString())
            view.page.shell.say(why === "" ? "Saved" : why, why === "" ? 1400 : 3500)
        }
    }

    ColumnLayout {
        width: view.availableWidth - 10
        spacing: 10
        Text { visible: !view.hasNotes; Layout.fillWidth: true; Layout.topMargin: 8; wrapMode: Text.Wrap; color: theme.textDim; font.pixelSize: 12; text: "The follow-up is written from the notes: they come with the language model (Settings → AI)." }

        FormCard {
            visible: view.hasNotes
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: "Email to the people who were there"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                Text { visible: view.polished; text: "Smoothed by the language model"; color: theme.textDim; font.pixelSize: 11 }
            }
            FormField { Layout.fillWidth: true; text: view.subject; placeholderText: "Subject"; onTextEdited: view.subject = text }
            TextArea {
                id: emailBox
                Layout.fillWidth: true
                Layout.preferredHeight: 260
                text: view.email
                wrapMode: TextEdit.Wrap
                color: theme.text
                font.pixelSize: 12
                selectByMouse: true
                background: Rectangle { radius: theme.radiusSm; color: theme.surface; border.width: 1; border.color: emailBox.activeFocus ? theme.accent : theme.hairline }
                onTextChanged: if (activeFocus) view.email = text
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                PillButton { text: "Copy"; iconName: "copy"; onClicked: { notch.copyText(view.subject + "\n\n" + view.email); view.page.shell.say("Email copied", 1400) } }
                PillButton { text: "Open in mail app"; iconName: "external"; onClicked: view.mailto() }
                PillButton { text: "Save…"; iconName: "download"; onClicked: view.save("email") }
                PillButton { text: "Write again"; onClicked: audioIntel.followUp(audioIntel.current, "email") }
            }
            Caption { Layout.fillWidth: true; visible: view.email.length > 1500; text: "A long email is cut when it opens in the mail app: use Copy for all of it." }
        }

        FormCard {
            visible: view.hasNotes
            Text { text: "Agenda for the next meeting"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Caption { Layout.fillWidth: true; text: "What was left open: tasks not done, questions, risks and next steps." }
            TextArea {
                id: agendaBox
                Layout.fillWidth: true
                Layout.preferredHeight: 170
                text: view.agenda
                wrapMode: TextEdit.Wrap
                color: theme.text
                font.pixelSize: 12
                selectByMouse: true
                background: Rectangle { radius: theme.radiusSm; color: theme.surface; border.width: 1; border.color: agendaBox.activeFocus ? theme.accent : theme.hairline }
                onTextChanged: if (activeFocus) view.agenda = text
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                PillButton { text: "Copy"; iconName: "copy"; onClicked: { notch.copyText(view.agenda); view.page.shell.say("Agenda copied", 1400) } }
                PillButton { text: "Save…"; iconName: "download"; onClicked: view.save("agenda") }
            }
        }

        FormCard {
            visible: view.hasNotes
            Text { text: "Tasks"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Caption { Layout.fillWidth: true; text: audioIntel.info.actionCount > 0 ? "Put the tasks of this recording on your Tasks list, with their dates. Those already on it are left alone." : "No tasks were found in this recording." }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                PillButton {
                    primary: true
                    enabled: audioIntel.info.actionCount > 0
                    text: "Add the tasks to Tasks"
                    iconName: "plus"
                    onClicked: { const r = taskSuggest.addMeeting(audioIntel.current); view.tasksNote = r.added > 0 ? r.added + (r.added === 1 ? " task added" : " tasks added") + (r.skipped > 0 ? ", " + r.skipped + " already on the list" : "") : "Nothing new: they are on the list already" }
                }
            }
            Text { visible: view.tasksNote !== ""; Layout.fillWidth: true; text: view.tasksNote; wrapMode: Text.Wrap; color: theme.success; font.pixelSize: 12 }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
