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

    signal contextMenuRequested(var info) // {id, source, pinned, isText}
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
            color: cb.down ? "#5a5a5e" : cb.hovered ? "#48484a" : "#3a3a3c"
            border.color: "#000000"
            border.width: 1.5
        }
        contentItem: Text {
            text: cb.text
            color: "#f5f5f7"
            font.pixelSize: 11
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: tile.selected ? "#1a0a84ff" : hover.hovered ? "#141416" : "transparent"
        border.width: tile.selected ? 1 : 0
        border.color: "#660a84ff"
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    HoverHandler { id: hover }

    Rectangle {
        id: thumb
        anchors.horizontalCenter: parent.horizontalCenter
        y: 10
        width: 64
        height: 64
        radius: 16
        color: "#1c1c1e"
        border.width: tile.selected ? 2 : 0
        border.color: "#0a84ff"
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
                    strokeColor: "#f5f5f7"
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
            border.color: "#000000"
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
            color: "#0a84ff"
            border.color: "#000000"
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
            border.color: "#000000"
            border.width: 1.5
            Text { anchors.centerIn: parent; text: "!"; color: "white"; font.pixelSize: 11; font.bold: true }
        }

        CircleButton {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: -6
            text: "↗"
            visible: hover.hovered && tile.available
            Accessible.name: "Open"
            onClicked: notch.open(tile.itemId)
        }
        CircleButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: -6
            text: "✕"
            visible: hover.hovered && !tile.copying
            Accessible.name: "Remove from Notch"
            onClicked: notch.remove(tile.itemId)
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
        color: "#f5f5f7"
        selectionColor: "#0a84ff"
        font.pixelSize: 11
        background: Rectangle { radius: 6; color: "#1c1c1e"; border.width: 1; border.color: "#0a84ff" }
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
        color: "#f5f5f7"
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
        text: tile.copying ? "Copying…" : tile.failed ? "Missing" : tile.sizeText
        color: tile.failed ? "#ff6961" : "#8e8e93"
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
        background: Rectangle { color: "#1c1c1e"; radius: 10; border.color: "#2c2c2e" }
        contentItem: Text {
            text: tip.text
            color: "#f5f5f7"
            font.pixelSize: 11
            lineHeight: 1.15
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: tile.contextMenuRequested({ id: tile.itemId, source: tile.sourcePath, pinned: tile.pinned,
                                              isText: tile.isText })
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
        onTapped: {
            if (tile.view.selectedIds.length > 0) {
                tile.selectRequested(tile.index, tile.itemId, "toggle")
                return
            }
            tile.previewRequested({
            id: tile.itemId, name: tile.name, url: tile.fileUrl, isImage: tile.isImage, isText: tile.isText,
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
