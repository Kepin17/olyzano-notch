import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Shapes

Item {
    id: tile
    required property string itemId
    required property string name
    required property string sizeText
    required property string addedText
    required property string expiresText
    required property string status
    required property string iconName
    required property url fileUrl
    required property string sourcePath
    required property bool isImage
    required property bool isText
    required property int index
    required property bool pinned
    required property var sizeBytes
    required property string mimeType
    required property bool removed      // shown in Recently removed
    required property string removedText
    required property bool expiringSoon // auto-deleted within a day

    signal contextMenuRequested(var info) // {id, name, source, pinned, isText}
    signal previewRequested(var item)
    // mode: "toggle" (Ctrl+click, or any click while selecting) | "range" (Shift+click)
    signal selectRequested(int index, string id, string mode)

    // The ListView owns the selection (selectedIds) and decides what a drag carries (dragIds).
    readonly property var view: ListView.view
    readonly property bool selected: view ? view.selectedIds.indexOf(itemId) >= 0 : false
    readonly property bool renaming: view ? view.renamingId === itemId : false
    property var draggedIds: []

    readonly property bool available: status === "available"
    readonly property bool copying: status === "copying"
    readonly property bool failed: status === "error"
    readonly property bool showThumbnail: isImage && available

    width: 96
    height: 136

    component CircleButton: AbstractButton {
        id: cb
        implicitWidth: 22
        implicitHeight: 22
        background: Rectangle {
            radius: 11
            color: cb.down ? theme.textFaint : cb.hovered ? theme.textFaint : theme.surfacePressed
            border.color: theme.island
            border.width: 1.5
        }
        contentItem: Text {
            text: cb.text
            color: theme.text
            font.pixelSize: 11
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: tile.selected ? theme.accentFaint : hover.hovered ? theme.surface : "transparent"
        border.width: tile.selected ? 1 : 0
        border.color: theme.accentSoft
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    HoverHandler {
        id: hover
        // Lets Main know which tile is under the cursor (Space = Quick Look).
        onHoveredChanged: {
            if (!tile.view)
                return
            if (hovered)
                tile.view.hoveredId = tile.itemId
            else if (tile.view.hoveredId === tile.itemId)
                tile.view.hoveredId = ""
        }
    }

    Rectangle {
        id: thumb
        anchors.horizontalCenter: parent.horizontalCenter
        y: 10
        width: 64
        height: 64
        radius: 16
        color: theme.surface
        border.width: tile.selected ? 2 : 0
        border.color: theme.accent
        scale: hover.hovered && !tile.copying ? 1.05 : 1
        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }

        Image {
            id: icon
            anchors.centerIn: parent
            width: 40
            height: 40
            visible: !tile.showThumbnail
            source: "image://mime/" + tile.iconName
            sourceSize: Qt.size(80, 80)
            opacity: tile.available ? 1 : 0.45
        }

        Image {
            id: photo
            anchors.fill: parent
            visible: false
            source: tile.showThumbnail ? tile.fileUrl : ""
            sourceSize: Qt.size(128, 128)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        Item {
            id: roundMask
            anchors.fill: parent
            visible: false
            layer.enabled: true
            Rectangle { anchors.fill: parent; radius: thumb.radius }
        }
        MultiEffect {
            anchors.fill: parent
            visible: tile.showThumbnail
            source: photo
            maskEnabled: true
            maskSource: roundMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }

        // Copying: dim + spinning arc.
        Rectangle {
            anchors.fill: parent
            radius: thumb.radius
            color: "#99000000"
            visible: tile.copying
            Shape {
                anchors.centerIn: parent
                width: 24
                height: 24
                preferredRendererType: Shape.CurveRenderer
                RotationAnimator on rotation {
                    running: tile.copying
                    from: 0
                    to: 360
                    duration: 900
                    loops: Animation.Infinite
                }
                ShapePath {
                    strokeColor: theme.text
                    strokeWidth: 2.5
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc { centerX: 12; centerY: 12; radiusX: 10; radiusY: 10; startAngle: 0; sweepAngle: 270 }
                }
            }
        }

        Rectangle {
            visible: tile.pinned && !hover.hovered
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: -4
            width: 18
            height: 18
            radius: 9
            color: "#ff9f0a"
            border.color: theme.island
            border.width: 1.5
            Text { anchors.centerIn: parent; text: "★"; color: "black"; font.pixelSize: 10 }
        }

        Rectangle {
            visible: tile.selected
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: -4
            width: 18
            height: 18
            radius: 9
            color: theme.accent
            border.color: theme.island
            border.width: 1.5
            Text { anchors.centerIn: parent; text: "✓"; color: "white"; font.pixelSize: 10; font.bold: true }
        }

        Rectangle {
            visible: tile.failed
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: -4
            width: 18
            height: 18
            radius: 9
            color: "#ff453a"
            border.color: theme.island
            border.width: 1.5
            Text { anchors.centerIn: parent; text: "!"; color: "white"; font.pixelSize: 11; font.bold: true }
        }

        Rectangle {
            visible: tile.expiringSoon && !hover.hovered
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: -4
            width: 18
            height: 18
            radius: 9
            color: "#ff9f0a"
            border.color: theme.island
            border.width: 1.5
            Text { anchors.centerIn: parent; text: "⏳"; font.pixelSize: 9 }
        }

        CircleButton {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: -6
            text: tile.removed ? "↺" : "↗"
            visible: hover.hovered && (tile.available || tile.removed)
            Accessible.name: tile.removed ? "Restore" : "Open"
            onClicked: tile.removed ? notch.restoreItems([tile.itemId]) : notch.open(tile.itemId)
        }
        CircleButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: -6
            text: "✕"
            visible: hover.hovered && !tile.copying
            Accessible.name: tile.removed ? "Delete now" : "Remove from Notch"
            onClicked: tile.removed ? notch.deleteForever([tile.itemId]) : notch.remove(tile.itemId)
        }
    }

    TextField {
        id: renameField
        visible: tile.renaming
        anchors.top: thumb.bottom
        anchors.topMargin: 6
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width + 24 // a bit wider than the tile: names are long
        z: 2
        height: 24
        leftPadding: 6
        rightPadding: 6
        topPadding: 0
        bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        horizontalAlignment: TextInput.AlignHCenter
        color: theme.text
        selectionColor: theme.accent
        font.pixelSize: 11
        background: Rectangle { radius: 6; color: theme.surface; border.width: 1; border.color: theme.accent }
        onVisibleChanged: {
            if (!visible)
                return
            text = tile.name
            forceActiveFocus()
            const dot = tile.name.lastIndexOf(".")
            select(0, dot > 0 ? dot : tile.name.length) // base name only, keep the extension
        }
        onAccepted: tile.view.commitRename(tile.itemId, text)
        onActiveFocusChanged: if (!activeFocus && visible) tile.view.renamingId = ""
        Keys.onEscapePressed: tile.view.renamingId = ""
    }

    Text {
        id: title
        visible: !tile.renaming
        anchors.top: thumb.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 8
        text: tile.name
        color: theme.text
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
        text: tile.copying ? "Copying…" : tile.failed ? "Missing" : tile.removed ? tile.removedText
            : tile.expiringSoon ? "⏳ " + tile.expiresText.replace(" left", "") + " · Keep" : tile.sizeText
        color: tile.failed ? "#ff6961" : tile.expiringSoon && !tile.removed ? "#ff9f0a" : theme.textDim
        // One click on "· Keep" saves a file that is about to expire.
        TapHandler {
            enabled: tile.expiringSoon && !tile.removed
            onTapped: notch.setPinned([tile.itemId], true)
        }
        font.pixelSize: 10
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }

    ToolTip {
        id: tip
        visible: hover.hovered && !dragHandler.active
        delay: 650
        text: tile.name + "\n" + tile.sizeText + " · added " + tile.addedText + "\n" + tile.expiresText
              + (tile.sourcePath ? "\nFrom " + tile.sourcePath : "")
        background: Rectangle { color: theme.surface; radius: 10; border.color: theme.surfaceHover }
        contentItem: Text {
            text: tip.text
            color: theme.text
            font.pixelSize: 11
            lineHeight: 1.15
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: tile.contextMenuRequested({ id: tile.itemId, name: tile.name, source: tile.sourcePath,
                                              pinned: tile.pinned, isText: tile.isText && tile.mimeType !== "application/x-desktop" })
    }
    TapHandler {
        acceptedModifiers: Qt.ControlModifier
        onTapped: tile.selectRequested(tile.index, tile.itemId, "toggle")
    }
    TapHandler {
        acceptedModifiers: Qt.ShiftModifier
        onTapped: tile.selectRequested(tile.index, tile.itemId, "range")
    }
    TapHandler {
        acceptedModifiers: Qt.NoModifier
        enabled: tile.available || (tile.view && tile.view.selectedIds.length > 0)
        // Press and hold: Quick Look until released.
        longPressThreshold: 0.35
        onLongPressed: tile.view.quickLook(tile.itemId)
        onPressedChanged: if (!pressed) tile.view.endQuickLook()
        onTapped: {
            if (tile.view.selectedIds.length > 0) {
                tile.selectRequested(tile.index, tile.itemId, "toggle")
                return
            }
            tile.previewRequested({
            id: tile.itemId, name: tile.name, url: tile.fileUrl, isImage: tile.isImage, isText: tile.isText,
            sizeBytes: tile.sizeBytes, mimeType: tile.mimeType,
            iconName: tile.iconName, sizeText: tile.sizeText, addedText: tile.addedText,
            expiresText: tile.expiresText, sourcePath: tile.sourcePath
            })
        }
    }

    // Drag out: hands the managed copies to the target as file URLs. Dragging a selected tile
    // carries the whole selection; dropping on a category chip moves them there.
    DragHandler {
        id: dragHandler
        target: null
        enabled: tile.available
        // Started imperatively: the system drag cancels this handler, so a binding would loop.
        onActiveChanged: {
            if (!active)
                return
            tile.draggedIds = tile.view.dragIds(tile.itemId)
            tile.Drag.mimeData = { "text/uri-list": notch.uriList(tile.draggedIds) }
            notch.setDragging(tile.draggedIds, true)
            tile.Drag.active = true
        }
    }
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction // never Move: a file manager would take our managed copy away
    Drag.imageSource: tile.showThumbnail ? tile.fileUrl : icon.source
    Drag.onDragFinished: {
        tile.Drag.active = false
        notch.setDragging(tile.draggedIds, false)
    }
}
