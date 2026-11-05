import QtQuick
import QtQuick.Layouts

// One file on the cloud shelf: its icon, name and size; a ring and a percent while it is being fetched. Click fetches it, right-click for more.
Item {
    id: tile
    required property string cloudId
    required property string name
    required property string sizeText
    required property string iconName
    required property string addedText
    required property bool mine
    required property bool locked
    required property bool busy
    required property int percent
    signal openRequested()
    signal menuRequested()

    readonly property var view: GridView.view
    width: view ? view.cellWidth - 4 : 96
    height: 128

    Rectangle {
        anchors.fill: parent
        radius: theme.radiusMd
        color: hover.hovered ? theme.hoverFill : "transparent"
        border.width: 1
        border.color: theme.washi ? theme.outline : theme.hairline
    }
    HoverHandler { id: hover; cursorShape: tile.locked ? Qt.ArrowCursor : Qt.PointingHandCursor }
    Rectangle {
        id: thumb
        anchors.horizontalCenter: parent.horizontalCenter
        y: 10
        width: 64
        height: 64
        radius: theme.radiusMd
        color: theme.surface
        border.width: 1
        border.color: theme.washi ? theme.outline : theme.hairline
        Image {
            anchors.centerIn: parent
            width: 40
            height: 40
            source: "image://mime/" + tile.iconName
            sourceSize: Qt.size(80, 80)
            opacity: tile.locked ? 0.35 : 1
        }
        Rectangle { // being fetched
            anchors.fill: parent
            radius: thumb.radius
            color: "#99000000"
            visible: tile.busy
            Text { anchors.centerIn: parent; text: tile.percent >= 0 ? tile.percent + "%" : "…"; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
        }
        LineIcon { // the cloud mark: this file is on the server
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 4
            glyph: "cloud"
            sourceSize: Qt.size(13, 13)
            color: theme.textDim
        }
    }
    Text {
        id: title
        anchors.top: thumb.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 8
        textFormat: Text.PlainText // a name can look like markup
        text: tile.name
        color: tile.locked ? theme.textDim : theme.text
        font.pixelSize: 11
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        maximumLineCount: 2
        elide: Text.ElideRight
    }
    Text {
        anchors.top: title.bottom
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 8
        text: tile.locked ? "Needs the shelf key" : tile.sizeText + (tile.mine ? "" : " · shared")
        color: theme.textDim
        font.pixelSize: 10
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        enabled: !tile.locked && !tile.busy
        onTapped: tile.openRequested()
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: tile.menuRequested()
    }
}
