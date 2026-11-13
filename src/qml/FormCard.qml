import QtQuick
import QtQuick.Layouts

// A quiet rounded box for one thing on the Forms pages; what goes inside is laid out in a column.
Rectangle {
    id: card
    default property alias content: col.data
    property alias spacing: col.spacing
    property bool highlight: false
    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + 28
    radius: theme.radiusMd
    color: theme.well
    border.width: 1
    border.color: highlight ? Qt.alpha(theme.accent, 0.55) : theme.hairline
    ColumnLayout {
        id: col
        anchors { fill: parent; margins: 14 }
        spacing: 10
    }
}
