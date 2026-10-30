import QtQuick
import QtQuick.Layouts

// A titled group of settings / info on a card. Promoted from the former SettingsPage-local
// SettingCard so Main / Dock / Clipboard can share the same chrome (well + hairline + radiusMd
// + optional glass).
Rectangle {
    id: card
    property string title: ""
    property bool glass: false
    default property alias content: body.data
    Layout.fillWidth: true
    implicitHeight: body.implicitHeight + 24
    radius: theme.radiusMd
    color: theme.card
    border.width: 1
    border.color: theme.outline
    GlassLayer { anchors.fill: parent; radius: theme.radiusMd; visible: card.glass }
    ColumnLayout {
        id: body
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
        spacing: 10
        Text {
            visible: card.title !== ""
            text: card.title.toUpperCase()
            color: theme.textDim
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 1
        }
    }
}
