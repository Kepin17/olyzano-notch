import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore

// Translate the transcript of the open recording, then save it as subtitles with two languages in each cue, or as the translation alone.
FormCard {
    id: card
    required property var page
    property string lang: "en"
    property string pending: ""
    property int stamp: 0                                                               // bumps when a job ends (or the language changes), so `have` is asked again
    readonly property bool have: { stamp; return translator.hasTranslation(audioIntel.current, lang) }
    visible: page.translateOpen && page.tab === "transcript"
    Layout.fillWidth: true

    Connections { target: translator; function onFinished() { card.stamp++ } }
    function save(format) {
        pending = format
        saveDialog.currentFile = StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/" + translator.suggestedName(audioIntel.current, lang, format)
        saveDialog.open()
    }
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        title: "Save"
        onVisibleChanged: card.page.shell.dialogs += visible ? 1 : -1
        Component.onDestruction: if (visible) card.page.shell.dialogs -= 1
        onAccepted: { const why = translator.exportMeeting(audioIntel.current, card.lang, card.pending, selectedFile.toString()); card.page.shell.say(why === "" ? "Saved" : why, why === "" ? 1400 : 3500) }
    }

    Text { text: "Translate the transcript"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
    LanguagePicker { allowAuto: false; value: card.lang; onPicked: (c) => { card.lang = c; card.stamp++ } onMoreRequested: { card.page.shell.settingsCategory = "languages"; card.page.shell.page = "settings" } }
    Text { visible: translator.lastError !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 12; text: translator.lastError }
    Repeater { model: translator.warnings; delegate: Text { required property string modelData; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.warn; font.pixelSize: 11; text: modelData } }
    Rectangle { visible: translator.busy; Layout.fillWidth: true; Layout.preferredHeight: 4; radius: 2; color: theme.surface
        Rectangle { width: parent.width * translator.progress; height: parent.height; radius: 2; color: theme.accent } }
    Caption { visible: translator.busy; Layout.fillWidth: true; text: translator.status }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        PillButton { visible: translator.busy; text: "Stop"; onClicked: translator.cancel() }
        PillButton { visible: !translator.busy; primary: !card.have; text: card.have ? "Translate again" : "Translate"; iconName: "globe"; onClicked: { translator.clearError(); translator.translateMeeting(audioIntel.current, card.lang) } }
    }
    Flow {
        visible: card.have && !translator.busy && card.stamp >= 0
        Layout.fillWidth: true
        spacing: 8
        PillButton { text: "Subtitles with both (SRT)"; iconName: "download"; onClicked: card.save("bilingual-srt") }
        PillButton { text: "Both (WebVTT)"; iconName: "download"; onClicked: card.save("bilingual-vtt") }
        PillButton { text: "Translation only (SRT)"; iconName: "download"; onClicked: card.save("srt") }
        PillButton { text: "Translation (text)"; iconName: "download"; onClicked: card.save("txt") }
    }
    Caption { Layout.fillWidth: true; text: "Done on this computer with the notes model. Standard is good for Indonesian and English; for other languages choose the Larger notes model in Settings → AI." }
}
