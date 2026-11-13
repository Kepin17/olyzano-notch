import QtQuick
import QtQuick.Layouts

// Search & Ask: what is kept in the index of your files and recordings (made on this computer, from the PDFs, text files and the transcripts).
Card {
    id: card
    title: "Search & Ask"
    glass: true

    Caption { Layout.fillWidth: true; text: "Notch keeps an index of the words in your PDFs, text files and recordings, so you can find a file by describing it (the ✦ button in Files) and ask questions in Documents and in a recording. It stays on this computer." }
    SettingToggle { title: "Index my files and recordings"; checked: knowledge.enabled; onToggled: knowledge.enabled = !knowledge.enabled }
    SettingToggle { title: "Include PDFs"; caption: "Their text, page by page."; checked: knowledge.indexPdf; onToggled: knowledge.indexPdf = !knowledge.indexPdf }
    SettingToggle { title: "Read scanned PDFs (OCR)"; caption: "Slow: every page that is a picture is read with OCR."; checked: knowledge.indexOcr; onToggled: knowledge.indexOcr = !knowledge.indexOcr }
    SettingToggle { title: "Include the answers of forms"; caption: "So that a search finds what people answered. It stays on this computer; turning it off takes the answers out of the index."; checked: knowledge.indexForms; onToggled: knowledge.indexForms = !knowledge.indexForms }
    SettingToggle { title: "Suggest tasks from recordings"; caption: "When a recording's notes are made, its action items are offered on the Tasks page. Nothing is added until you press Add."; checked: taskSuggest.autoMeetings; onToggled: taskSuggest.autoMeetings = !taskSuggest.autoMeetings }
    SettingToggle { title: "Add the tasks of a recording to Tasks by themselves"; caption: "Instead of offering them, the tasks of a recording are put on your Tasks list when its notes are made (those already on it are left alone)."; checked: taskSuggest.autoAdd; onToggled: taskSuggest.autoAdd = !taskSuggest.autoAdd }
    Text {
        Layout.fillWidth: true
        text: knowledge.status + (knowledge.stats.docs !== undefined ? "  ·  " + knowledge.stats.docs + " documents, " + knowledge.stats.chunks + " passages, " + knowledge.stats.sizeText : "")
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: theme.textDim
        font.pixelSize: 11
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        PillButton { text: "Check now"; enabled: !knowledge.indexing && knowledge.enabled; onClicked: knowledge.rescan() }
        PillButton { text: "Rebuild"; enabled: !knowledge.indexing; onClicked: knowledge.rebuild() }
        PillButton { text: "Clear"; iconName: "trash"; onClicked: knowledge.clearIndex() }
        Item { Layout.fillWidth: true }
    }
    Caption { Layout.fillWidth: true; visible: !knowledge.modelReady; text: "Asking questions needs the language model (see AI models above). Finding files works without it." }
}
