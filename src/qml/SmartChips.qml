import QtQuick
import QtQuick.Layouts

// What a smart search understood of the sentence, as chips that can be taken away one by one, and how many files it found.
ColumnLayout {
    id: chips
    spacing: 6
    readonly property var smart: knowledge.smart
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        PulseDot { visible: knowledge.smartBusy }
        Text {
            Layout.fillWidth: true
            text: knowledge.smartBusy ? "Understanding what you look for…" : smart.error !== undefined && smart.error !== "" ? String(smart.error)
                  : smart.count + (smart.count === 1 ? " file" : " files") + " for “" + smart.query + "”"
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: smart.error !== undefined && smart.error !== "" ? theme.warn : theme.textSoft
            font.pixelSize: 11
        }
        PillButton { text: "Show all"; onClicked: knowledge.clearSmart() }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: knowledge.smartBusy ? [] : (smart.chips || [])
            delegate: Rectangle {
                required property var modelData
                required property int index
                implicitWidth: label.implicitWidth + 30
                implicitHeight: 24
                radius: 12
                color: modelData.kind === "period" ? Qt.alpha(theme.accent, 0.18) : theme.surface
                border.width: 1
                border.color: modelData.kind === "period" ? Qt.alpha(theme.accent, 0.5) : theme.hairline
                opacity: modelData.kind === "synonym" ? 0.75 : 1
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Text { id: label; text: modelData.label; textFormat: Text.PlainText; color: theme.text; font.pixelSize: 11 }
                    LineIcon { glyph: "close"; sourceSize: Qt.size(8, 8); color: theme.textDim; anchors.verticalCenter: parent.verticalCenter }
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: knowledge.smartDrop(index) }
                Accessible.role: Accessible.Button
                Accessible.name: "Leave out " + modelData.label
            }
        }
    }
    Caption { visible: smart.note !== undefined && smart.note !== ""; Layout.fillWidth: true; text: smart.note || "" }
}
