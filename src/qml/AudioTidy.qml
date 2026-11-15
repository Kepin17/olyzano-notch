import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Tidy the transcript of the open recording: take out "eee" and "um", collapse a word said three times in a row, and use the person's own list of
// words the recognizer gets wrong (Settings → AI). It shows what would change first; each line keeps the recognized text underneath, and the last tidy can be undone.
FormCard {
    id: card
    required property var page
    property bool fillers: true
    property bool repeats: true
    property bool words: true
    property int stamp: 0                                                                // bumps when the transcript or the list changes, so the preview is asked again
    property int doneCount: -1                                                           // lines changed by the tidy just made (-1: none yet)
    readonly property var preview: { stamp; audioIntel.dictionary; return audioIntel.tidyPreview(fillers, repeats, words) }
    visible: page.tidyOpen && page.tab === "transcript"
    Layout.fillWidth: true

    Connections { target: audioIntel; function onCurrentChanged() { card.stamp++; card.doneCount = -1 } }

    Text { text: "Tidy the transcript"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        PillButton { text: "Remove um, eee"; checked: card.fillers; onClicked: { card.fillers = !card.fillers; card.stamp++ } }
        PillButton { text: "Repeated words"; checked: card.repeats; onClicked: { card.repeats = !card.repeats; card.stamp++ } }
        PillButton { text: "My word list" + (audioIntel.dictionary.length > 0 ? "  ·  " + audioIntel.dictionary.length : ""); checked: card.words; onClicked: { card.words = !card.words; card.stamp++ } }
    }
    Caption {
        Layout.fillWidth: true
        text: card.doneCount >= 0 ? (card.doneCount === 0 ? "Nothing to tidy." : card.doneCount + (card.doneCount === 1 ? " line was tidied." : " lines were tidied."))
              : card.preview.count === 0 ? "Nothing to tidy with these choices." : card.preview.count + (card.preview.count === 1 ? " line would change." : " lines would change.")
    }
    Repeater {
        model: card.doneCount >= 0 ? [] : card.preview.samples
        delegate: ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 1
            Text { Layout.fillWidth: true; text: modelData.before; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.textFaint; font.pixelSize: 11; font.strikeout: true; elide: Text.ElideRight; maximumLineCount: 2 }
            Text { Layout.fillWidth: true; text: modelData.after; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.text; font.pixelSize: 12; maximumLineCount: 2; elide: Text.ElideRight }
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        PillButton {
            primary: true
            text: "Tidy"
            iconName: "pen"
            enabled: card.preview.count > 0
            onClicked: { card.doneCount = audioIntel.tidyApply(card.fillers, card.repeats, card.words); card.stamp++ }
        }
        PillButton { visible: audioIntel.canUndoTidy; text: "Undo"; iconName: "undo"; onClicked: { audioIntel.tidyUndo(); card.doneCount = -1; card.stamp++ } }
    }
    Caption { Layout.fillWidth: true; text: "Nothing is deleted for good: each line keeps what was recognized, and Undo brings the last tidy back. The notes are not rewritten; make them again if the changes matter." }
}
