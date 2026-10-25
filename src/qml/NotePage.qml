import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Quick note editor page: name, .txt/.md, text with the toolkit row. State lives in the shell.
ColumnLayout {
    id: page
    required property var shell
    visible: shell.page === "note"

    // Fill the fields from the shell's draft and focus the editor (called when the page opens).
    function load() {
        noteNameField.text = shell.noteName
        noteEditor.text = shell.noteText
        noteEditor.forceActiveFocus()
    }

    // Note toolkit: transforms the selection, or the whole note when nothing is selected.
    function applyTransform(kind) {
        const start = noteEditor.selectionStart, end = noteEditor.selectionEnd
        const partial = end > start
        const r = notch.transformText(kind, partial ? noteEditor.text.substring(start, end) : noteEditor.text)
        if (!r.ok) {
            shell.infoText = "⚠ " + r.error
            shell.restartInfo()
            return
        }
        if (partial) {
            noteEditor.remove(start, end)
            noteEditor.insert(start, r.text)
            noteEditor.select(start, start + r.text.length)
        } else {
            noteEditor.text = r.text
        }
        noteEditor.forceActiveFocus()
    }
    Layout.fillWidth: true
    Layout.leftMargin: 8
    Layout.rightMargin: 8
    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        TextField {
            id: noteNameField
            Layout.fillWidth: true
            implicitHeight: 28
            leftPadding: 12
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            placeholderText: "Name (optional)"
            placeholderTextColor: theme.placeholder
            color: shell.textPrimary
            selectionColor: shell.accent
            font.pixelSize: 12
            background: Rectangle {
                radius: 14
                color: theme.surface
                border.width: 1
                border.color: noteNameField.activeFocus ? shell.accent : "transparent"
            }
            onTextChanged: shell.noteName = text
            Keys.onPressed: (e) => {
                if (e.key === Qt.Key_S && (e.modifiers & Qt.ControlModifier)) {
                    shell.saveNote()
                    e.accepted = true
                }
            }
            Keys.onEscapePressed: shell.page = "shelf"
        }
        PillButton {
            visible: shell.noteId === ""
            text: ".txt"
            checked: shell.noteFormat === "txt"
            onClicked: shell.noteFormat = "txt"
        }
        PillButton {
            visible: shell.noteId === ""
            text: ".md"
            checked: shell.noteFormat === "md"
            onClicked: shell.noteFormat = "md"
        }
    }

    Row {
        Layout.fillWidth: true
        spacing: 5
        Repeater {
            model: [["json", "{ } JSON"], ["base64-encode", "Base64 ↑"], ["base64-decode", "Base64 ↓"],
                    ["url-encode", "URL ↑"], ["url-decode", "URL ↓"], ["upper", "ABC"], ["lower", "abc"]]
            PillButton {
                required property var modelData
                implicitHeight: 24
                text: modelData[1]
                Accessible.name: modelData[0]
                onClicked: page.applyTransform(modelData[0])
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 260
        radius: 14
        color: theme.well
        border.width: 1
        border.color: noteEditor.activeFocus ? theme.accentSoft : theme.surface
        ScrollView {
            id: noteScroll
            anchors.fill: parent
            anchors.margins: 10
            contentWidth: availableWidth
            TextArea {
                id: noteEditor
                width: noteScroll.availableWidth
                wrapMode: TextArea.Wrap
                color: theme.textSoft
                selectionColor: shell.accent
                font.family: "monospace"
                font.pixelSize: 12
                placeholderText: "Write a note or paste a snippet…"
                placeholderTextColor: theme.textFaint
                background: null
                padding: 0
                onTextChanged: shell.noteText = text
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_S && (e.modifiers & Qt.ControlModifier)) {
                        shell.saveNote()
                        e.accepted = true
                    }
                }
                Keys.onEscapePressed: shell.page = "shelf"
            }
        }
    }

    Caption {
        Layout.fillWidth: true
        text: (shell.noteId !== "" ? "Saving updates this file in Olyzano Notch" : "Saved as " + shell.noteFileName())
              + " · Ctrl+S to save · Esc to go back (draft is kept)"
    }
}
