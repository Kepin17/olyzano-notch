import QtQuick
import QtQuick.Layouts

// A subject (or a term, or a question) with its points under it.
ColumnLayout {
    id: nt
    required property var page
    property var topic: ({})
    property int number: 0
    Layout.fillWidth: true
    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { visible: nt.number > 0; text: nt.number; color: theme.accent; font.pixelSize: 12; font.weight: Font.DemiBold; Layout.preferredWidth: 14 }
        Text { Layout.fillWidth: true; text: nt.topic.title || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
        AudioTimeLink { page: nt.page; seconds: nt.topic.time !== undefined ? nt.topic.time : -1 }
    }
    AudioBullets { Layout.leftMargin: nt.number > 0 ? 22 : 0; items: nt.topic.points || [] }
}
