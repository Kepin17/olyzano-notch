import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes

Window {
    id: root
    width: 720
    height: 620
    visible: false
    color: "transparent"
    title: "Notch"

    readonly property color accent: theme.accent
    readonly property color textPrimary: theme.text
    readonly property color textSecondary: theme.textDim
    readonly property int openWidth: 560

    property bool pinned: false
    property bool open: false
    property bool dwelled: false
    property string page: "shelf" // shelf | preview | settings | note
    // Quick note being written/edited. The draft survives closing Notch until it is saved.
    property string noteId: ""       // empty = new note
    property string noteName: ""
    property string noteFormat: "txt"
    property string noteText: ""
    property var preview: ({})
    property string previewText: ""
    property var selection: []          // selected item ids
    property int selectionAnchor: -1    // view index Shift+click ranges start from
    property string editingCategory: "" // "" | "new" | category id being renamed
    property string editingName: ""
    property var pendingAssign: []      // ids to move into the category being created
    property bool chipDragOver: false
    property string infoText: ""        // short confirmation shown in the panel (saved, zip ready)
    Timer { id: infoTimer; interval: 3500; onTriggered: root.infoText = "" }
    property bool playerExpanded: false // toggled by the equalizer button in the header
    property bool playerFlash: false    // full player shown briefly on new playback / track change
    Timer { id: flashTimer; interval: 6000; onTriggered: root.playerFlash = false }
    readonly property bool selecting: page === "shelf" && selection.length > 0
    readonly property bool inRemoved: notch.categoryFilter === "__removed__" // Recently removed view
    readonly property bool anyMenu: menu.visible || sendMenu.visible || assignMenu.visible || categoryMenu.visible
                                    || toolsMenu.visible || extractPopup.visible || ruleMenu.visible
                                    || sortMenu.visible || removedMenu.visible
                                    || removedChipMenu.visible
    readonly property bool dragOver: edgeDrop.containsDrag || islandDrop.containsDrag || chipDragOver
    readonly property bool engaged: pinned || dwelled || islandHover.hovered || dragOver || anyMenu
                                    || folderDialog.visible || saveDialog.visible
    // hidden: only the top-edge strip is live. peek: copy in progress. open: full panel.
    // toast: a short system alert (charger, Bluetooth). Shown even while hidden, like Dynamic Island.
    property var toast: null // {kind, title, detail, icon, level}
    readonly property color toastColor: !toast ? theme.outline
                                      : toast.kind === "charging" ? "#30d158"
                                      : toast.kind === "bluetooth" ? accent
                                      : toast.kind === "lowbattery" ? "#ff453a"
                                      : toast.kind === "media" ? "#7fb5cf"
                                      : toast.kind === "file" ? "#5ac8fa"
                                      : toast.kind === "color" ? toast.detail : theme.textDim
    readonly property string mode: open ? "open" : toast ? "toast" : notch.copying > 0 ? "peek" : "hidden"

    Timer { id: toastTimer; interval: 3200; onTriggered: root.toast = null }
    Connections {
        target: kdeconnect
        function onSent(count, deviceName, error) {
            root.infoText = error !== "" ? "Couldn't send to " + deviceName + ": " + error
                                         : "Sent " + count + (count === 1 ? " file to " : " files to ") + deviceName
            infoTimer.restart()
        }
    }
    Connections {
        target: notch
        function onNotice(title, detail, icon, art) {
            if (root.open) {
                root.infoText = title + (detail ? " · " + detail : "")
                infoTimer.restart()
                return
            }
            root.toast = { kind: icon === "color" ? "color" : "file", title: title, detail: detail, icon: icon, art: art, level: -1 }
            toastTimer.interval = 3200
            toastTimer.restart()
        }
    }
    Connections {
        target: media
        function onTrackChanged(title, artist, art) {
            root.playerFlash = true
            flashTimer.restart()
            if (!notch.alerts || root.open)
                return
            root.toast = { kind: "media", title: title, detail: artist, icon: "", art: art, level: -1 }
            toastTimer.interval = 3200
            toastTimer.restart()
        }
    }
    Connections {
        target: systemEvents
        function onAlert(kind, title, detail, icon, level) {
            if (!notch.alerts)
                return
            root.toast = { kind: kind, title: title, detail: detail, icon: icon, level: level }
            toastTimer.interval = kind === "lowbattery" ? 5000 : 3200 // warnings stay a little longer
            toastTimer.restart()
        }
    }

    onEngagedChanged: {
        if (engaged) {
            closeTimer.stop()
            open = true
        } else {
            closeTimer.restart()
        }
    }
    onOpenChanged: {
        if (open)
            return
        page = "shelf"
        editingCategory = ""
        playerExpanded = false
        clearSelection()
    }
    onAnyMenuChanged: updateMask()
    Timer { id: closeTimer; interval: 380; onTriggered: root.open = false }
    // Dwell before revealing, so flinging the mouse up to a tab doesn't pop the panel.
    Timer { id: dwellTimer; interval: 160; onTriggered: root.dwelled = true }

    // Everything outside the live area is click-through.
    function updateMask() {
        if (anyMenu)
            desktop.setInputRegion(root, Qt.rect(0, 0, width, height))
        else if (mode === "hidden")
            desktop.setInputRegion(root, Qt.rect(edge.x, 0, edge.width, edge.height))
        else
            desktop.setInputRegion(root, Qt.rect(island.x, 0, island.width, Math.max(island.height, edge.height)))
    }
    onVisibleChanged: updateMask()
    onModeChanged: updateMask()

    function acceptDrop(d) {
        if (d.hasUrls && !d.source) {
            notch.addDrop(d.urls, d.html, notch.categoryFilter) // into the category being viewed
            d.acceptProposedAction()
        }
    }

    function select(index, id, mode) {
        const sel = selection.slice()
        if (mode === "range" && selectionAnchor >= 0 && index >= 0) {
            const ids = notch.visibleIds()
            for (let i = Math.min(selectionAnchor, index); i <= Math.max(selectionAnchor, index); ++i)
                if (sel.indexOf(ids[i]) < 0)
                    sel.push(ids[i])
        } else {
            const at = sel.indexOf(id)
            if (at >= 0)
                sel.splice(at, 1)
            else
                sel.push(id)
            selectionAnchor = index
        }
        selection = sel
    }

    function clearSelection() {
        selection = []
        selectionAnchor = -1
    }

    function openTools(ids) {
        toolsMenu.targets = ids
        toolsMenu.mimes = notch.mimeTypes(ids)
        toolsMenu.popup()
    }

    function runTool(kind, option) {
        notch.runTool(kind, toolsMenu.targets, option || "")
        clearSelection()
    }

    function categoryName(id) {
        for (const c of notch.categories)
            if (c.id === id)
                return c.name
        return ""
    }

    function saveFiles(ids) {
        saveDialog.targetIds = ids
        saveDialog.open()
    }

    function moveTo(ids, categoryId) {
        notch.setCategory(ids, categoryId)
        clearSelection()
    }

    function beginCategoryEdit(target, name, ids) {
        page = "shelf"
        editingName = name
        pendingAssign = ids
        editingCategory = target
    }

    function commitCategory(text) {
        if (editingCategory === "new") {
            const id = notch.createCategory(text)
            if (id !== "" && pendingAssign.length > 0)
                moveTo(pendingAssign, id)
        } else if (editingCategory !== "") {
            notch.renameCategory(editingCategory, text)
        }
        editingCategory = ""
        pendingAssign = []
    }

    Connections {
        target: notch
        function onCountChanged() {
            const ids = notch.visibleIds()
            root.selection = root.selection.filter(id => ids.indexOf(id) >= 0)
            if (root.inRemoved && notch.removedCount === 0) // Recently removed just emptied
                notch.categoryFilter = ""
        }
        function onCategoryFilterChanged() { root.clearSelection() }
        function onFilterChanged() { root.clearSelection() }
    }

    // Called by DesktopIntegration for tray actions.
    function toggleFromTray(target) {
        if (target === "settings") {
            page = "settings"
            pinned = true
        } else {
            pinned = !pinned
        }
    }

    // Text files open straight in the note editor; everything else in the preview.
    function isEditable(item) {
        return item.isText && item.mimeType !== "application/x-desktop" && item.sizeBytes <= 1048576
    }

    function newNote() {
        if (noteId !== "") { // leaving an edit: start a fresh draft
            noteId = ""
            noteName = ""
            noteText = ""
        }
        page = "note"
    }

    function editNote(item) {
        noteId = item.id
        noteName = item.name
        noteText = notch.readText(item.id)
        page = "note"
    }

    function noteFileName() {
        const base = noteName.trim() || "Note " + Qt.formatDateTime(new Date(), "yyyy-MM-dd HH.mm")
        return /\.[A-Za-z0-9]{1,8}$/.test(base) ? base : base + "." + noteFormat
    }

    // Note toolkit: transforms the selection, or the whole note when nothing is selected.
    function applyTransform(kind) {
        const start = noteEditor.selectionStart, end = noteEditor.selectionEnd
        const partial = end > start
        const r = notch.transformText(kind, partial ? noteEditor.text.substring(start, end) : noteEditor.text)
        if (!r.ok) {
            infoText = "⚠ " + r.error
            infoTimer.restart()
            return
        }
        if (partial) {
            noteEditor.remove(start, end)
            noteEditor.insert(start, r.text)
            noteEditor.select(start, start + r.text.length)
        } else {
            noteEditor.text = r.text
        }
        noteEditor.forceActiveFocus()
    }

    function saveNote() {
        if (!notch.saveNote(noteId, noteFileName(), noteText))
            return
        noteId = ""
        noteName = ""
        noteText = ""
        page = "shelf"
    }

    onPageChanged: {
        if (page !== "note")
            return
        noteNameField.text = noteName
        noteEditor.text = noteText
        noteEditor.forceActiveFocus()
    }

    function showPreview(item) {
        if (isEditable(item)) {
            editNote(item)
            return
        }
        preview = item
        previewText = item.isText ? notch.previewText(item.id) : ""
        page = "preview"
    }

    component PillButton: AbstractButton {
        id: pb
        implicitWidth: pbLabel.implicitWidth + 22
        implicitHeight: 26
        opacity: enabled ? 1 : 0.4
        background: Rectangle {
            radius: height / 2
            color: pb.checked ? theme.accent : pb.down ? theme.surfacePressed : pb.hovered ? theme.surfaceHover : theme.surface
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        contentItem: Text {
            id: pbLabel
            text: pb.text
            color: pb.checked ? "white" : theme.text // on the accent fill, always white
            font.pixelSize: 12
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component IconButton: AbstractButton {
        id: ib
        implicitWidth: 26
        implicitHeight: 26
        background: Rectangle {
            radius: 13
            color: ib.down ? theme.surfacePressed : ib.hovered ? theme.surfaceHover : theme.surface
        }
        contentItem: Text {
            text: ib.text
            color: theme.text
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component Toggle: AbstractButton {
        id: tg
        implicitWidth: 42
        implicitHeight: 24
        padding: 0
        background: Rectangle {
            radius: 12
            color: tg.checked ? "#30d158" : theme.surfacePressed
            Behavior on color { ColorAnimation { duration: 150 } }
        }
        contentItem: Item {
            Rectangle {
                y: 2
                x: tg.checked ? parent.width - width - 2 : 2
                width: 20
                height: 20
                radius: 10
                color: "white"
                Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
            }
        }
    }

    component PulseDot: Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: 4
        color: theme.accent
        SequentialAnimation on opacity {
            running: visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 550; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1; duration: 550; easing.type: Easing.InOutSine }
        }
    }

    component SectionLabel: Text {
        color: theme.text
        font.pixelSize: 13
        font.weight: Font.Medium
    }

    component DarkMenu: Menu {
        padding: 6
        background: Rectangle {
            implicitWidth: 220
            radius: 12
            color: theme.surface
            border.color: theme.surfaceHover
        }
        delegate: MenuItem {
            id: mi
            implicitHeight: 30
            contentItem: Text {
                text: mi.text
                color: mi.enabled ? theme.text : theme.textDim
                font.pixelSize: 12
                elide: Text.ElideMiddle
                verticalAlignment: Text.AlignVCenter
                leftPadding: 4
            }
            background: Rectangle {
                radius: 8
                color: mi.highlighted && mi.enabled ? theme.surfaceHover : "transparent"
            }
        }
    }

    component DarkSeparator: MenuSeparator {
        contentItem: Rectangle { implicitHeight: 1; color: theme.surfaceHover }
    }

    // Category filter chip. Drop tiles on it to move them in, or files to add them into it.
    component Chip: AbstractButton {
        id: chip
        property string catId
        property int itemCount
        property string special // "screenshots" | "downloads" | ""
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
                    text: (chip.special === "screenshots" ? "📷 " : chip.special === "downloads" ? "⬇ "
                         : chip.special === "kdeconnect" ? "📱 " : chip.special === "removed" ? "🗑 " : "") + chip.text
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
            enabled: chip.special !== "removed" && chip.itemCount > 0
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
                drag.accepted = drag.source ? drag.source.draggedIds !== undefined && drag.source !== chip : drag.hasUrls
            }
            onDropped: (d) => {
                if (d.source === chip)
                    return
                if (d.source && d.source.draggedIds !== undefined) {
                    if (chip.catId === "__removed__")
                        notch.removeItems(d.source.draggedIds)
                    else
                        notch.setCategory(d.source.draggedIds, chip.catId)
                    d.accept(Qt.CopyAction)
                } else if (d.hasUrls) {
                    notch.addDrop(d.urls, d.html, chip.catId)
                    d.acceptProposedAction()
                }
            }
        }
    }

    // One settings row: title + optional description on the left, switch pinned to the right edge.
    component SettingToggle: RowLayout {
        id: st
        property string title
        property string caption
        property bool checked
        signal toggled()
        Layout.fillWidth: true
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            SectionLabel {
                Layout.fillWidth: true
                text: st.title
            }
            Caption {
                Layout.fillWidth: true
                visible: st.caption !== ""
                text: st.caption
            }
        }
        Toggle {
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            checked: st.checked
            onClicked: st.toggled()
            Accessible.name: st.title
        }
    }

    component Caption: Text {
        color: theme.textDim
        font.pixelSize: 11
        wrapMode: Text.Wrap
    }

    Item {
        id: island
        x: (root.width - width) / 2
        width: root.mode === "open" ? root.openWidth : root.mode === "toast" ? 380 : root.mode === "peek" ? 230 : 150
        height: root.mode === "open" ? panel.implicitHeight + (root.dragOver ? 8 : 0)
              : root.mode === "toast" ? 46 : root.mode === "peek" ? 36 : 0
        focus: true

        readonly property real shoulder: Math.min(10, Math.max(0, height) / 3)
        readonly property real corner: Math.min(24, Math.max(0, height - shoulder))
        readonly property bool closing: root.mode === "hidden"

        // Height is stiffer than width: the drop falls first, then spreads and settles.
        Behavior on height { SpringAnimation { spring: 4.2; damping: island.closing ? 0.9 : 0.32; epsilon: 0.25 } }
        Behavior on width { SpringAnimation { spring: island.closing ? 5 : 2.6; damping: island.closing ? 0.9 : 0.3; epsilon: 0.25 } }
        onWidthChanged: root.updateMask()
        onHeightChanged: root.updateMask()

        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_A && (e.modifiers & Qt.ControlModifier) && root.page === "shelf") {
                root.selection = notch.visibleIds()
                e.accepted = true
            } else if (e.key === Qt.Key_V && (e.modifiers & Qt.ControlModifier)) {
                notch.pasteClipboard(notch.categoryFilter)
                e.accepted = true
            } else if (e.key === Qt.Key_C && (e.modifiers & Qt.ControlModifier) && root.selecting) {
                notch.copyFiles(root.selection)
                e.accepted = true
            } else if (e.key === Qt.Key_Z && (e.modifiers & Qt.ControlModifier) && notch.undoCount > 0) {
                notch.undoRemove()
                e.accepted = true
            } else if (e.key === Qt.Key_F2 && root.selection.length === 1) {
                list.renamingId = root.selection[0]
                e.accepted = true
            } else if (e.key === Qt.Key_Delete && root.selecting && !root.inRemoved) {
                notch.removeItems(root.selection)
                root.clearSelection()
                e.accepted = true
            } else if (e.key === Qt.Key_Space || e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                root.pinned = !root.pinned
                e.accepted = true
            } else if (e.key === Qt.Key_Escape) {
                if (root.selecting) {
                    root.clearSelection()
                } else if (root.page !== "shelf") {
                    root.page = "shelf"
                } else {
                    root.pinned = false
                    root.open = false
                }
                e.accepted = true
            }
        }

        // Declared first so it sits underneath: category chips get first pick of a drop.
        DropArea {
            id: islandDrop
            anchors.fill: parent
            enabled: root.mode !== "hidden"
            onEntered: (drag) => { drag.accepted = drag.hasUrls && !drag.source }
            onDropped: (d) => root.acceptDrop(d)
        }

        // Liquid silhouette: flat top fused to the screen edge, concave shoulders, round bottom.
        Shape {
            anchors.fill: parent
            visible: island.height > 0.5
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: sp
                readonly property real s: island.shoulder
                readonly property real r: island.corner
                readonly property real w: island.width
                readonly property real h: Math.max(0, island.height)
                fillColor: theme.island
                strokeColor: root.dragOver ? root.accent : root.mode === "toast" ? root.toastColor : theme.outline
                strokeWidth: root.mode === "toast" ? 1.5 : 1
                startX: 0
                startY: 0
                PathQuad { x: sp.s; y: sp.s; controlX: sp.s; controlY: 0 }
                PathLine { x: sp.s; y: sp.h - sp.r }
                PathQuad { x: sp.s + sp.r; y: sp.h; controlX: sp.s; controlY: sp.h }
                PathLine { x: sp.w - sp.s - sp.r; y: sp.h }
                PathQuad { x: sp.w - sp.s; y: sp.h - sp.r; controlX: sp.w - sp.s; controlY: sp.h }
                PathLine { x: sp.w - sp.s; y: sp.s }
                PathQuad { x: sp.w; y: 0; controlX: sp.w - sp.s; controlY: 0 }
                PathLine { x: 0; y: 0 }
            }
        }

        // Toast: system alert. Battery fills up to the level; charging glows green.
        RowLayout {
            id: toastRow
            anchors.fill: parent
            anchors.leftMargin: island.shoulder + 18
            anchors.rightMargin: island.shoulder + 18
            spacing: 10
            opacity: root.mode === "toast" && island.height > 30 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            readonly property var t: root.toast || ({ kind: "", title: "", detail: "", icon: "", art: "", level: -1 })
            // Laptop power uses the drawn battery; Bluetooth devices their own icon.
            readonly property bool isPower: t.kind === "charging" || t.kind === "battery"
                                            || (t.kind === "lowbattery" && t.icon === "battery-low")

            // Battery glyph
            Item {
                visible: toastRow.isPower
                implicitWidth: 30
                implicitHeight: 14
                Rectangle {
                    id: shell
                    width: 27
                    height: 14
                    radius: 4
                    color: "transparent"
                    border.width: 1.5
                    border.color: theme.textDim
                    Rectangle {
                        x: 2
                        y: 2
                        height: parent.height - 4
                        radius: 2
                        color: toastRow.t.kind === "charging" ? "#30d158" : toastRow.t.level <= 20 ? "#ff453a" : theme.text
                        // Starts empty each time the toast appears and fills to the level.
                        width: toastRow.visible ? Math.max(2, (shell.width - 4) * Math.max(0, toastRow.t.level) / 100) : 0
                        Behavior on width { NumberAnimation { duration: 700; easing.type: Easing.OutCubic } }
                    }
                }
                Rectangle { x: 28; y: 4; width: 2; height: 6; radius: 1; color: theme.textDim }
                Text {
                    anchors.centerIn: shell
                    visible: toastRow.t.kind === "charging"
                    text: "⚡"
                    color: "white"
                    font.pixelSize: 11
                    style: Text.Outline
                    styleColor: "black"
                    SequentialAnimation on scale {
                        running: toastRow.visible && toastRow.t.kind === "charging"
                        loops: Animation.Infinite
                        NumberAnimation { to: 1.25; duration: 450; easing.type: Easing.OutQuad }
                        NumberAnimation { to: 1; duration: 450; easing.type: Easing.InQuad }
                    }
                }
            }
            Image {
                // Device icons only (Bluetooth): media/file/color toasts bring their own visual.
                visible: !toastRow.isPower && ["media", "file", "color"].indexOf(toastRow.t.kind) < 0
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                source: toastRow.t.icon ? "image://mime/" + toastRow.t.icon : ""
                sourceSize: Qt.size(44, 44)
                // Device icons are monochrome theme icons drawn for light backgrounds: render them white.
                layer.enabled: true
                layer.effect: MultiEffect { brightness: theme.dark ? 1.0 : -1.0 } // white on dark, black on light
            }
            Rectangle {
                visible: toastRow.t.kind === "color"
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                radius: 12
                color: toastRow.t.kind === "color" ? toastRow.t.detail : "transparent"
                border.width: 1.5
                border.color: theme.outline
            }
            Rectangle {
                visible: toastRow.t.kind === "media" || toastRow.t.kind === "file"
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: 7
                color: theme.surface
                clip: true
                Image {
                    anchors.fill: parent
                    source: toastRow.t.art || (toastRow.t.icon ? "image://mime/" + toastRow.t.icon : "image://mime/audio-x-generic")
                    cache: false
                    sourceSize: Qt.size(56, 56)
                    fillMode: Image.PreserveAspectCrop
                }
            }
            Text {
                Layout.fillWidth: true
                text: toastRow.t.title
                color: root.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                Layout.maximumWidth: toastRow.t.kind === "media" || toastRow.t.kind === "file" ? 130 : -1
                elide: Text.ElideRight
                text: toastRow.t.kind === "bluetooth" && toastRow.t.level >= 0
                      ? toastRow.t.detail + " · " + toastRow.t.level + "%" : toastRow.t.detail
                color: toastRow.t.kind === "charging" ? "#30d158"
                     : toastRow.t.kind === "lowbattery" ? "#ff453a"
                     : toastRow.t.kind === "media" || toastRow.t.kind === "file" ? root.textSecondary
                     : toastRow.t.detail === "Disconnected" ? root.textSecondary
                     : toastRow.t.kind === "bluetooth" ? root.accent : root.textPrimary
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Equalizer {
                visible: toastRow.t.kind === "media"
                Layout.alignment: Qt.AlignVCenter
                maxHeight: 16
            }
        }

        // Peek: compact copy-progress pill.
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: island.shoulder + 16
            anchors.rightMargin: island.shoulder + 16
            spacing: 8
            opacity: root.mode === "peek" && island.height > 24 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            PulseDot {}
            Text {
                Layout.fillWidth: true
                text: "Copying " + notch.copying + (notch.copying === 1 ? " file" : " files")
                color: root.textPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text { text: notch.count; color: root.textSecondary; font.pixelSize: 12 }
        }

        // Open: full panel. Fixed width, centered and clipped so it doesn't reflow mid-spring.
        Item {
            x: island.shoulder
            width: Math.max(0, island.width - 2 * island.shoulder)
            height: Math.max(0, island.height)
            clip: true
            opacity: root.mode === "open" ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: root.mode === "open" ? 280 : 90; easing.type: Easing.OutCubic } }

            ColumnLayout {
                id: panel
                x: (parent.width - width) / 2
                width: root.openWidth - 20
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.leftMargin: root.page === "shelf" ? 12 : 6
                    Layout.rightMargin: 8
                    spacing: 8

                    IconButton {
                        visible: root.page !== "shelf"
                        text: "‹"
                        Accessible.name: "Back"
                        onClicked: root.page = "shelf"
                    }
                    Text {
                        Layout.maximumWidth: 300
                        text: root.selecting ? root.selection.length + " selected"
                            : root.page === "settings" ? "Settings" : root.page === "preview" ? root.preview.name
                            : root.page === "note" ? (root.noteId !== "" ? "Edit note" : "New note") : "Notch"
                        color: root.textPrimary
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    Rectangle {
                        visible: root.page === "shelf" && !root.selecting
                        implicitWidth: Math.max(20, countLabel.implicitWidth + 12)
                        implicitHeight: 20
                        radius: 10
                        color: theme.surface
                        Text {
                            id: countLabel
                            anchors.centerIn: parent
                            text: notch.count
                            color: root.textSecondary
                            font.pixelSize: 11
                            font.weight: Font.Medium
                        }
                    }
                    Item { Layout.fillWidth: true }
                    PulseDot { visible: root.page === "shelf" && notch.copying > 0 }
                    TextField {
                        id: search
                        visible: root.page === "shelf" && notch.count > 0 && !root.selecting
                        Layout.preferredWidth: activeFocus || text !== "" ? 180 : 110
                        Behavior on Layout.preferredWidth { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        implicitHeight: 26
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 0
                        bottomPadding: 0
                        verticalAlignment: TextInput.AlignVCenter
                        placeholderText: "Search"
                        placeholderTextColor: theme.placeholder
                        color: root.textPrimary
                        selectionColor: root.accent
                        font.pixelSize: 12
                        background: Rectangle {
                            radius: 13
                            color: theme.surface
                            border.width: 1
                            border.color: search.activeFocus ? root.accent : "transparent"
                        }
                        onTextChanged: notch.filter = text
                        Keys.onEscapePressed: {
                            if (text !== "")
                                text = ""
                            else
                                island.forceActiveFocus()
                        }
                    }
                    PillButton {
                        visible: root.page === "preview"
                        text: "Open"
                        onClicked: notch.open(root.preview.id)
                    }
                    PillButton {
                        visible: root.selecting && root.inRemoved
                        text: "Restore"
                        onClicked: {
                            notch.restoreItems(root.selection)
                            root.clearSelection()
                        }
                    }
                    PillButton {
                        visible: root.selecting && root.inRemoved
                        text: "Delete now"
                        onClicked: {
                            notch.deleteForever(root.selection)
                            root.clearSelection()
                        }
                    }
                    PillButton {
                        visible: root.selecting && !root.inRemoved
                        text: "Move to…"
                        onClicked: {
                            assignMenu.targetIds = root.selection
                            assignMenu.popup()
                        }
                    }
                    PillButton {
                        visible: root.selecting && !root.inRemoved
                        text: "Tools"
                        onClicked: root.openTools(root.selection)
                    }
                    PillButton {
                        visible: root.selecting && !root.inRemoved
                        text: "Save…"
                        onClicked: root.saveFiles(root.selection)
                    }
                    PillButton {
                        visible: root.selecting && !root.inRemoved
                        text: "Remove"
                        onClicked: {
                            notch.removeItems(root.selection)
                            root.clearSelection()
                        }
                    }
                    IconButton {
                        visible: root.selecting
                        text: "✕"
                        Accessible.name: "Clear selection"
                        onClicked: root.clearSelection()
                    }
                    AbstractButton {
                        id: eqButton
                        readonly property bool shown: root.playerExpanded || root.playerFlash
                        visible: root.page === "shelf" && !root.selecting && media.active
                        implicitWidth: 38
                        implicitHeight: 26
                        Accessible.name: shown ? "Hide player" : "Show player"
                        onClicked: {
                            root.playerExpanded = !shown
                            root.playerFlash = false
                        }
                        background: Rectangle {
                            radius: 13
                            color: eqButton.shown ? theme.accentSoft : eqButton.down ? theme.surfacePressed : eqButton.hovered ? theme.surfaceHover : theme.surface
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                        contentItem: Item {
                            Equalizer {
                                anchors.centerIn: parent
                                barWidth: 2.5
                                maxHeight: 14
                            }
                        }
                    }
                    PillButton {
                        visible: root.page === "note"
                        text: "Save"
                        checked: true
                        Accessible.name: "Save note"
                        onClicked: root.saveNote()
                    }
                    IconButton {
                        visible: root.page === "shelf" && !root.selecting
                        text: "🎨"
                        Accessible.name: "Pick a color from the screen"
                        onClicked: {
                            // Get out of the way so anything on screen (even under Notch) can be picked.
                            root.pinned = false
                            root.open = false
                            colorPicker.pick()
                        }
                    }
                    IconButton {
                        visible: root.page === "shelf" && !root.selecting
                        text: "✎"
                        Accessible.name: "New note"
                        onClicked: root.newNote()
                    }
                    IconButton {
                        visible: root.page === "shelf" && !root.selecting
                        text: "⚙"
                        Accessible.name: "Settings"
                        onClicked: root.page = "settings"
                    }
                    PillButton {
                        visible: !root.selecting // room for the selection actions
                        text: root.pinned ? "Pinned" : "Pin"
                        checked: root.pinned
                        onClicked: root.pinned = !root.pinned
                        Accessible.name: "Keep Notch open"
                    }
                }

                MediaCard {
                    visible: root.page === "shelf" && media.active && (root.playerExpanded || root.playerFlash)
                    onInteracted: {
                        root.playerExpanded = true
                        root.playerFlash = false
                    }
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                }

                Rectangle {
                    visible: notch.errorMessage !== ""
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    implicitHeight: errorText.implicitHeight + 16
                    radius: 12
                    color: theme.dangerBg
                    Text {
                        id: errorText
                        anchors.fill: parent
                        anchors.margins: 8
                        anchors.rightMargin: 28
                        text: notch.errorMessage
                        color: "#ff6961"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 10
                        text: "✕"
                        color: "#ff6961"
                        font.pixelSize: 11
                    }
                    TapHandler { onTapped: notch.clearError() }
                }

                // ---- Categories ----
                RowLayout {
                    visible: root.page === "shelf" && (notch.count > 0 || notch.categories.length > 0)
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    spacing: 6

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        clip: true

                        ListView {
                            id: chips
                            orientation: ListView.Horizontal
                            spacing: 6
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: [{ id: "", name: "All", count: notch.count }].concat(notch.categories).concat(
                                       notch.removedCount > 0
                                       ? [{ id: "__removed__", name: "Recently removed", count: notch.removedCount, special: "removed" }]
                                       : [])
                            delegate: Chip {
                                required property var modelData
                                catId: modelData.id
                                text: modelData.name
                                itemCount: modelData.count
                                special: modelData.special || ""
                                onDropHoverChanged: root.chipDragOver = dropHover
                                onMenuRequested: {
                                    if (catId === "__removed__")
                                        return removedChipMenu.popup()
                                    categoryMenu.catId = catId
                                    categoryMenu.catName = text
                                    categoryMenu.popup()
                                }
                            }
                            anchors.fill: parent
                            // Wheel / touchpad scroll the chips sideways (they would otherwise only drag).
                            function scrollBy(dx) {
                                const max = Math.max(originX, originX + contentWidth - width)
                                contentX = Math.max(originX, Math.min(max, contentX + dx))
                            }
                            WheelHandler {
                                target: null
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                onWheel: (event) => {
                                    const p = event.pixelDelta, a = event.angleDelta
                                    const d = (p.x !== 0 || p.y !== 0) ? (Math.abs(p.x) > Math.abs(p.y) ? p.x : p.y)
                                                                       : (Math.abs(a.x) > Math.abs(a.y) ? a.x : a.y)
                                    chips.scrollBy(-d)
                                }
                            }
                            // Keep the chosen category in view.
                            Connections {
                                target: notch
                                function onCategoryFilterChanged() {
                                    const all = [""].concat(notch.categories.map(c => c.id))
                                    chips.positionViewAtIndex(Math.max(0, all.indexOf(notch.categoryFilter)), ListView.Contain)
                                }
                            }
                        }

                        // Fades hint at chips hidden off either edge.
                        Rectangle {
                            visible: chips.contentX > chips.originX + 1
                            anchors.left: parent.left
                            width: 28
                            height: parent.height
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: theme.island }
                                GradientStop { position: 1; color: "transparent" }
                            }
                        }
                        Rectangle {
                            visible: chips.contentX + chips.width < chips.originX + chips.contentWidth - 1
                            anchors.right: parent.right
                            width: 28
                            height: parent.height
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "transparent" }
                                GradientStop { position: 1; color: theme.island }
                            }
                        }
                    }

                    TextField {
                        id: categoryField
                        visible: root.editingCategory !== ""
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 26
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 0
                        bottomPadding: 0
                        verticalAlignment: TextInput.AlignVCenter
                        placeholderText: "Category name"
                        placeholderTextColor: theme.placeholder
                        color: root.textPrimary
                        selectionColor: root.accent
                        font.pixelSize: 12
                        maximumLength: 60
                        background: Rectangle { radius: 13; color: theme.surface; border.width: 1; border.color: root.accent }
                        onVisibleChanged: {
                            if (!visible)
                                return
                            text = root.editingName
                            selectAll()
                            forceActiveFocus()
                        }
                        onAccepted: root.commitCategory(text)
                        onActiveFocusChanged: if (!activeFocus && visible) root.editingCategory = ""
                        Keys.onEscapePressed: root.editingCategory = ""
                    }
                    IconButton {
                        visible: root.editingCategory === ""
                        text: "+"
                        Accessible.name: "New category"
                        onClicked: root.beginCategoryEdit("new", "", [])
                    }

                    PillButton {
                        visible: notch.count > 1
                        text: "↕ " + ({ date: "Date", name: "Name", size: "Size", type: "Type" })[notch.sortMode]
                        Accessible.name: "Sort files"
                        onClicked: sortMenu.popup()
                    }
                }

                // ---- Picked colors ----
                Flickable {
                    visible: root.page === "shelf" && !root.inRemoved && notch.recentColors.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.preferredHeight: 24
                    contentWidth: swatches.implicitWidth
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: swatches
                        spacing: 8
                        Repeater {
                            model: notch.recentColors
                            Rectangle {
                                id: swatch
                                required property string modelData
                                width: 22
                                height: 22
                                radius: 11
                                color: modelData
                                border.width: swatchHover.hovered ? 2 : 1
                                border.color: swatchHover.hovered ? theme.text : theme.outline
                                HoverHandler { id: swatchHover }
                                ToolTip.visible: swatchHover.hovered
                                ToolTip.delay: 300
                                ToolTip.text: swatch.modelData + " · click to copy · right-click to remove"
                                TapHandler {
                                    onTapped: {
                                        notch.copyText(swatch.modelData)
                                        root.infoText = "Copied " + swatch.modelData
                                        infoTimer.restart()
                                    }
                                }
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: notch.removeColor(swatch.modelData)
                                }
                            }
                        }
                    }
                }

                // ---- Shelf ----
                Item {
                    id: emptyState
                    visible: root.page === "shelf" && notch.count === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.preferredHeight: 108
                    Shape {
                        anchors.fill: parent
                        layer.enabled: true
                        layer.samples: 4
                        ShapePath {
                            strokeColor: root.dragOver ? root.accent : theme.surfacePressed
                            strokeWidth: 1.5
                            strokeStyle: ShapePath.DashLine
                            dashPattern: [4, 4]
                            fillColor: root.dragOver ? theme.accentFaint : "transparent"
                            PathRectangle {
                                x: 1
                                y: 1
                                width: emptyState.width - 2
                                height: emptyState.height - 2
                                radius: 18
                            }
                        }
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 4
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.dragOver ? "Release to add" : "Drop files here"
                            color: root.dragOver ? root.accent : root.textPrimary
                            font.pixelSize: 14
                            font.weight: Font.Medium
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "or click here and press Ctrl+V · kept for " + notch.retentionDays
                                  + (notch.retentionDays === 1 ? " day" : " days")
                            color: root.textSecondary
                            font.pixelSize: 11
                        }
                    }
                }

                Text {
                    visible: root.page === "shelf" && notch.count > 0 && list.count === 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 144
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: notch.filter !== "" ? "No files match “" + notch.filter + "”"
                        : root.inRemoved ? "Nothing here. Removed files stay for 24 hours."
                        : "No files in this category yet. Drop some here."
                    color: root.textSecondary
                    font.pixelSize: 13
                }

                ListView {
                    id: list
                    visible: root.page === "shelf" && count > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    Layout.preferredHeight: 144
                    orientation: ListView.Horizontal
                    spacing: 4
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: notch.items
                    property var selectedIds: root.selection
                    property string renamingId: ""
                    function commitRename(id, name) {
                        renamingId = ""
                        notch.renameItem(id, name)
                        island.forceActiveFocus()
                    }
                    function dragIds(id) { return root.selection.indexOf(id) >= 0 ? root.selection : [id] }
                    // Flick-dragging would fight the tiles' drag-out, so scrolling is wheel/touchpad + scrollbar.
                    interactive: false
                    function scrollBy(dx) {
                        const max = Math.max(originX, originX + contentWidth - width)
                        contentX = Math.max(originX, Math.min(max, contentX + dx))
                    }
                    WheelHandler {
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: (event) => {
                            // Touchpads report pixels, wheels report angle (120 per notch); either axis scrolls sideways.
                            const p = event.pixelDelta, a = event.angleDelta
                            const d = (p.x !== 0 || p.y !== 0) ? (Math.abs(p.x) > Math.abs(p.y) ? p.x : p.y)
                                                               : (Math.abs(a.x) > Math.abs(a.y) ? a.x : a.y)
                            list.scrollBy(-d)
                        }
                    }
                    ScrollBar.horizontal: ScrollBar {
                        id: tileBar
                        policy: list.contentWidth > list.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        padding: 2
                        contentItem: Rectangle {
                            implicitHeight: tileBar.hovered || tileBar.pressed ? 8 : 5
                            radius: height / 2
                            color: tileBar.pressed ? theme.textDim : tileBar.hovered ? theme.textFaint : theme.textFaint
                            Behavior on implicitHeight { NumberAnimation { duration: 120 } }
                        }
                        background: null
                    }
                    delegate: FileItemDelegate {
                        onPreviewRequested: (item) => root.showPreview(item)
                        onSelectRequested: (index, id, mode) => root.select(index, id, mode)
                        onContextMenuRequested: (info) => {
                            menu.itemId = info.id
                            menu.itemName = info.name
                            menu.sourcePath = info.source
                            menu.itemPinned = info.pinned
                            menu.itemIsText = info.isText
                            if (root.inRemoved)
                                removedMenu.popup()
                            else
                                menu.popup()
                        }
                    }
                    add: Transition {
                        NumberAnimation { property: "scale"; from: 0.4; to: 1; duration: 420; easing.type: Easing.OutBack; easing.overshoot: 2 }
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
                    }
                    remove: Transition {
                        NumberAnimation { property: "scale"; to: 0.4; duration: 180; easing.type: Easing.InCubic }
                        NumberAnimation { property: "opacity"; to: 0; duration: 180 }
                    }
                    displaced: Transition {
                        NumberAnimation { properties: "x"; duration: 280; easing.type: Easing.OutCubic }
                    }
                }

                // ---- Preview ----
                Rectangle {
                    id: previewFrame
                    readonly property bool hasImage: root.preview.isImage === true
                    readonly property bool hasText: !hasImage && root.previewText !== ""
                    visible: root.page === "preview"
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.preferredHeight: hasImage || hasText ? 320 : 150
                    radius: 16
                    color: theme.well
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 8
                        visible: previewFrame.hasImage
                        source: visible ? root.preview.url : ""
                        sourceSize.width: 1200
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }

                    ScrollView {
                        id: textScroll
                        anchors.fill: parent
                        anchors.margins: 12
                        visible: previewFrame.hasText
                        contentWidth: availableWidth
                        TextEdit {
                            width: textScroll.availableWidth
                            readOnly: true
                            selectByMouse: true
                            textFormat: TextEdit.PlainText
                            text: root.previewText
                            color: theme.textSoft
                            selectionColor: root.accent
                            font.family: "monospace"
                            font.pixelSize: 11
                            wrapMode: TextEdit.WrapAnywhere
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        visible: !previewFrame.hasImage && !previewFrame.hasText
                        spacing: 10
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 64
                            height: 64
                            source: root.page === "preview" ? "image://mime/" + root.preview.iconName : ""
                            sourceSize: Qt.size(128, 128)
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "No preview for this file type"
                            color: root.textSecondary
                            font.pixelSize: 12
                        }
                    }
                }

                Text {
                    visible: root.page === "preview"
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    text: (root.preview.sizeText || "") + " · added " + (root.preview.addedText || "") + " · "
                          + (root.preview.expiresText || "")
                          + (root.preview.sourcePath ? "\nFrom " + root.preview.sourcePath : "")
                    color: root.textSecondary
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    maximumLineCount: 2
                }

                // ---- Note ----
                ColumnLayout {
                    visible: root.page === "note"
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        TextField {
                            id: noteNameField
                            Layout.fillWidth: true
                            implicitHeight: 28
                            leftPadding: 12
                            rightPadding: 12
                            topPadding: 0
                            bottomPadding: 0
                            verticalAlignment: TextInput.AlignVCenter
                            placeholderText: "Name (optional)"
                            placeholderTextColor: theme.placeholder
                            color: root.textPrimary
                            selectionColor: root.accent
                            font.pixelSize: 12
                            background: Rectangle {
                                radius: 14
                                color: theme.surface
                                border.width: 1
                                border.color: noteNameField.activeFocus ? root.accent : "transparent"
                            }
                            onTextChanged: root.noteName = text
                            Keys.onPressed: (e) => {
                                if (e.key === Qt.Key_S && (e.modifiers & Qt.ControlModifier)) {
                                    root.saveNote()
                                    e.accepted = true
                                }
                            }
                            Keys.onEscapePressed: root.page = "shelf"
                        }
                        PillButton {
                            visible: root.noteId === ""
                            text: ".txt"
                            checked: root.noteFormat === "txt"
                            onClicked: root.noteFormat = "txt"
                        }
                        PillButton {
                            visible: root.noteId === ""
                            text: ".md"
                            checked: root.noteFormat === "md"
                            onClicked: root.noteFormat = "md"
                        }
                    }

                    Row {
                        Layout.fillWidth: true
                        spacing: 5
                        Repeater {
                            model: [["json", "{ } JSON"], ["base64-encode", "Base64 ↑"], ["base64-decode", "Base64 ↓"],
                                    ["url-encode", "URL ↑"], ["url-decode", "URL ↓"], ["upper", "ABC"], ["lower", "abc"]]
                            PillButton {
                                required property var modelData
                                implicitHeight: 24
                                text: modelData[1]
                                Accessible.name: modelData[0]
                                onClicked: root.applyTransform(modelData[0])
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 260
                        radius: 14
                        color: theme.well
                        border.width: 1
                        border.color: noteEditor.activeFocus ? theme.accentSoft : theme.surface
                        ScrollView {
                            id: noteScroll
                            anchors.fill: parent
                            anchors.margins: 10
                            contentWidth: availableWidth
                            TextArea {
                                id: noteEditor
                                width: noteScroll.availableWidth
                                wrapMode: TextArea.Wrap
                                color: theme.textSoft
                                selectionColor: root.accent
                                font.family: "monospace"
                                font.pixelSize: 12
                                placeholderText: "Write a note or paste a snippet…"
                                placeholderTextColor: theme.textFaint
                                background: null
                                padding: 0
                                onTextChanged: root.noteText = text
                                Keys.onPressed: (e) => {
                                    if (e.key === Qt.Key_S && (e.modifiers & Qt.ControlModifier)) {
                                        root.saveNote()
                                        e.accepted = true
                                    }
                                }
                                Keys.onEscapePressed: root.page = "shelf"
                            }
                        }
                    }

                    Caption {
                        Layout.fillWidth: true
                        text: (root.noteId !== "" ? "Saving updates this file in Notch" : "Saved as " + root.noteFileName())
                              + " · Ctrl+S to save · Esc to go back (draft is kept)"
                    }
                }

                // ---- Settings ---- (scrolls: taller than the window can show)
                ScrollView {
                    id: settingsScroll
                    visible: root.page === "settings"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(settingsColumn.implicitHeight, 470)
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        id: settingsColumn
                        width: settingsScroll.availableWidth - 24
                        x: 12
                        spacing: 16

                        ColumnLayout {
                            spacing: 6
                            SectionLabel { text: "Keep files for" }
                            Row {
                                spacing: 6
                                Repeater {
                                    model: [1, 3, 7, 14, 30]
                                    PillButton {
                                        required property int modelData
                                        text: modelData + (modelData === 1 ? " day" : " days")
                                        checked: notch.retentionDays === modelData
                                        onClicked: notch.retentionDays = modelData
                                    }
                                }
                            }
                            Caption { text: "Files older than this are removed right away." }
                        }

                        ColumnLayout {
                            spacing: 6
                            SectionLabel { text: "Appearance" }
                            Row {
                                spacing: 6
                                Repeater {
                                    model: [["dark", "Dark"], ["light", "Light"], ["system", "Follow system"]]
                                    PillButton {
                                        required property var modelData
                                        text: modelData[1]
                                        checked: notch.themeMode === modelData[0]
                                        onClicked: notch.themeMode = modelData[0]
                                    }
                                }
                            }
                            Caption { text: "Highlights use your KDE accent color." }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            SectionLabel { text: "Sort new files by type" }
                            Caption {
                                Layout.fillWidth: true
                                text: "New files go to these categories unless you drop them into an open category. Create categories with + on the shelf."
                            }
                            Repeater {
                                model: [["pdf", "📄  PDF"], ["image", "🖼  Images"], ["video", "🎬  Video"], ["audio", "🎵  Audio"],
                                        ["document", "📝  Documents"], ["archive", "🗜  Archives"], ["text", "⌨  Code & text"]]
                                RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData[1]
                                        color: theme.text
                                        font.pixelSize: 12
                                    }
                                    PillButton {
                                        implicitHeight: 24
                                        text: (root.categoryName(notch.typeRules[modelData[0]]) || "No category") + "  ▾"
                                        checked: root.categoryName(notch.typeRules[modelData[0]]) !== ""
                                        onClicked: {
                                            ruleMenu.group = modelData[0]
                                            ruleMenu.popup()
                                        }
                                    }
                                }
                            }
                            PillButton {
                                Layout.alignment: Qt.AlignRight
                                text: "Apply to existing files"
                                Accessible.name: "Sort uncategorized files by these rules"
                                onClicked: notch.applyRulesToExisting()
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            SectionLabel { text: "Storage folder" }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: notch.storageDir
                                    color: root.textSecondary
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                                PillButton {
                                    text: "Change…"
                                    onClicked: {
                                        if (storageHint.visible)
                                            storageHint.warn = true
                                        else
                                            folderDialog.open()
                                    }
                                }
                            }
                            Caption {
                                id: storageHint
                                property bool warn: false
                                visible: notch.count > 0 || notch.copying > 0
                                onVisibleChanged: warn = false
                                color: warn ? "#ff6961" : theme.textDim
                                text: "Remove all files from the shelf first, then change the folder."
                            }
                        }

                        SettingToggle {
                            title: "Start at login"
                            checked: notch.autostart
                            onToggled: notch.autostart = !notch.autostart
                        }

                        SettingToggle {
                            title: "Charger & Bluetooth alerts"
                            caption: "Notch peeks out when you plug in, a device connects, or a battery runs low."
                            checked: notch.alerts
                            onToggled: notch.alerts = !notch.alerts
                        }

                        SettingToggle {
                            title: "Add new screenshots"
                            caption: "Spectacle screenshots land in the 📷 Screenshots category."
                            checked: notch.autoScreenshots
                            onToggled: notch.autoScreenshots = !notch.autoScreenshots
                        }

                        // Shown under its toggle while Spectacle isn't writing files.
                        Rectangle {
                            visible: notch.autoScreenshots && !notch.spectacleAutoSave
                            Layout.fillWidth: true
                            Layout.topMargin: -6
                            implicitHeight: warnRow.implicitHeight + 16
                            radius: 12
                            color: theme.warnBg
                            RowLayout {
                                id: warnRow
                                anchors.fill: parent
                                anchors.margins: 8
                                anchors.leftMargin: 12
                                spacing: 10
                                Caption {
                                    Layout.fillWidth: true
                                    color: "#ff9f0a"
                                    text: "Spectacle doesn't save screenshots automatically, so they never reach a folder Notch can see."
                                }
                                PillButton {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: "Turn on in Spectacle"
                                    Accessible.name: "Turn on automatic saving in Spectacle"
                                    onClicked: notch.enableSpectacleAutoSave()
                                }
                            }
                        }

                        SettingToggle {
                            title: "Add new downloads"
                            caption: "Finished files in Downloads land in the ⬇ Downloads category."
                            checked: notch.autoDownloads
                            onToggled: notch.autoDownloads = !notch.autoDownloads
                        }

                        SettingToggle {
                            title: "Add files from KDE Connect"
                            caption: "Files your phone or tablet sends land in the 📱 KDE Connect category."
                            checked: notch.autoKdeConnect
                            onToggled: notch.autoKdeConnect = !notch.autoKdeConnect
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Caption { Layout.fillWidth: true; text: "Notch " + Qt.application.version }
                            PillButton { text: "Quit Notch"; onClicked: Qt.quit() }
                        }
                    }
                }

                Rectangle {
                    visible: root.infoText !== ""
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    implicitHeight: 34
                    radius: 12
                    color: theme.infoBg
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        verticalAlignment: Text.AlignVCenter
                        readonly property bool warning: root.infoText.startsWith("⚠")
                        text: (warning ? "" : "✓  ") + root.infoText
                        color: warning ? "#ff9f0a" : "#5ac8fa"
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                }

                Rectangle {
                    visible: notch.undoCount > 0 && root.page === "shelf"
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    implicitHeight: 36
                    radius: 12
                    color: theme.surface
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 5
                        Text {
                            Layout.fillWidth: true
                            text: notch.undoCount + (notch.undoCount === 1 ? " file" : " files") + " moved to Recently removed"
                            color: root.textPrimary
                            font.pixelSize: 12
                        }
                        PillButton { text: "Undo"; checked: true; onClicked: notch.undoRemove() }
                    }
                }

                Item { Layout.preferredHeight: 4 }
            }
        }

        HoverHandler { id: islandHover }
    }

    // Faint handle hinting where Notch lives; red when there's an unread error. Visual only.
    Rectangle {
        x: (root.width - width) / 2
        y: 0
        width: 44
        height: 3
        radius: 1.5
        color: notch.errorMessage !== "" ? "#ff453a" : theme.dark ? "#ffffff" : "#000000"
        opacity: root.mode !== "hidden" ? 0 : edgeHover.hovered ? 0.7 : 0.18
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    // The only live area while hidden: a thin strip on the top edge.
    Item {
        id: edge
        x: (root.width - width) / 2
        width: 300
        height: 3
        HoverHandler {
            id: edgeHover
            onHoveredChanged: {
                if (hovered) {
                    dwellTimer.restart()
                } else {
                    dwellTimer.stop()
                    root.dwelled = false
                }
            }
        }
        DropArea {
            id: edgeDrop
            anchors.fill: parent
            onEntered: (drag) => { drag.accepted = drag.hasUrls && !drag.source }
            onDropped: (d) => root.acceptDrop(d)
        }
    }

    DarkMenu {
        id: menu
        property string itemId
        property string sourcePath
        property string itemName
        property bool itemPinned
        property bool itemIsText
        // Acts on the whole selection when the clicked tile is part of it.
        readonly property var targets: root.selection.indexOf(itemId) >= 0 ? root.selection : [itemId]
        onAboutToShow: sendMenu.devices = kdeconnect.devices()
        Action { text: "Open"; onTriggered: notch.open(menu.itemId) }
        Action {
            text: root.selection.indexOf(menu.itemId) >= 0 ? "Deselect" : "Select"
            onTriggered: root.select(-1, menu.itemId, "toggle")
        }
        Action { text: menu.targets.length > 1 ? "Copy " + menu.targets.length + " files" : "Copy"; onTriggered: notch.copyFiles(menu.targets) }
        Action {
            text: "Copy contents"
            enabled: menu.itemIsText && menu.targets.length === 1
            onTriggered: notch.copyContents(menu.itemId)
        }
        Action { text: menu.targets.length > 1 ? "Copy paths" : "Copy path"; onTriggered: notch.copyPaths(menu.targets) }
        DarkSeparator {}
        Action {
            text: "Edit"
            enabled: menu.itemIsText && menu.targets.length === 1
            onTriggered: root.editNote({ id: menu.itemId, name: menu.itemName })
        }
        Action {
            text: "Rename…"
            enabled: menu.targets.length === 1
            onTriggered: list.renamingId = menu.itemId
        }
        Action { text: "Save to…"; onTriggered: root.saveFiles(menu.targets) }
        DarkMenu {
            id: sendMenu
            property var devices: []
            title: "Send to device  ▸"
            Instantiator {
                model: sendMenu.devices
                delegate: Action {
                    required property var modelData
                    text: (modelData.type === "desktop" || modelData.type === "laptop" ? "💻  " : "📱  ") + modelData.name
                    onTriggered: kdeconnect.send(modelData.id, modelData.name, notch.fileUrls(menu.targets))
                }
                onObjectAdded: (index, object) => sendMenu.insertAction(index, object)
                onObjectRemoved: (index, object) => sendMenu.removeAction(object)
            }
            MenuItem {
                id: noDevice
                visible: sendMenu.devices.length === 0
                height: visible ? implicitHeight : 0
                implicitHeight: 30
                enabled: false
                contentItem: Text {
                    text: "No KDE Connect device nearby"
                    color: theme.textDim
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 4
                }
                background: null
            }
        }
        Action { text: "Tools…"; onTriggered: root.openTools(menu.targets) }
        Action {
            text: menu.itemPinned ? "Allow auto-delete" : "Keep (never auto-delete)"
            onTriggered: notch.setPinned(menu.targets, !menu.itemPinned)
        }
        Action {
            text: "Move to category…"
            onTriggered: {
                assignMenu.targetIds = menu.targets
                assignMenu.popup()
            }
        }
        Action {
            text: menu.targets.length > 1 ? "Remove " + menu.targets.length + " files" : "Remove from Notch"
            onTriggered: {
                notch.removeItems(menu.targets)
                root.clearSelection()
            }
        }
        DarkSeparator {}
        Action { enabled: false; text: "From: " + (menu.sourcePath || "unknown") }
    }

    // PDF & image tools. Items enable only for files they apply to; order = selection order.
    DarkMenu {
        id: toolsMenu
        property var targets: []
        property var mimes: []
        readonly property int n: mimes.length
        readonly property bool allPdf: n > 0 && mimes.every(m => m === "application/pdf")
        readonly property bool allImages: n > 0 && mimes.every(m => m.startsWith("image/"))
        Action { text: "Merge PDFs"; enabled: toolsMenu.allPdf && toolsMenu.n >= 2; onTriggered: root.runTool("merge") }
        Action { text: "Images → PDF"; enabled: toolsMenu.allImages; onTriggered: root.runTool("images-pdf") }
        Action { text: "Compress PDF"; enabled: toolsMenu.allPdf; onTriggered: root.runTool("compress") }
        Action { text: "Split into pages"; enabled: toolsMenu.allPdf && toolsMenu.n === 1; onTriggered: root.runTool("split") }
        Action {
            text: "Extract pages…"
            enabled: toolsMenu.allPdf && toolsMenu.n === 1
            onTriggered: {
                extractPopup.itemId = toolsMenu.targets[0]
                extractPopup.pages = notch.pageCount(extractPopup.itemId)
                extractPopup.open()
            }
        }
        DarkSeparator {}
        Action {
            text: "Copy text (OCR)"
            enabled: toolsMenu.n === 1 && (toolsMenu.allImages || toolsMenu.allPdf)
            onTriggered: root.runTool("ocr-copy")
        }
        Action {
            text: "Text to note (OCR)"
            enabled: toolsMenu.n === 1 && (toolsMenu.allImages || toolsMenu.allPdf)
            onTriggered: root.runTool("ocr-note")
        }
        DarkSeparator {}
        Action { text: "Convert to PNG"; enabled: toolsMenu.allImages; onTriggered: root.runTool("convert", "png") }
        Action { text: "Convert to JPG"; enabled: toolsMenu.allImages; onTriggered: root.runTool("convert", "jpg") }
        Action { text: "Convert to WebP"; enabled: toolsMenu.allImages; onTriggered: root.runTool("convert", "webp") }
        Action { text: "Resize to 1920 px wide"; enabled: toolsMenu.allImages; onTriggered: root.runTool("resize", "1920") }
        Action { text: "Resize to 1280 px wide"; enabled: toolsMenu.allImages; onTriggered: root.runTool("resize", "1280") }
        Action { text: "Resize to 800 px wide"; enabled: toolsMenu.allImages; onTriggered: root.runTool("resize", "800") }
        DarkSeparator {}
        Action {
            text: "Compress to .zip"
            onTriggered: {
                notch.zipItems(toolsMenu.targets)
                root.clearSelection()
            }
        }
    }

    Popup {
        id: extractPopup
        property string itemId
        property int pages: 0
        x: (root.width - width) / 2
        y: 90
        width: 320
        padding: 16
        focus: true
        function go() {
            notch.runTool("extract", [itemId], rangeField.text)
            close()
        }
        onOpened: {
            rangeField.text = ""
            rangeField.forceActiveFocus()
        }
        background: Rectangle { radius: 16; color: theme.surface; border.color: theme.outline }
        contentItem: ColumnLayout {
            spacing: 10
            SectionLabel { text: "Extract pages" }
            Caption {
                Layout.fillWidth: true
                text: (extractPopup.pages > 0 ? "This PDF has " + extractPopup.pages + " pages. " : "")
                      + "Example: 1-3, 5, 8-"
            }
            TextField {
                id: rangeField
                Layout.fillWidth: true
                implicitHeight: 30
                leftPadding: 12
                placeholderText: "Pages"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 13
                background: Rectangle { radius: 15; color: theme.well; border.width: 1; border.color: theme.accent }
                onAccepted: extractPopup.go()
                Keys.onEscapePressed: extractPopup.close()
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: extractPopup.close() }
                PillButton { text: "Extract"; checked: true; onClicked: extractPopup.go() }
            }
        }
    }

    DarkMenu {
        id: ruleMenu
        property string group
        Instantiator {
            model: notch.categories
            delegate: Action {
                required property var modelData
                text: modelData.name
                onTriggered: notch.setTypeRule(ruleMenu.group, modelData.id)
            }
            onObjectAdded: (index, object) => ruleMenu.insertAction(index, object)
            onObjectRemoved: (index, object) => ruleMenu.removeAction(object)
        }
        DarkSeparator {}
        Action { text: "No category"; onTriggered: notch.setTypeRule(ruleMenu.group, "") }
    }

    DarkMenu {
        id: removedMenu
        readonly property var targets: menu.targets
        Action { text: "Restore"; onTriggered: { notch.restoreItems(removedMenu.targets); root.clearSelection() } }
        Action { text: "Open"; onTriggered: notch.open(menu.itemId) }
        DarkSeparator {}
        Action { text: "Delete now"; onTriggered: { notch.deleteForever(removedMenu.targets); root.clearSelection() } }
    }

    DarkMenu {
        id: removedChipMenu
        Action { text: "Restore all"; onTriggered: notch.restoreItems(notch.visibleIds()) }
        Action { text: "Empty Recently removed"; onTriggered: notch.emptyRecentlyRemoved() }
    }

    DarkMenu {
        id: assignMenu
        property var targetIds: []
        Instantiator {
            model: notch.categories
            delegate: Action {
                required property var modelData
                text: modelData.name
                onTriggered: root.moveTo(assignMenu.targetIds, modelData.id)
            }
            onObjectAdded: (index, object) => assignMenu.insertAction(index, object)
            onObjectRemoved: (index, object) => assignMenu.removeAction(object)
        }
        Action { text: "No category"; onTriggered: root.moveTo(assignMenu.targetIds, "") }
        DarkSeparator {}
        Action { text: "New category…"; onTriggered: root.beginCategoryEdit("new", "", assignMenu.targetIds) }
    }

    DarkMenu {
        id: categoryMenu
        property string catId
        property string catName
        Action { text: "Rename…"; onTriggered: root.beginCategoryEdit(categoryMenu.catId, categoryMenu.catName, []) }
        Action { text: "Save category to…"; onTriggered: root.saveFiles(notch.categoryItemIds(categoryMenu.catId)) }
        Action { text: "Zip category"; onTriggered: notch.zipItems(notch.categoryItemIds(categoryMenu.catId)) }
        Action { text: "Delete category"; onTriggered: notch.deleteCategory(categoryMenu.catId) }
        DarkSeparator {}
        Action { enabled: false; text: "Its files stay in Notch" }
    }

    DarkMenu {
        id: sortMenu
        Repeater {
            model: [["date", "Date added"], ["name", "Name"], ["size", "Size"], ["type", "Type"]]
            delegate: MenuItem {
                id: sortItem
                required property var modelData
                implicitHeight: 30
                text: (notch.sortMode === modelData[0] ? "✓  " : "     ") + modelData[1]
                onTriggered: notch.sortMode = modelData[0]
                contentItem: Text {
                    text: sortItem.text
                    color: theme.text
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 4
                }
                background: Rectangle { radius: 8; color: sortItem.highlighted ? theme.surfaceHover : "transparent" }
            }
        }
    }

    FolderDialog {
        id: saveDialog
        property var targetIds: []
        title: "Save to folder"
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        onAccepted: notch.saveTo(targetIds, selectedFolder)
    }

    FolderDialog {
        id: folderDialog
        title: "Choose where Notch stores files"
        currentFolder: "file://" + notch.storageDir
        onAccepted: notch.setStorageDir(selectedFolder)
    }
}
