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
    SettingToggle { title: "Suggest tasks from recordings"; caption: "When a recording's notes are made, its action items are offered on the Tasks page. Nothing is added until you press Add."; checked: taskSuggest.autoMeetings; onToggled: taskSuggest.autoMeetings = !taskSuggest.autoMeetings }
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
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6
        readonly property var emb: { audioIntel.models; return audioIntel.modelInfo("embed-qwen3") }
        SectionLabel { text: "Find by meaning (optional)" }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { Layout.fillWidth: true; text: parent.parent.emb.title + "  ·  " + parent.parent.emb.sizeText; color: theme.text; font.pixelSize: 12; elide: Text.ElideRight }
            PillButton { visible: !parent.parent.emb.ready && parent.parent.emb.state !== "downloading"; text: parent.parent.emb.partial ? "Resume" : "Download"; iconName: "download"; onClicked: audioIntel.downloadModel("embed-qwen3") }
            PillButton { visible: parent.parent.emb.state === "downloading"; text: "Cancel"; onClicked: audioIntel.cancelDownload("embed-qwen3") }
            PillButton { visible: parent.parent.emb.ready; text: "Remove"; iconName: "trash"; onClicked: audioIntel.removeModel("embed-qwen3") }
        }
        Rectangle { visible: parent.emb.state === "downloading"; Layout.fillWidth: true; Layout.preferredHeight: 4; radius: 2; color: theme.surface
            Rectangle { width: parent.width * parent.parent.emb.progress; height: parent.height; radius: 2; color: theme.accent } }
        Caption { Layout.fillWidth: true; text: "With it the search follows what a sentence means, not only its words, and finds a file written in another language. Downloaded only when you press Download; the passages are then indexed again with it." }
        Text { visible: parent.emb.error !== ""; Layout.fillWidth: true; text: parent.emb.error; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 11 }
    }
    Caption { Layout.fillWidth: true; visible: !knowledge.modelReady; text: "Asking questions needs the language model (see Models and helpers below). Finding files works without it." }
}
