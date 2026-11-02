import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

RowLayout {
    id: step
    property bool done: false
    property string title: ""
    property string detail: ""
    property string action: ""
    signal triggered()
    Layout.fillWidth: true
    spacing: 10
    Rectangle {
        Layout.preferredWidth: 20
        Layout.preferredHeight: 20
        radius: 10
        color: step.done ? Qt.alpha(theme.accent, 0.2) : "transparent"
        border.width: 1
        border.color: step.done ? Qt.alpha(theme.accent, 0.6) : theme.outline
        LineIcon { anchors.centerIn: parent; visible: step.done; glyph: "check"; sourceSize: Qt.size(11, 11); color: theme.accentText }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 1
        Text { text: step.title; color: step.done ? theme.textDim : theme.text; font.pixelSize: 12; font.weight: Font.Medium }
        Text { Layout.fillWidth: true; visible: !!text; text: step.detail; color: theme.textFaint; font.pixelSize: 11; elide: Text.ElideMiddle }
    }
    PillButton { visible: !step.done && !!step.action; text: step.action; checked: true; onClicked: step.triggered() }
}
