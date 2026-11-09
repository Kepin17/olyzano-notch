import QtQuick
import QtQuick.Layouts

// A plain list of short points.
ColumnLayout {
    id: bl
    property var items: []
    Layout.fillWidth: true
    spacing: 4
    Repeater {
        model: bl.items
        RowLayout {
            required property string modelData
            Layout.fillWidth: true
            spacing: 8
            Rectangle { Layout.alignment: Qt.AlignTop; Layout.topMargin: 6; Layout.preferredWidth: 4; Layout.preferredHeight: 4; radius: 2; color: theme.textFaint }
            Text { Layout.fillWidth: true; text: modelData; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.text; font.pixelSize: 12; lineHeight: 1.15 }
        }
    }
}
