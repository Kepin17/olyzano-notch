import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The app dock: its own window on one edge of each monitor (bottom, left or right), like the macOS Dock.
// Pinned apps, a divider, then the other running apps (dot on the screen-edge side). Click brings the app
// forward (or starts it), middle-click opens a new window, right-click for more, + pins an installed app.
// With auto-hide it waits past the edge until the pointer rests on that edge of the screen.
// Size, magnification, opacity and edge come from Settings (dock.*).
Window {
    id: root
    readonly property string side: dock.position
    readonly property bool vertical: side !== "bottom"
    // Room inside the window for the app picker and menus; everything outside the bar is click-through.
    width: vertical ? 520 : Math.min(1400, Screen.width > 0 ? Screen.width : 1400)
    height: vertical ? Math.min(1200, Screen.height > 0 ? Screen.height : 1200) : 520
    visible: false
    color: "transparent"
    title: "Notch Dock"

    readonly property int iconSize: dock.iconSize
    readonly property real magnify: dock.magnification
    readonly property int slotSize: iconSize + 12      // along the dock
    readonly property int thickness: iconSize + 22     // across it
    readonly property int edgeGap: 6
    readonly property real headroom: iconSize * (magnify - 1) + 12 // icons grow this far inwards
    readonly property bool overlayOpen: appMenu.visible || picker.visible
    property bool edgeDwelled: false
    readonly property bool engaged: !dock.autoHide || edgeDwelled || zoneHover.hovered || overlayOpen
    property bool shown: !dock.autoHide
    property int hoveredIndex: -1

    onEngagedChanged: {
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
    }
    Timer { id: hideTimer; interval: 700; onTriggered: root.shown = false }
    Timer { id: dwellTimer; interval: 150; onTriggered: root.edgeDwelled = true }

    // The bar at rest plus the space its icons grow into: hovering here keeps the dock up.
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
    readonly property rect maskRect: overlayOpen ? Qt.rect(0, 0, width, height) : shown ? zoneRect() : edgeRect()
    onMaskRectChanged: desktop.setInputRegion(root, maskRect)

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
    }

    Item {
        readonly property rect r: root.zoneRect()
        x: r.x; y: r.y; width: r.width; height: r.height
        HoverHandler { id: zoneHover }
    }

    Rectangle {
        id: bar
        readonly property real restX: root.side === "left" ? root.edgeGap
                                     : root.side === "right" ? root.width - width - root.edgeGap
                                     : (root.width - width) / 2
        readonly property real restY: root.vertical ? (root.height - height) / 2 : root.height - height - root.edgeGap
        // 0 = in place, 1 = tucked past the edge.
        property real tucked: root.shown ? 0 : 1
        readonly property real away: root.thickness + root.edgeGap + 12
        x: restX + (root.side === "left" ? -away : root.side === "right" ? away : 0) * tucked
        y: restY + (root.vertical ? 0 : away) * tucked
        width: root.vertical ? root.thickness : grid.width + 18
        height: root.vertical ? grid.height + 18 : root.thickness
        radius: Math.min(root.thickness / 3, theme.washi ? theme.chipRadius + 6 : 22)
        color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, dock.backgroundOpacity)
        border.width: 1
        border.color: Qt.rgba(theme.outline.r, theme.outline.g, theme.outline.b, theme.outline.a * Math.max(0.5, dock.backgroundOpacity))
        Behavior on tucked { SpringAnimation { spring: 4; damping: root.shown ? 0.34 : 0.9; epsilon: 0.005 } }
        Behavior on width { enabled: !root.vertical; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on height { enabled: root.vertical; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

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
                    readonly property int dividerSize: divider ? 13 : 0
                    readonly property bool hovered: root.hoveredIndex === index
                    readonly property bool near: root.hoveredIndex >= 0 && Math.abs(root.hoveredIndex - index) === 1
                    width: root.vertical ? root.thickness : root.slotSize + dividerSize
                    height: root.vertical ? root.slotSize + dividerSize : root.thickness
                    z: hovered ? 2 : near ? 1 : 0 // the grown icon and its label draw over the neighbours

                    Rectangle {
                        visible: slot.divider
                        x: root.vertical ? (parent.width - width) / 2 : 6
                        y: root.vertical ? 6 : (parent.height - height) / 2
                        width: root.vertical ? root.iconSize * 0.85 : 1
                        height: root.vertical ? 1 : root.iconSize * 0.85
                        color: theme.outline
                    }

                    Item {
                        id: cell
                        x: root.vertical ? 0 : slot.dividerSize
                        y: root.vertical ? slot.dividerSize : 0
                        width: root.vertical ? root.thickness : root.slotSize
                        height: root.vertical ? root.slotSize : root.thickness

                        Image {
                            id: icon
                            property real lift: 0
                            x: root.side === "left" ? 14 : root.side === "right" ? cell.width - width - 14 : (cell.width - width) / 2
                            y: root.vertical ? (cell.height - height) / 2 : 8
                            width: root.iconSize
                            height: root.iconSize
                            source: slot.modelData.iconSource
                            sourceSize: Qt.size(128, 128)
                            smooth: true
                            mipmap: true
                            // Grows away from the screen edge.
                            transformOrigin: root.side === "left" ? Item.Left : root.side === "right" ? Item.Right : Item.Bottom
                            scale: slot.hovered ? root.magnify : slot.near ? 1 + (root.magnify - 1) * 0.4 : 1
                            opacity: slot.modelData.canLaunch || slot.modelData.running ? 1 : 0.45 // pinned but uninstalled
                            transform: Translate {
                                x: root.side === "left" ? icon.lift : root.side === "right" ? -icon.lift : 0
                                y: root.vertical ? 0 : -icon.lift
                            }
                            Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                        }
                        // Starting an app: a couple of hops, like the Mac.
                        SequentialAnimation {
                            id: bounce
                            loops: 2
                            NumberAnimation { target: icon; property: "lift"; to: 16; duration: 230; easing.type: Easing.OutQuad }
                            NumberAnimation { target: icon; property: "lift"; to: 0; duration: 230; easing.type: Easing.InQuad }
                        }

                        Rectangle {
                            readonly property real length: slot.modelData.active ? 10 : 5
                            visible: slot.modelData.running
                            x: root.side === "left" ? 4 : root.side === "right" ? cell.width - 9 : (cell.width - width) / 2
                            y: root.vertical ? (cell.height - height) / 2 : cell.height - 9
                            width: root.vertical ? 5 : length
                            height: root.vertical ? length : 5
                            radius: 2.5
                            color: slot.modelData.active ? theme.accent : theme.textDim
                        }

                        // Name label beside the hovered icon, on the screen side away from the edge.
                        Rectangle {
                            readonly property real grown: root.iconSize * root.magnify
                            visible: slot.hovered && !root.overlayOpen
                            x: root.side === "left" ? icon.x + grown + 10
                             : root.side === "right" ? icon.x + root.iconSize - grown - width - 10
                             : (cell.width - width) / 2
                            y: root.vertical ? (cell.height - height) / 2 : icon.y + root.iconSize - grown - height - 8
                            width: nameText.implicitWidth + 20
                            height: 24
                            radius: 12
                            color: theme.surface
                            border.width: 1
                            border.color: theme.outline
                            Text {
                                id: nameText
                                anchors.centerIn: parent
                                text: slot.modelData.name
                                color: theme.text
                                font.pixelSize: 12
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                            onContainsMouseChanged: {
                                if (containsMouse)
                                    root.hoveredIndex = slot.index
                                else if (root.hoveredIndex === slot.index)
                                    root.hoveredIndex = -1
                            }
                            onClicked: (mouse) => {
                                const app = slot.modelData
                                if (mouse.button === Qt.RightButton) {
                                    appMenu.app = app
                                    appMenu.popup()
                                } else if (mouse.button === Qt.MiddleButton) {
                                    if (app.canLaunch) {
                                        bounce.restart()
                                        dock.launchNew(app.id)
                                    }
                                } else {
                                    if (!app.running && app.canLaunch)
                                        bounce.restart()
                                    dock.activate(app.id)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                width: root.vertical ? root.thickness : 40
                height: root.vertical ? 40 : root.thickness
                IconButton {
                    anchors.centerIn: parent
                    iconName: "plus"
                    onClicked: picker.open()
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
            enabled: appMenu.app.canLaunch === true
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
}
