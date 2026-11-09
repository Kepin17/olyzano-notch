import QtQuick
import QtQuick.Layouts

// Reading images: the optional picture model (Qwen3-VL and its mmproj, two files shown as one). Downloaded only when you press Download.
Card {
    id: card
    title: "Reading images"
    glass: true
    readonly property var model: { audioIntel.models; return audioIntel.modelInfo("vision-qwen3vl") }
    readonly property var proj: { audioIntel.models; return audioIntel.modelInfo("vision-mmproj") }
    readonly property bool ready: !!model.ready && !!proj.ready
    readonly property bool downloading: model.state === "downloading" || proj.state === "downloading"
    readonly property bool partial: !!model.partial || !!proj.partial
    readonly property real progress: { const a = (model.ready ? 1 : model.progress || 0) * 2.5, b = (proj.ready ? 1 : proj.progress || 0) * 0.45; return (a + b) / 2.95 }
    readonly property string error: model.error || proj.error || ""

    Caption { Layout.fillWidth: true; text: "An optional model that looks at pictures: it says what is in a photo or screenshot, reads writing, tables and handwriting, and describes the charts and diagrams of a PDF. It runs on this computer only." }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { Layout.fillWidth: true; text: "Qwen3-VL 4B (reads pictures)  ·  about 2.9 GB"; color: theme.text; font.pixelSize: 12; elide: Text.ElideRight }
        PillButton { visible: !card.ready && !card.downloading; text: card.partial ? "Resume" : "Download"; iconName: "download"
            onClicked: { audioIntel.downloadModel("vision-qwen3vl"); audioIntel.downloadModel("vision-mmproj") } }
        PillButton { visible: card.downloading; text: "Cancel"; onClicked: { audioIntel.cancelDownload("vision-qwen3vl"); audioIntel.cancelDownload("vision-mmproj") } }
        PillButton { visible: card.ready; text: "Remove"; iconName: "trash"; onClicked: { audioIntel.removeModel("vision-qwen3vl"); audioIntel.removeModel("vision-mmproj") } }
    }
    Rectangle {
        visible: card.downloading
        Layout.fillWidth: true
        Layout.preferredHeight: 4
        radius: 2
        color: theme.surface
        Rectangle { width: parent.width * card.progress; height: parent.height; radius: 2; color: theme.accent }
    }
    Caption { visible: card.downloading; Layout.fillWidth: true; text: (model.state === "downloading" ? model.receivedText + (model.speedText !== "" ? " · " + model.speedText : "") : proj.receivedText + (proj.speedText !== "" ? " · " + proj.speedText : "")) }
    Caption { Layout.fillWidth: true; text: card.ready ? "Ready. Right-click a picture on the shelf → Tools → Image (AI), or choose Describe (AI) for figures when you summarize a PDF."
                                                       : "Downloaded only when you press Download, from Hugging Face, and checked. It needs about 4.5 GB of memory while it runs, and it is much quicker on a graphics card." }
    Text { visible: card.error !== ""; Layout.fillWidth: true; text: card.error; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 11 }
}
