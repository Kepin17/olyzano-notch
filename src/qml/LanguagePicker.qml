import QtQuick
import QtQuick.Layouts

// The language of something: one chip with the language chosen that opens the languages in use (and Automatic), and a link to add more in
// Settings → Languages. `value` is "auto" or a code; `picked(code)` says what was chosen.
ColumnLayout {
    id: picker
    property string value: "auto"
    property bool allowAuto: true
    property bool open: false
    signal picked(string code)
    signal moreRequested()
    Layout.fillWidth: true
    spacing: 6

    Rectangle {
        implicitWidth: row.implicitWidth + 22
        implicitHeight: 30
        radius: 15
        color: hover.hovered ? theme.hoverFill : theme.surface
        border.width: 1
        border.color: picker.open ? theme.accent : theme.outline
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: picker.open = !picker.open }
        Accessible.role: Accessible.Button
        Accessible.name: "Language: " + languages.label(picker.value)
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 7
            LineIcon { glyph: "globe"; sourceSize: Qt.size(14, 14); color: theme.textDim; anchors.verticalCenter: parent.verticalCenter }
            Text { text: languages.label(picker.value); color: theme.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
            LineIcon { glyph: "chevron"; sourceSize: Qt.size(11, 11); color: theme.textDim; rotation: picker.open ? 90 : -90; anchors.verticalCenter: parent.verticalCenter }
        }
    }
    Flow {
        visible: picker.open
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: (picker.allowAuto ? [{ code: "auto", label: "Automatic" }] : []).concat(languages.picker)
            delegate: PillButton {
                required property var modelData
                text: modelData.label
                checked: picker.value === modelData.code
                onClicked: { picker.open = false; picker.picked(modelData.code) }
            }
        }
        Text {
            text: "More languages…"
            color: theme.accent
            font.pixelSize: 12
            height: 30
            verticalAlignment: Text.AlignVCenter
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: { picker.open = false; picker.moreRequested() } }
        }
    }
}
