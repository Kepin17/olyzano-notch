import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Your forms: a card for each, with the one thing you most likely want to do next. Everything else is under "More".
ColumnLayout {
    id: list
    required property var tab
    property bool choosing: false   // the template cards are open
    property string more: ""        // the form whose extra buttons are showing
    Layout.fillWidth: true
    spacing: 12

    readonly property var templates: [
        ["send", "Send me files", "People put their name and files here.", "↑"],
        ["contact", "Contact", "Name, email and a message.", "@"],
        ["rsvp", "RSVP", "Who is coming, and how many.", "✓"],
        ["blank", "Blank", "Start from nothing.", "+"]]

    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { text: "Forms"; color: theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Caption { Layout.fillWidth: true; text: "Ask people questions. Answers and files come back to this computer." }
        }
        PillButton { text: list.choosing ? "Cancel" : "New form"; iconName: list.choosing ? "close" : "plus"; primary: !list.choosing; onClicked: list.choosing = !list.choosing }
    }

    GridLayout {
        visible: list.choosing || forms.list.length === 0
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 10
        rowSpacing: 10
        Repeater {
            model: list.templates
            delegate: Rectangle {
                id: tpl
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: 74
                radius: theme.radiusMd
                color: hov.hovered ? theme.surfaceHover : theme.well
                border.width: 1
                border.color: hov.hovered ? Qt.alpha(theme.accent, 0.5) : theme.hairline
                HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: { list.choosing = false; list.tab.startNew(tpl.modelData[0]) } }
                Accessible.role: Accessible.Button
                Accessible.name: "New form: " + modelData[1]
                RowLayout {
                    anchors { fill: parent; margins: 12 }
                    spacing: 12
                    FormKind { glyph: tpl.modelData[3]; implicitWidth: 36; implicitHeight: 36 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { Layout.fillWidth: true; text: tpl.modelData[1]; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Caption { Layout.fillWidth: true; text: tpl.modelData[2]; maximumLineCount: 2; elide: Text.ElideRight }
                    }
                }
            }
        }
    }

    Repeater {
        model: forms.list
        delegate: FormCard {
            id: card
            required property var modelData
            readonly property bool open: modelData.running
            highlight: open
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: list.tab.statusColor(card.modelData) }
                Text { Layout.fillWidth: true; text: card.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                Text { text: list.tab.statusName(card.modelData); color: list.tab.statusColor(card.modelData); font.pixelSize: 11; font.weight: Font.DemiBold }
            }
            Caption { Layout.fillWidth: true; text: list.tab.summary(card.modelData) }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                PillButton {
                    primary: true
                    text: card.open ? "Link and QR" : "Share"
                    iconName: "share"
                    onClicked: list.tab.openShare(card.modelData)
                }
                PillButton { text: card.modelData.answers > 0 ? "Answers (" + card.modelData.answers + ")" : "Answers"; onClicked: list.tab.openAnswers(card.modelData.id) }
                Item { Layout.fillWidth: true }
                PillButton { iconOnly: true; iconName: "gear"; text: "More"; checked: list.more === card.modelData.id; onClicked: list.more = list.more === card.modelData.id ? "" : card.modelData.id }
            }
            RowLayout {
                visible: list.more === card.modelData.id
                Layout.fillWidth: true
                spacing: 8
                PillButton { visible: !card.open; text: "Edit"; iconName: "pen"; onClicked: { list.more = ""; list.tab.edit(card.modelData.id) } }
                PillButton { visible: card.open; text: "Stop sharing"; iconName: "stop"; onClicked: { list.more = ""; forms.close(card.modelData.id) } }
                Item { Layout.fillWidth: true }
                PillButton {
                    text: list.tab.confirmId === card.modelData.id ? "Yes, delete it and its answers" : "Delete"
                    iconName: "trash"
                    onClicked: { if (list.tab.confirmId === card.modelData.id) { forms.removeForm(card.modelData.id); list.tab.confirmId = ""; list.more = "" } else list.tab.confirmId = card.modelData.id }
                }
            }
        }
    }
}
