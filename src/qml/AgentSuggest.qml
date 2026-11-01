import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The list that opens above the prompt box while you type @file or /command. It never takes the keyboard: the prompt box
// keeps the cursor and sends Up / Down / Tab / Enter here through currentIndex and picked().
Popup {
    id: pop
    property var items: []   // [{glyph, title, args, detail, mono}]
    property int currentIndex: 0
    signal picked(int index)
    padding: 4
    focus: false
    closePolicy: Popup.NoAutoClose
    x: 0
    y: -height - 6
    width: parent ? parent.width : 420
    background: Rectangle {
        color: theme.surface
        radius: theme.radiusMd
        border.width: 1
        border.color: theme.outline
    }
    contentItem: ListView {
        id: list
        implicitHeight: Math.min(contentHeight, 264)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: pop.items
        currentIndex: pop.currentIndex
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            width: list.width - 8
            height: 32
            radius: theme.radiusSm
            color: index === pop.currentIndex ? Qt.alpha(theme.accent, 0.16) : "transparent"
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: pop.currentIndex = row.index
                onClicked: pop.picked(row.index)
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                LineIcon { visible: !!row.modelData.glyph; glyph: row.modelData.glyph || ""; sourceSize: Qt.size(14, 14); color: theme.textDim }
                Text {
                    text: row.modelData.title
                    color: row.index === pop.currentIndex ? theme.accentText : theme.text
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    font.family: row.modelData.mono ? "monospace" : Qt.application.font.family
                }
                Text { visible: !!row.modelData.args; text: row.modelData.args || ""; color: theme.textFaint; font.pixelSize: 11; font.family: "monospace" }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.detail || ""
                    color: theme.textFaint
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                }
            }
        }
    }
}
