import QtQuick
import QtQuick.Layouts

// Graphics card: what Notch found, what it uses for the speech and the language models (decided by itself, or by the setting), a speed
// test for an integrated card, and the program that lets the language model use a card.
Card {
    id: card
    title: "Graphics card"
    glass: true
    readonly property var hw: audioIntel.hardware
    readonly property var helper: { audioIntel.models; return audioIntel.modelInfo(hw.helperId) }

    Caption { Layout.fillWidth: true; text: "Notch finds the graphics card by itself and uses it for the speech and the language model when that is quicker. If it does not work, the processor takes over." }

    // what was found
    Repeater {
        model: card.hw.devices
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 8
            LineIcon { glyph: "server"; sourceSize: Qt.size(14, 14); color: theme.textDim }
            Text { Layout.fillWidth: true; text: modelData.name; color: theme.text; font.pixelSize: 12; elide: Text.ElideRight }
            Text { text: modelData.memory + (modelData.integrated ? ", shared with the computer" : ""); color: theme.textDim; font.pixelSize: 11 }
        }
    }
    Caption { visible: card.hw.detected && card.hw.devices.length === 0; Layout.fillWidth: true; text: "No graphics card was found: everything runs on the processor." }
    Caption { visible: !card.hw.detected; Layout.fillWidth: true; text: "Looking for graphics cards…" }
    Caption { visible: card.hw.devices.length > 0 && !card.hw.vulkan; Layout.fillWidth: true; text: "No Vulkan driver was found, which is what a card is used through: install your graphics driver's Vulkan package." }
    Caption { visible: card.hw.devices.length > 0 && card.hw.vulkan && !card.hw.built; Layout.fillWidth: true; text: "This build of Notch was made without graphics card support for the speech model, so it runs on the processor." }

    SectionLabel { text: "Use" }
    Segmented { Layout.fillWidth: true; model: [["auto", "Automatic"], ["cpu", "Processor only"], ["gpu", "Graphics card"]]; value: card.hw.setting; onPicked: (v) => audioIntel.setHardwareMode(v) }
    Caption { visible: card.hw.reason !== ""; Layout.fillWidth: true; text: card.hw.reason }
    Caption { visible: card.hw.speech !== "" || card.hw.notes !== ""; Layout.fillWidth: true
        text: "Last used: speech on " + (card.hw.speech || "—") + ", notes on " + (card.hw.notes || "—") }
    Text { visible: card.hw.failure !== ""; Layout.fillWidth: true; text: card.hw.failure; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.warn; font.pixelSize: 11 }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        PillButton { text: card.hw.testing ? "Testing…" : "Test speed"; enabled: !card.hw.testing && card.hw.devices.length > 0; onClicked: audioIntel.speedTest() }
        PillButton { text: "Detect again"; onClicked: audioIntel.detectHardware() }
        Item { Layout.fillWidth: true }
    }
    Caption { visible: card.hw.testResult !== ""; Layout.fillWidth: true; text: card.hw.testResult }
    Caption { visible: card.hw.benchKnown && card.hw.testResult === ""; Layout.fillWidth: true; text: "Speed test: the card was " + card.hw.speedup.toFixed(1) + " times as quick as the processor." }

    // the program that lets the language model use the card
    ColumnLayout {
        visible: card.hw.devices.length > 0 && card.hw.vulkan
        Layout.fillWidth: true
        spacing: 6
        SectionLabel { text: "Language model on the card" }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { Layout.fillWidth: true; text: card.helper.title; color: theme.text; font.pixelSize: 12; elide: Text.ElideRight }
            PillButton { visible: !card.helper.ready && card.helper.state !== "downloading"; text: card.helper.partial ? "Resume" : "Download"; iconName: "download"; onClicked: audioIntel.downloadModel(card.helper.id) }
            PillButton { visible: card.helper.state === "downloading"; text: "Cancel"; onClicked: audioIntel.cancelDownload(card.helper.id) }
            PillButton { visible: card.helper.ready; text: "Remove"; iconName: "trash"; onClicked: audioIntel.removeModel(card.helper.id) }
        }
        Rectangle {
            visible: card.helper.state === "downloading"
            Layout.fillWidth: true
            Layout.preferredHeight: 4
            radius: 2
            color: theme.surface
            Rectangle { width: parent.width * card.helper.progress; height: parent.height; radius: 2; color: theme.accent }
        }
        Caption { Layout.fillWidth: true; text: card.helper.sizeText + ". Downloaded only when you press Download. Without it the notes, translation and answers run on the processor." }
        Text { visible: card.helper.error !== ""; Layout.fillWidth: true; text: card.helper.error; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 11 }
    }
}
