import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// The app dock: its own window on one edge of each monitor (bottom, left or right), like the macOS Dock.
// Pinned apps, a divider, then the other running apps. Icons grow a little as the pointer nears them and
// their neighbours make room; drag a pinned icon to reorder it (or off the dock to remove it), drag a
// running app among the pinned ones to keep it. Click brings the app forward (or starts it), middle-click
// opens a new window, right-click for more, + adds an installed app.
// With auto-hide it waits past the edge until the pointer touches that edge, and stays while the pointer
// is anywhere over the dock or between it and the edge.
// Size, magnification, opacity, blur, animation and edge come from Settings (dock.*).
Window {
    id: root
    readonly property string side: dock.position
    readonly property bool vertical: side !== "bottom"
    // Room inside the window for the app picker and menus; everything outside the bar is click-through.
    width: vertical ? 520 : Math.min(1400, Screen.width > 0 ? Screen.width : 1400)
    height: vertical ? Math.min(1200, Screen.height > 0 ? Screen.height : 1200) : 520
    visible: false
    color: "transparent"
    title: "Olyzano Notch Dock"

    readonly property int iconSize: dock.iconSize
    readonly property real magnify: dock.magnification
    readonly property int slotSize: iconSize + 14       // along the dock
    readonly property int thickness: iconSize + 22      // across it
    readonly property int dividerSize: 15
    readonly property int plusSize: 44
    readonly property int trashSize: dock.showTrash ? dividerSize + slotSize : 0
    readonly property int padding: 10
    readonly property int edgeGap: 8
    readonly property real headroom: iconSize * (magnify - 1) + 16 // icons grow this far inwards
    readonly property bool animated: dock.animation !== "none"
    readonly property real barRadius: Math.min(thickness / 2.6, theme.washi ? theme.chipRadius + 8 : 22)

    readonly property bool overlayOpen: appMenu.visible || picker.visible || folderDialog.visible || trashMenu.visible || emptyConfirm.visible
                                        || windowList.visible || stack.visible
    property bool dropHover: false     // files dragged over the dock from another app
    property int dropTargetIndex: -1   // the icon they would be opened with
    property int listIndex: -1         // the app whose windows are listed (after hovering it a moment)
    property int stackIndex: -1        // the folder whose newest files are fanned out
    property bool edgeDwelled: false
    readonly property bool engaged: !dock.autoHide || edgeDwelled || zoneHover.hovered || overlayOpen || dragIndex >= 0
                                    || dropHover || dropTargetIndex >= 0
    property bool shown: !dock.autoHide

    // `notch --debug-dock` prints what holds the dock up.
    readonly property bool debugDock: Qt.application.arguments.indexOf("--debug-dock") >= 0
    onEngagedChanged: {
        if (debugDock)
            console.log("dock engaged=" + engaged, "autoHide=" + dock.autoHide, "edge=" + edgeDwelled, "zone=" + zoneHover.hovered,
                        "overlay=" + overlayOpen, "drag=" + dragIndex, "dropHover=" + dropHover, "dropTarget=" + dropTargetIndex)
        if (engaged) {
            hideTimer.stop()
            shown = true
        } else {
            hideTimer.restart()
        }
    }
    onShownChanged: dock.setWatching(shown)
    onVisibleChanged: {
        dock.setWatching(visible && shown)
        desktop.setInputRegion(root, maskRect)
        desktop.setBlurRegion(root, blurRect, barRadius)
    }
    Timer { id: hideTimer; interval: 400; onTriggered: root.shown = false }
    Timer { id: dwellTimer; interval: 120; onTriggered: root.edgeDwelled = true }
    // If a drag ever ends without telling us (the source went away), the dock still lets go.
    Timer { id: dropGuard; interval: 15000; onTriggered: { root.dropHover = false; if (root.dropTargetIndex === 9999) root.dropTargetIndex = -1 } }

    // ---- Geometry. The layout is worked out unmagnified, centred on the window, so the pointer can be
    // compared with fixed slot centres (magnifying doesn't move the target under the pointer). ----
    readonly property real mainCenter: vertical ? height / 2 : width / 2
    readonly property int pinnedCount: {
        let n = 0
        for (const item of dock.items)
            if (item.pinned)
                ++n
        return n
    }
    readonly property var baseCenters: {
        const items = dock.items
        const offsets = []
        let pos = 0
        for (let i = 0; i < items.length; ++i) {
            const divider = i > 0 && !items[i].pinned && items[i - 1].pinned ? dividerSize : 0
            offsets.push(pos + divider + slotSize / 2)
            pos += divider + slotSize
        }
        const start = mainCenter - (pos + plusSize + trashSize) / 2
        return offsets.map(o => start + o)
    }
    // Pointer position along the dock, in window coordinates; NaN when it's elsewhere or dragging.
    readonly property real pointer: zoneHover.hovered && dragIndex < 0
                                    ? (vertical ? zone.y + zoneHover.point.position.y : zone.x + zoneHover.point.position.x)
                                    : NaN
    readonly property int hoveredIndex: {
        if (isNaN(pointer))
            return -1
        for (let i = 0; i < baseCenters.length; ++i)
            if (Math.abs(pointer - baseCenters[i]) <= slotSize / 2)
                return i
        return -1
    }

    // Rest on an app with several windows for a moment and its window titles are listed.
    onHoveredIndexChanged: {
        if (hoveredIndex >= 0 && hoveredIndex !== listIndex)
            listTimer.restart()
        else if (hoveredIndex < 0)
            listTimer.stop()
        listCloser.restart()
    }
    Timer {
        id: listTimer
        interval: 550
        onTriggered: {
            const item = dock.items[root.hoveredIndex]
            root.listIndex = item && item.windows.length > 1 && root.stackIndex < 0 ? root.hoveredIndex : -1
        }
    }
    Timer {
        id: listCloser
        interval: 350
        onTriggered: if (!listHover.hovered && root.hoveredIndex !== root.listIndex) root.listIndex = -1
    }
    // Along-the-dock position of a slot and the cross position just inside the bar, for popups beside it.
    function popupX(index, w) {
        if (side === "left")
            return bar.restX + bar.width + headroom - 8
        if (side === "right")
            return bar.restX - headroom - w + 8
        return Math.max(8, Math.min(width - w - 8, baseCenters[index] - w / 2))
    }
    function popupY(index, h) {
        if (vertical)
            return Math.max(8, Math.min(height - h - 8, baseCenters[index] - h / 2))
        return bar.restY - headroom - h + 8
    }

    // The bar at rest plus the space its icons grow into, out to the screen edge.
    function zoneRect() {
        if (side === "left")
            return Qt.rect(0, bar.restY, bar.restX + bar.width + headroom, bar.height)
        if (side === "right")
            return Qt.rect(bar.restX - headroom, bar.restY, width - bar.restX + headroom, bar.height)
        return Qt.rect(bar.restX, bar.restY - headroom, bar.width, height - bar.restY + headroom)
    }
    // Live while hidden: a thin strip on the screen edge, as long as the dock.
    function edgeRect() {
        if (side === "bottom") {
            const w = Math.max(320, bar.width)
            return Qt.rect((width - w) / 2, height - 2, w, 2)
        }
        const h = Math.max(320, bar.height)
        return Qt.rect(side === "left" ? 0 : width - 2, (height - h) / 2, 2, h)
    }
    // Gaming mode over a full-screen app: nothing here can be reached (a corner pixel stays; an empty mask means none).
    readonly property rect maskRect: gaming.active ? Qt.rect(0, 0, 1, 1)
                                   : overlayOpen || dragIndex >= 0 ? Qt.rect(0, 0, width, height)
                                   : shown ? zoneRect() : edgeRect()
    onMaskRectChanged: desktop.setInputRegion(root, maskRect)
    readonly property rect blurRect: shown && dock.blur ? Qt.rect(bar.x, bar.y, bar.width, bar.height) : Qt.rect(0, 0, 0, 0)
    onBlurRectChanged: desktop.setBlurRegion(root, blurRect, barRadius)

    // ---- Drag and drop ----
    property int dragIndex: -1
    property string dragId: ""
    property real dragDx: 0
    property real dragDy: 0
    property real dragPos: 0   // along the dock
    property real dragCross: 0 // across it
    readonly property bool dragPinned: dragIndex >= 0 && dragIndex < dock.items.length && dock.items[dragIndex].pinned === true
    // Pulled well off the dock: dropping removes it.
    readonly property bool dragOut: dragPinned && (side === "left" ? dragCross > bar.restX + bar.width + 50
                                                 : side === "right" ? dragCross < bar.restX - 50
                                                 : dragCross < bar.restY - 50)
    // Where the dragged app would land among the pinned ones, or -1.
    readonly property int dropIndex: {
        if (dragIndex < 0 || dragOut)
            return -1
        if (dragPinned) {
            let best = 0
            for (let i = 1; i < pinnedCount; ++i)
                if (Math.abs(dragPos - baseCenters[i]) < Math.abs(dragPos - baseCenters[best]))
                    best = i
            return best
        }
        // A running app joins the pinned ones when dropped before the first running slot.
        if (dragPos > baseCenters[pinnedCount] - slotSize / 2)
            return -1
        let n = 0
        for (let i = 0; i < pinnedCount; ++i)
            if (baseCenters[i] < dragPos)
                ++n
        return n
    }
    function finishDrag() {
        const id = dragId, pinned = dragPinned, out = dragOut, target = dropIndex
        dragIndex = -1
        dragId = ""
        if (pinned && out)
            dock.unpin(id)
        else if (target >= 0)
            dock.pinAt(id, target)
    }

    // ---- Glass bar ----
    Item {
        id: bar
        readonly property real length: grid.width + 2 * root.padding
        readonly property real restX: root.side === "left" ? root.edgeGap
                                     : root.side === "right" ? root.width - width - root.edgeGap
                                     : root.mainCenter - width / 2
        readonly property real restY: root.vertical ? root.mainCenter - height / 2 : root.height - height - root.edgeGap
        // 0 = in place, 1 = tucked past the edge.
        property real tucked: root.shown ? 0 : 1
        readonly property real away: root.thickness + root.edgeGap + 16
        x: restX + (root.side === "left" ? -away : root.side === "right" ? away : 0) * tucked
        y: restY + (root.vertical ? 0 : away) * tucked
        width: root.vertical ? root.thickness : grid.width + 2 * root.padding
        height: root.vertical ? grid.height + 2 * root.padding : root.thickness
        Behavior on tucked {
            enabled: root.animated
            SpringAnimation {
                spring: dock.animation === "bouncy" ? 3.6 : 5
                damping: !root.shown ? 1 : dock.animation === "bouncy" ? 0.28 : 0.6
                epsilon: 0.004
            }
        }

        // Soft shadow: rings outside the edge, so a see-through bar doesn't darken.
        Repeater {
            model: [[2, 0.10], [5, 0.07], [9, 0.04]]
            Rectangle {
                required property var modelData
                anchors.fill: parent
                anchors.margins: -modelData[0]
                anchors.topMargin: -modelData[0] + (root.vertical ? 0 : 2)
                radius: root.barRadius + modelData[0]
                color: "transparent"
                border.width: modelData[0]
                border.color: Qt.rgba(0, 0, 0, modelData[1])
            }
        }
        Rectangle {
            id: glass
            anchors.fill: parent
            radius: root.barRadius
            color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, dock.backgroundOpacity)
            border.width: 1
            border.color: theme.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.12)
        }
        // Sheen from the inner side, and a hairline of light just inside the edge.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: root.barRadius - 1
            gradient: Gradient {
                orientation: root.vertical ? Gradient.Horizontal : Gradient.Vertical
                GradientStop { position: 0; color: root.side === "right" ? "transparent" : Qt.rgba(1, 1, 1, theme.dark ? 0.10 : 0.35) }
                GradientStop { position: 0.55; color: "transparent" }
                GradientStop { position: 1; color: root.side === "right" ? Qt.rgba(1, 1, 1, theme.dark ? 0.10 : 0.35) : "transparent" }
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1.5
            radius: root.barRadius - 1.5
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, theme.dark ? 0.06 : 0.4)
        }

        // Drop marker when a running app is dragged among the pinned ones.
        Rectangle {
            readonly property real at: {
                const t = root.dropIndex
                if (t < 0 || root.dragPinned)
                    return NaN
                return t < root.pinnedCount || root.pinnedCount === 0 ? root.baseCenters[t] - root.slotSize / 2
                                                                      : root.baseCenters[root.pinnedCount - 1] + root.slotSize / 2
            }
            visible: !isNaN(at)
            x: root.vertical ? 8 : at - bar.x - 1.5
            y: root.vertical ? at - bar.y - 1.5 : 8
            width: root.vertical ? parent.width - 16 : 3
            height: root.vertical ? 3 : parent.height - 16
            radius: 1.5
            color: theme.accent
        }

        DropArea {
            anchors.fill: parent
            anchors.margins: -root.headroom
            // Accepted on purpose: a DropArea that rejects a drag is never told when it leaves, which left
            // dropHover on for good and the dock stuck on screen after any file was dragged over it.
            onEntered: (drag) => {
                root.dropHover = drag.hasUrls
                dropGuard.restart()
            }
            onExited: root.dropHover = false
            onDropped: (drop) => {
                root.dropHover = false
                drop.accepted = false // the icons decide where files go; the space between them takes nothing
            }
        }

        Grid {
            id: grid
            anchors.centerIn: parent
            flow: root.vertical ? Grid.TopToBottom : Grid.LeftToRight
            rows: root.vertical ? -1 : 1
            columns: root.vertical ? 1 : -1

            Repeater {
                model: dock.items
                delegate: Item {
                    id: slot
                    required property var modelData
                    required property int index
                    readonly property bool divider: index > 0 && !modelData.pinned && dock.items[index - 1].pinned
                    readonly property int dividerSize: divider ? root.dividerSize : 0
                    readonly property bool dragged: root.dragIndex === index
                    // Cosine falloff over ~2.5 slots around the pointer.
                    readonly property real targetMag: {
                        if (isNaN(root.pointer) || root.magnify <= 1 || index >= root.baseCenters.length)
                            return 1
                        const d = Math.abs(root.pointer - root.baseCenters[index])
                        const r = root.slotSize * 2.5
                        return d >= r ? 1 : 1 + (root.magnify - 1) * (Math.cos(Math.PI * d / r) + 1) / 2
                    }
                    property real mag: targetMag
                    Behavior on mag { enabled: root.animated; NumberAnimation { duration: 110; easing.type: Easing.OutQuad } }
                    // Pinned icons step aside while another pinned icon is dragged past them.
                    readonly property real shift: {
                        const f = root.dragIndex, t = root.dropIndex
                        if (f < 0 || dragged || t < 0 || !root.dragPinned)
                            return 0
                        if (f < t && index > f && index <= t)
                            return -root.slotSize
                        if (f > t && index >= t && index < f)
                            return root.slotSize
                        return 0
                    }
                    property real offset: shift
                    Behavior on offset { enabled: root.animated; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                    width: root.vertical ? root.thickness : dividerSize + root.slotSize * mag
                    height: root.vertical ? dividerSize + root.slotSize * mag : root.thickness
                    z: dragged ? 3 : index === root.hoveredIndex ? 2 : 0

                    Rectangle {
                        visible: slot.divider
                        x: root.vertical ? (parent.width - width) / 2 : (slot.dividerSize - width) / 2
                        y: root.vertical ? (slot.dividerSize - height) / 2 : (parent.height - height) / 2
                        width: root.vertical ? root.iconSize * 0.8 : 1
                        height: root.vertical ? 1 : root.iconSize * 0.8
                        color: theme.dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.18)
                    }

                    Item {
                        id: cell
                        x: root.vertical ? 0 : slot.dividerSize
                        y: root.vertical ? slot.dividerSize : 0
                        width: root.vertical ? root.thickness : root.slotSize * slot.mag
                        height: root.vertical ? root.slotSize * slot.mag : root.thickness
                        transform: Translate {
                            x: slot.dragged ? root.dragDx : root.vertical ? 0 : slot.offset
                            y: slot.dragged ? root.dragDy : root.vertical ? slot.offset : 0
                        }

                        Image {
                            id: icon
                            property real lift: 0
                            readonly property real size: root.iconSize * slot.mag
                            // Anchored to the screen-edge side, so it grows inwards.
                            x: root.side === "left" ? 14 : root.side === "right" ? cell.width - 14 - size : (cell.width - size) / 2
                            y: root.vertical ? (cell.height - size) / 2 : cell.height - 14 - size
                            width: size
                            height: size
                            source: slot.modelData.iconSource
                            sourceSize: Qt.size(128, 128)
                            smooth: true
                            mipmap: true
                            opacity: root.dragOut && slot.dragged ? 0.45
                                   : slot.modelData.canLaunch || slot.modelData.running ? 1 : 0.45 // pinned but uninstalled
                            transform: Translate {
                                x: root.side === "left" ? icon.lift : root.side === "right" ? -icon.lift : 0
                                y: root.vertical ? 0 : -icon.lift
                            }
                        }
                        // Starting an app: a hop (two when the animation is bouncy).
                        SequentialAnimation {
                            id: bounce
                            loops: dock.animation === "bouncy" ? 2 : 1
                            NumberAnimation { target: icon; property: "lift"; to: root.iconSize * 0.3; duration: 220; easing.type: Easing.OutQuad }
                            NumberAnimation { target: icon; property: "lift"; to: 0; duration: 240; easing.type: Easing.OutBounce }
                        }

                        // Asking for attention: a hop now and then.
                        SequentialAnimation {
                            running: slot.modelData.urgent === true && root.animated
                            loops: Animation.Infinite
                            NumberAnimation { target: icon; property: "lift"; to: root.iconSize * 0.18; duration: 200; easing.type: Easing.OutQuad }
                            NumberAnimation { target: icon; property: "lift"; to: 0; duration: 280; easing.type: Easing.OutBounce }
                            PauseAnimation { duration: 1400 }
                        }
                        // Files dragged onto this icon: they'll open with it.
                        Rectangle {
                            visible: root.dropTargetIndex === slot.index
                            x: icon.x - 4; y: icon.y - 4
                            width: icon.size + 8; height: icon.size + 8
                            radius: 12
                            color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.18)
                            border.width: 2
                            border.color: theme.accent
                        }
                        // Unread count the app reports (Telegram, Thunderbird, …).
                        Rectangle {
                            visible: slot.modelData.badge > 0
                            x: icon.x + icon.size - width * 0.7
                            y: icon.y - height * 0.3
                            width: Math.max(18, badgeText.implicitWidth + 10)
                            height: 18
                            radius: 9
                            color: theme.danger
                            border.width: 1.5
                            border.color: Qt.rgba(0, 0, 0, 0.25)
                            Text {
                                id: badgeText
                                anchors.centerIn: parent
                                text: slot.modelData.badge > 99 ? "99+" : slot.modelData.badge
                                color: "white"
                                font.pixelSize: 10
                                font.weight: Font.Bold
                            }
                        }
                        // Progress the app reports (a download, a render).
                        Rectangle {
                            visible: slot.modelData.progress >= 0
                            x: icon.x + 4
                            y: icon.y + icon.size - 8
                            width: icon.size - 8
                            height: 5
                            radius: 2.5
                            color: Qt.rgba(0, 0, 0, 0.5)
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, slot.modelData.progress))
                                height: parent.height
                                radius: 2.5
                                color: theme.accent
                            }
                        }
                        DropArea {
                            anchors.fill: parent
                            onEntered: (drag) => {
                                drag.accepted = drag.hasUrls && slot.modelData.kind === "app" && slot.modelData.canLaunch === true
                                if (drag.accepted)
                                    root.dropTargetIndex = slot.index
                            }
                            onExited: if (root.dropTargetIndex === slot.index) root.dropTargetIndex = -1
                            onDropped: (drop) => {
                                root.dropTargetIndex = -1
                                root.dropHover = false
                                if (!drop.hasUrls)
                                    return
                                dock.openWith(slot.modelData.id, drop.urls.map(u => u.toString()))
                                drop.accept(Qt.CopyAction)
                                if (root.animated)
                                    bounce.restart()
                            }
                        }

                        // Running: a small dot. Active (in front): a longer accent pill with a glow. Not running: nothing.
                        Item {
                            visible: slot.modelData.running === true
                            readonly property bool active: slot.modelData.active === true
                            property real length: active ? 14 : 5
                            x: root.side === "left" ? 4 : root.side === "right" ? cell.width - 9 : (cell.width - width) / 2
                            y: root.vertical ? (cell.height - height) / 2 : cell.height - 9
                            width: root.vertical ? 5 : length
                            height: root.vertical ? length : 5
                            Behavior on length { enabled: root.animated; NumberAnimation { duration: 160 } }
                            Rectangle {
                                visible: parent.active
                                anchors.centerIn: parent
                                width: parent.width + 6
                                height: parent.height + 6
                                radius: Math.min(width, height) / 2
                                color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.3)
                            }
                            Rectangle {
                                anchors.fill: parent
                                radius: 2.5
                                color: parent.active ? theme.accent : theme.dark ? Qt.rgba(1, 1, 1, 0.65) : Qt.rgba(0, 0, 0, 0.55)
                            }
                        }

                        // Name label beside the hovered icon, away from the edge ("Remove" while being dragged off).
                        Rectangle {
                            readonly property bool removing: slot.dragged && root.dragOut
                            visible: removing || (slot.index === root.hoveredIndex && root.dragIndex < 0 && !root.overlayOpen
                                                  && root.dropTargetIndex < 0)
                            x: root.side === "left" ? icon.x + icon.size + 12
                             : root.side === "right" ? icon.x - width - 12
                             : (cell.width - width) / 2
                            y: root.vertical ? (cell.height - height) / 2 : icon.y - height - 10
                            width: nameText.implicitWidth + 22
                            height: 26
                            radius: 13
                            color: removing ? theme.danger : theme.surface
                            border.width: 1
                            border.color: theme.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.1)
                            Text {
                                id: nameText
                                anchors.centerIn: parent
                                text: parent.removing ? "Remove" : slot.modelData.name
                                color: parent.removing ? "white" : theme.text
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }
                        }

                        MouseArea {
                            id: tap
                            property point pressAt
                            property bool moved: false
                            anchors.fill: parent
                            preventStealing: true
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                            onPressed: (mouse) => {
                                pressAt = mapToItem(null, mouse.x, mouse.y)
                                moved = false
                            }
                            onPositionChanged: (mouse) => {
                                if (!(pressedButtons & Qt.LeftButton))
                                    return
                                const p = mapToItem(null, mouse.x, mouse.y)
                                const app = slot.modelData
                                if (!moved && Math.hypot(p.x - pressAt.x, p.y - pressAt.y) > 8 && (app.pinned || app.canLaunch)) {
                                    moved = true
                                    root.dragId = app.id
                                    root.dragIndex = slot.index
                                }
                                if (moved) {
                                    root.dragDx = p.x - pressAt.x
                                    root.dragDy = p.y - pressAt.y
                                    root.dragPos = root.vertical ? p.y : p.x
                                    root.dragCross = root.vertical ? p.x : p.y
                                }
                            }
                            onReleased: if (moved) root.finishDrag()
                            onCanceled: if (moved) root.finishDrag()
                            onClicked: (mouse) => {
                                if (moved)
                                    return
                                const app = slot.modelData
                                root.listIndex = -1
                                listTimer.stop()
                                if (mouse.button === Qt.RightButton) {
                                    appMenu.app = app
                                    appMenu.popup()
                                } else if (app.kind === "folder") {
                                    if (mouse.button === Qt.MiddleButton)
                                        dock.activate(app.id)
                                    else
                                        root.stackIndex = root.stackIndex === slot.index ? -1 : slot.index
                                } else if (mouse.button === Qt.MiddleButton) {
                                    if (app.canLaunch) {
                                        if (root.animated)
                                            bounce.restart()
                                        dock.launchNew(app.id)
                                    }
                                } else {
                                    if (!app.running && app.canLaunch && root.animated)
                                        bounce.restart()
                                    dock.activate(app.id)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                width: root.vertical ? root.thickness : root.plusSize
                height: root.vertical ? root.plusSize : root.thickness
                IconButton {
                    anchors.centerIn: parent
                    iconName: "plus"
                    Accessible.name: "Add an app to the dock"
                    onClicked: picker.open()
                }
            }

            // Trash, after a divider: drop files to move them to the trash, click to open it.
            Item {
                id: trashSlot
                visible: dock.showTrash
                property bool dropping: false
                width: root.vertical ? root.thickness : root.trashSize
                height: root.vertical ? root.trashSize : root.thickness
                Rectangle {
                    x: root.vertical ? (parent.width - width) / 2 : (root.dividerSize - width) / 2
                    y: root.vertical ? (root.dividerSize - height) / 2 : (parent.height - height) / 2
                    width: root.vertical ? root.iconSize * 0.8 : 1
                    height: root.vertical ? 1 : root.iconSize * 0.8
                    color: theme.dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.18)
                }
                Image {
                    id: trashIcon
                    property real size: root.iconSize * (trashSlot.dropping ? 1.15 : 1)
                    x: (root.vertical ? 0 : root.dividerSize) + (root.side === "left" ? 14 : root.side === "right" ? root.thickness - 14 - size : (root.slotSize - size) / 2)
                    y: (root.vertical ? root.dividerSize : 0) + (root.vertical ? (root.slotSize - size) / 2 : root.thickness - 14 - size)
                    width: size
                    height: size
                    source: dock.trashIcon
                    sourceSize: Qt.size(128, 128)
                    smooth: true
                    mipmap: true
                    Behavior on size { NumberAnimation { duration: 120 } }
                }
                Rectangle { // name label, like the apps'
                    visible: trashArea.containsMouse && zoneHover.hovered && !root.overlayOpen // containsMouse alone sticks when the dock hides under the pointer
                    x: root.side === "left" ? trashIcon.x + trashIcon.size + 12 : root.side === "right" ? trashIcon.x - width - 12 : trashIcon.x + (trashIcon.size - width) / 2
                    y: root.vertical ? trashIcon.y + (trashIcon.size - height) / 2 : trashIcon.y - height - 10
                    width: trashLabel.implicitWidth + 22
                    height: 26
                    radius: 13
                    color: theme.surface
                    border.width: 1
                    border.color: theme.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.1)
                    Text {
                        id: trashLabel
                        anchors.centerIn: parent
                        text: trashSlot.dropping ? "Move to Trash" : "Trash"
                        color: theme.text
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }
                MouseArea {
                    id: trashArea
                    anchors.fill: parent
                    hoverEnabled: true // only the trash's own label; the dock's hover zone is separate
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (mouse) => mouse.button === Qt.RightButton ? trashMenu.popup() : dock.openTrash()
                }
                DropArea {
                    anchors.fill: parent
                    onEntered: (drag) => {
                        drag.accepted = drag.hasUrls
                        trashSlot.dropping = drag.accepted
                        root.dropTargetIndex = drag.accepted ? 9999 : root.dropTargetIndex // keeps the dock up
                    }
                    onExited: {
                        trashSlot.dropping = false
                        if (root.dropTargetIndex === 9999)
                            root.dropTargetIndex = -1
                    }
                    onDropped: (drop) => {
                        trashSlot.dropping = false
                        root.dropTargetIndex = -1
                        root.dropHover = false
                        if (drop.hasUrls && dock.moveToTrash(drop.urls.map(u => u.toString())) > 0)
                            drop.accept(Qt.MoveAction)
                    }
                }
            }
        }
    }

    // On top of the bar and only listening, so hover is seen everywhere over the dock (icons included)
    // while clicks and drags still reach the icons underneath.
    Item {
        id: zone
        readonly property rect r: root.zoneRect()
        x: r.x; y: r.y; width: r.width; height: r.height
        HoverHandler { id: zoneHover }
    }
    Item {
        readonly property rect r: root.edgeRect()
        x: r.x; y: r.y; width: r.width; height: r.height
        HoverHandler {
            onHoveredChanged: {
                if (hovered) {
                    dwellTimer.start()
                } else {
                    dwellTimer.stop()
                    root.edgeDwelled = false
                }
            }
        }
        // Dragging files to the edge brings the dock up, so they can be dropped on an app.
        DropArea {
            anchors.fill: parent
            anchors.margins: -2
            onEntered: (drag) => {
                root.dropHover = drag.hasUrls
                dropGuard.restart()
            }
            onExited: root.dropHover = false
            onDropped: (drop) => {
                root.dropHover = false
                drop.accepted = false
            }
        }
    }

    // ---- Windows of the app under the pointer ----
    Rectangle {
        id: windowList
        readonly property var app: root.listIndex >= 0 && root.listIndex < dock.items.length ? dock.items[root.listIndex] : null
        visible: app !== null && app.windows.length > 1 && root.shown
        width: 280
        height: listColumn.implicitHeight + 16
        x: root.popupX(Math.max(0, root.listIndex), width)
        y: root.popupY(Math.max(0, root.listIndex), height)
        radius: 14
        color: theme.surface
        border.color: theme.outline
        HoverHandler {
            id: listHover
            onHoveredChanged: if (!hovered) listCloser.restart()
        }
        Column {
            id: listColumn
            x: 8; y: 8
            width: parent.width - 16
            spacing: 2
            Text {
                leftPadding: 6
                bottomPadding: 4
                text: windowList.app ? windowList.app.name : ""
                color: theme.textDim
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Repeater {
                model: windowList.app ? windowList.app.windows : []
                Rectangle {
                    required property var modelData
                    width: listColumn.width
                    height: 30
                    radius: 8
                    color: rowArea.containsMouse ? theme.surfaceHover : "transparent"
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        text: parent.modelData.title || "Untitled window"
                        color: theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            dock.activateWindow(parent.modelData.id)
                            root.listIndex = -1
                        }
                    }
                }
            }
        }
    }

    // ---- A kept folder's newest files, like a Stack ----
    MouseArea { // a click anywhere else closes it
        anchors.fill: parent
        visible: stack.visible
        onClicked: root.stackIndex = -1
    }
    Rectangle {
        id: stack
        readonly property var folder: root.stackIndex >= 0 && root.stackIndex < dock.items.length ? dock.items[root.stackIndex] : null
        property var entries: []
        onFolderChanged: entries = folder ? dock.folderEntries(folder.id, 16) : []
        visible: folder !== null && folder.kind === "folder" && root.shown
        width: 4 * 84 + 24
        height: stackColumn.implicitHeight + 24
        x: root.popupX(Math.max(0, root.stackIndex), width)
        y: root.popupY(Math.max(0, root.stackIndex), height)
        radius: 18
        color: theme.surface
        border.color: theme.outline
        focus: visible
        Keys.onEscapePressed: root.stackIndex = -1
        ColumnLayout {
            id: stackColumn
            x: 12; y: 12
            width: parent.width - 24
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: stack.folder ? stack.folder.name : ""
                    color: theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                PillButton {
                    text: "Open folder"
                    onClicked: {
                        dock.activate(stack.folder.id)
                        root.stackIndex = -1
                    }
                }
            }
            Text {
                visible: stack.entries.length === 0
                text: "This folder is empty."
                color: theme.textDim
                font.pixelSize: 12
            }
            Grid {
                columns: 4
                spacing: 0
                Repeater {
                    model: stack.entries
                    Item {
                        id: entry
                        required property var modelData
                        width: 84
                        height: 84
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 2
                            radius: 10
                            color: entryArea.containsMouse ? theme.surfaceHover : "transparent"
                        }
                        Image {
                            id: entryIcon
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 8
                            width: 40
                            height: 40
                            source: "image://mime/" + entry.modelData.iconName
                            sourceSize: Qt.size(80, 80)
                            // Dragging it out hands the file to another app.
                            Drag.active: entryArea.drag.active
                            Drag.dragType: Drag.Automatic
                            Drag.supportedActions: Qt.CopyAction
                            Drag.mimeData: ({ "text/uri-list": entry.modelData.url })
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 52
                            width: parent.width - 8
                            horizontalAlignment: Text.AlignHCenter
                            text: entry.modelData.name
                            color: theme.text
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                            maximumLineCount: 2
                            wrapMode: Text.WrapAnywhere
                        }
                        MouseArea {
                            id: entryArea
                            anchors.fill: parent
                            hoverEnabled: true
                            drag.target: entryIcon
                            onClicked: {
                                Qt.openUrlExternally(entry.modelData.url)
                                root.stackIndex = -1
                            }
                        }
                    }
                }
            }
        }
    }

    DarkMenu {
        id: trashMenu
        Action { text: "Open Trash"; onTriggered: dock.openTrash() }
        Action { text: "Empty Trash…"; enabled: dock.trashFull; onTriggered: emptyConfirm.open() }
    }
    Popup {
        id: emptyConfirm
        x: root.side === "left" ? bar.restX + bar.width + 14 : root.side === "right" ? bar.restX - width - 14 : (root.width - width) / 2
        y: root.vertical ? (root.height - height) / 2 : bar.restY - height - 14
        width: 300
        padding: 16
        background: Rectangle { radius: 16; color: theme.surface; border.color: theme.outline }
        contentItem: ColumnLayout {
            spacing: 10
            SectionLabel { text: "Empty the Trash?" }
            Caption { Layout.fillWidth: true; text: "Everything in it is deleted for good. This can't be undone." }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: emptyConfirm.close() }
                PillButton {
                    text: "Empty Trash"
                    checked: true
                    onClicked: {
                        dock.emptyTrash()
                        emptyConfirm.close()
                    }
                }
            }
        }
    }

    DarkMenu {
        id: appMenu
        property var app: ({})
        Action {
            text: appMenu.app.running ? "Show" : "Open"
            enabled: appMenu.app.running || appMenu.app.canLaunch
            onTriggered: dock.activate(appMenu.app.id)
        }
        Action {
            text: "New window"
            enabled: appMenu.app.canLaunch === true && appMenu.app.kind === "app"
            onTriggered: dock.launchNew(appMenu.app.id)
        }
        DarkSeparator {}
        Action {
            text: appMenu.app.pinned ? "Remove from Dock" : "Keep in Dock"
            enabled: appMenu.app.pinned || appMenu.app.canLaunch
            onTriggered: appMenu.app.pinned ? dock.unpin(appMenu.app.id) : dock.pin(appMenu.app.id)
        }
        Action {
            text: "Quit"
            enabled: appMenu.app.running === true
            onTriggered: dock.close(appMenu.app.id)
        }
    }

    Popup {
        id: picker
        property string filter
        x: root.side === "left" ? bar.restX + bar.width + 14
         : root.side === "right" ? bar.restX - width - 14
         : (root.width - width) / 2
        y: root.vertical ? Math.max(8, Math.min(root.height - height - 8, (root.height - height) / 2))
                         : bar.restY - height - 14
        width: 320
        height: 380
        padding: 12
        focus: true
        onOpened: {
            filter = ""
            search.text = ""
            desktop.setKeyboardInteractive(root, true)
            search.forceActiveFocus()
        }
        onClosed: desktop.setKeyboardInteractive(root, false)
        background: Rectangle { radius: 16; color: theme.surface; border.color: theme.outline }
        contentItem: ColumnLayout {
            spacing: 8
            SectionLabel { text: "Keep in Dock" }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                PillButton {
                    text: "Downloads"
                    iconName: "download"
                    onClicked: {
                        dock.pinFolder(dock.downloadsFolder())
                        picker.close()
                    }
                }
                PillButton {
                    text: "Add folder…"
                    iconName: "plus"
                    onClicked: folderDialog.open()
                }
            }
            TextField {
                id: search
                Layout.fillWidth: true
                implicitHeight: 30
                leftPadding: 12
                placeholderText: "Search apps"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 13
                background: Rectangle { radius: 15; color: theme.well; border.width: 1; border.color: theme.accent }
                onTextChanged: picker.filter = text
                Keys.onEscapePressed: picker.close()
            }
            ListView {
                id: apps
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // Re-queried on every keystroke and after each pin so the check marks stay right.
                property int revision: 0
                model: picker.visible ? (apps.revision, dock.installedApps(picker.filter)) : []
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: ItemDelegate {
                    id: entry
                    required property var modelData
                    width: apps.width
                    height: 36
                    padding: 6
                    onClicked: {
                        modelData.pinned ? dock.unpin(modelData.id) : dock.pin(modelData.id)
                        apps.revision++
                    }
                    background: Rectangle { radius: 8; color: entry.hovered ? theme.surfaceHover : "transparent" }
                    contentItem: RowLayout {
                        spacing: 10
                        Image {
                            source: entry.modelData.iconSource
                            sourceSize: Qt.size(48, 48)
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                        }
                        Text {
                            Layout.fillWidth: true
                            text: entry.modelData.name
                            color: theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: entry.modelData.pinned
                            text: "✓"
                            color: theme.accent
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Choose a folder to keep in the dock"
        onAccepted: {
            dock.pinFolder(selectedFolder.toString())
            picker.close()
        }
    }
}
