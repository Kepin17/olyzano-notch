import QtQuick
import QtQuick.Layouts

// Selectable sidebar row: accent wash when selected, hover, two-line title/subtitle.
// Trailing holds an optional chip / icon; the whole row is one tap target.
// Right-click and long-press are left to the caller (TapHandler on top).
Rectangle {
    id: row
    property bool selected: false
    property string title: ""
    property string subtitle: ""
    property bool subtitleMono: true
    default property alias trailing: trail.data

    signal tapped

    // set by the ListView (or fixed by the caller)
    width: ListView.view ? ListView.view.width : parent ? parent.width : 0
    height: 44
    radius: 6
    color: selected ? Qt.alpha(theme.accent, 0.16)
                    : hov.hovered ? Qt.alpha(theme.text, 0.06) : "transparent"
    HoverHandler { id: hov }
    TapHandler { onTapped: row.tapped() }

    RowLayout {
        anchors {
            left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
            leftMargin: 10; rightMargin: 8
        }
        spacing: 6
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: row.title
                color: theme.text
                font.pixelSize: 12
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: row.subtitle !== ""
                text: row.subtitle
                color: theme.textFaint
                font.family: row.subtitleMono ? "monospace" : ""
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
        RowLayout {
            id: trail
            spacing: 6
        }
    }
}
