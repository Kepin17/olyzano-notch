import QtQuick
import QtQuick.Layouts

// A heading with a thin rule, and what belongs under it (one part of the notes).
ColumnLayout {
    id: sec
    property string title
    property bool shown: true
    default property alias content: inner.data
    visible: shown
    Layout.fillWidth: true
    spacing: 8
    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Text { text: sec.title.toUpperCase(); color: theme.textDim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.hairline }
    }
    ColumnLayout { id: inner; Layout.fillWidth: true; spacing: 8 }
}
