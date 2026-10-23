import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Category filter chip. Drop tiles on it to move them in, or files to add them into it.
AbstractButton {
    id: chip
    property string catId
    property int itemCount
    property string special // "screenshots" | "downloads" | "kdeconnect" | ""
    property bool locked    // a locked (vault) category
    readonly property bool active: notch.categoryFilter === catId
    readonly property bool dropHover: chipDrop.containsDrag
    signal menuRequested()
    implicitHeight: 26
    implicitWidth: chipRow.implicitWidth + 22
    background: Rectangle {
        radius: 13
        color: chip.active ? theme.accent : chip.dropHover ? theme.surfaceHover : chip.hovered ? theme.surfaceHover : theme.surface
        border.width: chip.dropHover ? 1 : 0
        border.color: theme.accent
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    contentItem: Item {
        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 5
            Text {
                text: (chip.locked ? (notch.vaultUnlocked ? "🔓 " : "🔒 ") : "")
                    + (chip.special === "screenshots" ? "📷 " : chip.special === "downloads" ? "⬇ "
                     : chip.special === "kdeconnect" ? "📱 " : "") + chip.text
                color: chip.active ? "white" : theme.text
                font.pixelSize: 12
                font.weight: Font.Medium
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.itemCount
                color: chip.active ? "#d0e4ff" : theme.textDim
                font.pixelSize: 11
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
