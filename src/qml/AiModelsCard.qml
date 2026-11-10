import QtQuick
import QtQuick.Layouts

// Every model Notch can download, in one place, by what it is for. A whole category or everything can be installed with one press; the
// programs that several features share are downloaded once. Nothing is downloaded until a button is pressed.
Card {
    id: card
    title: "AI models"
    glass: true
    readonly property var sum: audioIntel.aiSummary
    readonly property var feats: audioIntel.aiFeatures
    function feature(id) { for (let i = 0; i < feats.length; ++i) if (feats[i].id === id) return feats[i]; return ({ state: "missing", title: id, note: "", sizeText: "", error: "", progress: 0, left: 0, extra: false }) }

    Caption { Layout.fillWidth: true; text: "Everything Notch downloads to work with audio, documents and pictures, by what it is for. It all runs on this computer, and nothing is downloaded until you press a button." }
    Text {
        Layout.fillWidth: true
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: theme.text
        font.pixelSize: 12
        text: card.sum.installed + " of " + card.sum.total + " features ready  ·  " + card.sum.diskText + " on disk" + (card.sum.freeText !== "" ? "  ·  " + card.sum.freeText + " free" : "")
    }
    RowLayout { // install it all
        visible: !card.sum.active
        Layout.fillWidth: true
        spacing: 8
        PillButton { visible: card.sum.recommendedLeft > 0; text: "Install recommended  ·  " + card.sum.recommendedText; iconName: "download"; checked: true; onClicked: audioIntel.installRecommended() }
        PillButton { visible: card.sum.everythingLeft > card.sum.recommendedLeft; text: "Install everything  ·  " + card.sum.everythingText; iconName: "download"; onClicked: audioIntel.installEverything() }
        Text { visible: card.sum.everythingLeft === 0; text: "Everything is installed."; color: theme.success; font.pixelSize: 12 }
        Item { Layout.fillWidth: true }
    }
    Caption { visible: !card.sum.active && card.sum.recommendedLeft > 0; Layout.fillWidth: true; text: "Recommended is all that every feature needs. Everything also adds the optional better-quality models." }
    ColumnLayout { // the install that is running
        visible: card.sum.active
        Layout.fillWidth: true
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; elide: Text.ElideRight; color: theme.text; font.pixelSize: 12
                text: "Downloading " + Math.min(card.sum.queueTotal, card.sum.queueDone + 1) + " of " + card.sum.queueTotal + (card.sum.running !== "" ? "  ·  " + card.sum.running : "") }
            PillButton { text: "Stop"; onClicked: audioIntel.cancelInstall() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 4; radius: 2; color: theme.surface
            Rectangle { width: parent.width * card.sum.queueProgress; height: parent.height; radius: 2; color: theme.accent } }
        Caption { Layout.fillWidth: true; text: (card.sum.speedText !== "" ? card.sum.speedText : "starting…") + (card.sum.etaText !== "" ? "  ·  " + card.sum.etaText : "") + "  ·  what is downloaded is kept if you stop" }
    }
    RowLayout {
        visible: card.sum.message !== ""
        Layout.fillWidth: true
        spacing: 8
        Text { Layout.fillWidth: true; text: card.sum.message; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 11 }
        PillButton { text: "OK"; onClicked: audioIntel.clearInstallMessage() }
    }

    Repeater {
        model: audioIntel.aiCategories
        delegate: ColumnLayout {
            id: cat
            required property var modelData
            readonly property var ids: audioIntel.aiFeatureIds(modelData.id) // fixed, so the rows stay while a download updates them
            readonly property var mine: card.feats.filter(f => f.category === modelData.id)
            readonly property int ready: mine.filter(f => f.ready).length
            readonly property real remaining: mine.reduce((a, f) => a + f.left, 0)
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.hairline }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { text: cat.modelData.title; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Caption { Layout.fillWidth: true; text: cat.ready + " of " + cat.mine.length + " ready  ·  " + cat.modelData.note }
                }
                PillButton { visible: cat.remaining > 0 && !card.sum.active; text: "Install all  ·  " + audioIntel.dataSize(cat.remaining); iconName: "download"; onClicked: audioIntel.installCategory(cat.modelData.id, true) }
            }
            Repeater {
                model: cat.ids
                delegate: ColumnLayout {
                    id: row
                    required property string modelData
                    readonly property var f: card.feature(modelData)
                    readonly property bool busy: f.state === "downloading" || f.state === "queued"
                    Layout.fillWidth: true
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text { Layout.fillWidth: true; elide: Text.ElideRight; color: theme.text; font.pixelSize: 13
                                text: row.f.title + "  ·  " + row.f.sizeText + (row.f.extra ? "  ·  optional" : "") }
                            Caption { Layout.fillWidth: true; text: row.f.ready ? "Installed  ·  " + row.f.diskText + " on disk" : row.f.state === "queued" ? "Waiting its turn…" : row.f.note }
                        }
                        PillButton { visible: !row.f.ready && !row.busy; text: row.f.state === "partial" ? "Continue" : "Download"; iconName: "download"; onClicked: audioIntel.installFeatures([row.modelData]) }
                        PillButton { visible: row.busy; text: "Cancel"; onClicked: audioIntel.cancelInstall() }
                        PillButton { visible: row.f.ready; text: "Remove"; iconName: "trash"; onClicked: audioIntel.removeFeature(row.modelData) }
                    }
                    Rectangle { visible: row.f.state === "downloading"; Layout.fillWidth: true; Layout.preferredHeight: 4; radius: 2; color: theme.surface
                        Rectangle { width: parent.width * row.f.progress; height: parent.height; radius: 2; color: theme.accent } }
                    Text { visible: row.f.error !== "" && !row.busy && !row.f.ready; Layout.fillWidth: true; text: row.f.error; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 11 }
                }
            }
        }
    }
}
