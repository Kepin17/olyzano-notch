import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Category filter chip. Drop tiles on it to move them in, or files to add them into it.
AbstractButton {
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    id: chip
    property string catId
    property int itemCount
    property string special // "screenshots" | "downloads" | "kdeconnect" | ""
    property bool locked    // a locked (vault) category
    readonly property bool active: notch.categoryFilter === catId
    readonly property bool dropHover: chipDrop.containsDrag
    signal menuRequested()
    signal dropDone()
    implicitHeight: 28
    implicitWidth: chipRow.implicitWidth + 24
    // The active chip is tinted (soft accent fill, accent text and outline), the way Washi's vermilion outline is.
    readonly property bool filled: false
    readonly property color ink: chip.active ? theme.accentText : theme.text
    background: Rectangle {
        radius: theme.washi ? theme.chipRadius : theme.radiusSm
        color: chip.dropHover ? theme.accentFaint : chip.active ? Qt.alpha(theme.accent, 0.16) : chip.hovered ? theme.hoverFill : "transparent"
        border.width: 1
        border.color: chip.dropHover ? theme.accent : chip.active ? Qt.alpha(theme.accent, 0.45) : theme.washi ? theme.outline : theme.hairline
        Behavior on color { enabled: activities.animations; ColorAnimation { duration: 120 } }
    }
    contentItem: Item {
        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 5
            LineIcon {
                anchors.verticalCenter: parent.verticalCenter
                glyph: chip.locked ? (notch.vaultUnlocked ? "unlock" : "lock")
                    : chip.special === "screenshots" ? "camera" : chip.special === "downloads" ? "download"
                    : chip.special === "kdeconnect" ? "phone" : ""
                visible: glyph !== ""
                sourceSize: Qt.size(13, 13)
                color: chip.ink
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.text
                color: chip.ink
                font.pixelSize: 12
                font.weight: Font.Medium
            }
            Rectangle { // the count, as a small badge
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(18, countText.implicitWidth + 10)
                height: 16
                radius: 4
                color: chip.active ? Qt.alpha(theme.accent, 0.22) : theme.hoverFill
                Text {
                    id: countText
                    anchors.centerIn: parent
                    text: chip.itemCount
                    color: chip.active ? theme.accent : theme.textDim
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }
        }
    }
    onClicked: notch.categoryFilter = catId
    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: chip.catId !== ""
        onTapped: chip.menuRequested()
    }

    // Drag the chip out: all files of the category go along (to Dolphin, a chat, …);
    // dropped on another chip they move into that category.
    property var draggedIds: []
    DragHandler {
        id: chipDragHandler
        target: null
        enabled: chip.itemCount > 0
        onActiveChanged: {
            if (!active)
                return
            chip.draggedIds = notch.categoryItemIds(chip.catId)
            chip.Drag.mimeData = { "text/uri-list": notch.uriList(chip.draggedIds) }
            notch.setDragging(chip.draggedIds, true)
            chip.Drag.active = true
        }
    }
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.onDragFinished: {
        chip.Drag.active = false
        notch.setDragging(chip.draggedIds, false)
    }

    DropArea {
        id: chipDrop
        anchors.fill: parent
        // From inside Notch only tiles/chips carrying draggedIds count (never this chip itself);
        // from outside, files.
        onEntered: (drag) => {
            drag.accepted = drag.source ? drag.source.draggedIds !== undefined && drag.source !== chip
                                        : drag.hasUrls || drag.hasText
        }
        onDropped: (d) => {
            chip.dropDone()
            if (d.source === chip)
                return
            if (d.source && d.source.draggedIds !== undefined) {
                notch.setCategory(d.source.draggedIds, chip.catId)
                d.accept(Qt.CopyAction)
            } else if (!d.hasUrls && d.hasText) { // a text selection: becomes a note here
                notch.addTextNote(d.text, chip.catId)
                d.acceptProposedAction()
            } else if (d.hasUrls) {
                notch.addDrop(d.urls, d.html, chip.catId)
                d.acceptProposedAction()
            }
        }
    }
}
