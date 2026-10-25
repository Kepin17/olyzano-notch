import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes

Window {
    id: root
    width: 840 // the widest panel (the Music page with lyrics, 780) and a margin; the rest of the window is click-through
    height: 620
    visible: false
    color: "transparent"
    title: "Olyzano Notch"

    readonly property color accent: theme.accent
    readonly property color textPrimary: theme.text
    readonly property color textSecondary: theme.textDim
    readonly property int openWidth: 560
    // The Music page with lyrics is split in two (player left, words right): wider, so each half has room.
    readonly property bool lyricsSplit: page === "music" && lyrics.enabled && media.active
    readonly property int panelWidth: lyricsSplit ? 780 : openWidth

    property bool pinned: false
    readonly property string capsuleMode: activities.capsuleMode
    onCapsuleModeChanged: {
        pinned = capsuleMode === "open" // "Always open" is a pin that stays
        updateMask()
    }
    Component.onCompleted: if (capsuleMode === "open") pinned = true
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
    // Set when a drag enters Notch, cleared on drop or when the pointer moves normally again. On
    // Wayland a DropArea can keep containsDrag after a drop that rebuilt the items under it
    // (e.g. a tile moved into a category), which would leave Notch stuck open.
    property bool dragLive: false
    property string infoText: ""        // short confirmation shown in the panel (saved, zip ready)
    Timer { id: infoTimer; interval: 3500; onTriggered: root.infoText = "" }
    readonly property bool selecting: page === "shelf" && selection.length > 0
    readonly property bool inRemoved: notch.categoryFilter === "__removed__" // Recently removed view
    readonly property bool anyMenu: menu.visible || moreMenu.visible || sendMenu.visible || assignMenu.visible || categoryMenu.visible
                                    || toolsMenu.visible || extractPopup.visible || settingsPage.overlayOpen
                                    || quickLookCard.visible || vaultPopup.visible || confirmDelete.visible
                                    || sharePopup.visible || incomingPopup.visible
                                    || sortMenu.visible || removedMenu.visible
                                    || removedChipMenu.visible
    readonly property bool dragOver: dragLive && (edgeDrop.containsDrag || islandDrop.containsDrag || chipDragOver)
    // Once it has started to open it stays open a moment, so a hover that blinks while the panel grows under a
    // still pointer (the input region changes with it) can't shut it again.
    Timer { id: holdTimer; interval: 900 }
    // `notch --debug-hover` prints what the pointer does at the top edge.
    readonly property bool debugHover: Qt.application.arguments.indexOf("--debug-hover") >= 0
    function dbg(what) { if (debugHover) console.log("hover", Date.now() % 100000, what, "mode=" + mode, "open=" + open, "dwelled=" + dwelled) }
    readonly property bool engaged: pinned || dwelled || holdTimer.running || (islandHover.hovered && mode !== "idle" && mode !== "prompt")
                                    || dragOver || anyMenu
                                    || saveDialog.visible || addDialog.visible
    // hidden: only the top-edge strip is live. peek: copy in progress. open: full panel.
    // toast: a short system alert (charger, Bluetooth). Shown even while hidden, like Dynamic Island.
    property var toast: null // {kind, title, detail, icon, level}
    readonly property color toastColor: !toast ? theme.outline
                                      : toast.kind === "charging" ? theme.success
                                      : toast.kind === "bluetooth" ? accent
                                      : toast.kind === "lowbattery" ? theme.danger
                                      : toast.kind === "media" ? theme.media
                                      : toast.kind === "file" ? theme.info
                                      : toast.kind === "timer" ? theme.accent
                                      : toast.kind === "call" || toast.kind === "phone" || toast.kind === "wellbeing" ? theme.success
                                      : toast.kind === "desktop" ? theme.info
                                      : toast.kind === "color" ? toast.detail : theme.textDim
    // prompt: the stretch reminder asking. idle: the small clock pill when nothing else is going on.
    readonly property string mode: wellbeing.state === "reminding" ? "prompt" : open ? "open" : toast ? "toast"
                                 : live ? "peek" : idleShown ? "idle" : "hidden"

    // ---- Stretch reminder ----
    // A film, a call or a Pomodoro holds the reminder back (each can be switched off in Settings).
    Binding {
        target: wellbeing
        property: "busy"
        value: (wellbeing.pauseForMedia && media.playing)
               || (wellbeing.pauseForCalls && (root.call !== null || activities.micApps.length > 0 || activities.cameraApps.length > 0))
               || (wellbeing.pauseForPomodoro && activities.timerKind === "pomodoro")
    }
    property double breakNow: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: wellbeing.state === "break"
        triggeredOnStart: true
        onTriggered: root.breakNow = Date.now()
    }
    readonly property double breakLeft: Math.max(0, wellbeing.breakEndsAt - breakNow)
    Connections {
        target: wellbeing
        function onBreakOver() { root.showToast("wellbeing", "Break's over", "Nicely done. Back to it", "chronometer", 5000) }
        function onEyeBreak() { root.showToast("wellbeing", "Rest your eyes", "Look at something far away for 20 seconds", "chronometer", 6000) }
    }

    // ---- Idle clock ----
    // Hidden while the window in front reaches this screen's top edge (a maximized browser's tabs, a title bar).
    readonly property bool idleCovered: dock.coveredScreen !== ""
                                        && (dock.coveredScreen === "*" || !root.screen || dock.coveredScreen === root.screen.name)
    // The capsule stays up whatever is in front (a maximised browser playing music used to hide it); only the
    // plain idle clock gives way to a window that reaches the top edge.
    readonly property bool idleShown: capsuleMode === "capsule" || (activities.idleClock && !idleCovered)
    property date idleNow: new Date()
    Timer {
        interval: 5000
        repeat: true
        running: root.mode === "idle"
        triggeredOnStart: true
        onTriggered: root.idleNow = new Date()
    }

    // ---- Now playing in the capsule ----
    // The song takes the capsule over while it plays, and for 2 minutes after a pause; then the clock returns.
    property bool musicStale: false
    readonly property bool musicShown: media.active && !musicStale
    property real capPos: 0 // ms, read once a second while the capsule shows the song
    Timer { id: pauseTimer; interval: 120000; onTriggered: root.musicStale = true }
    Timer {
        interval: 1000
        repeat: true
        running: root.musicShown && root.mode === "idle"
        triggeredOnStart: true
        onTriggered: root.capPos = media.positionMs()
    }
    Connections {
        target: media
        function onChanged() {
            if (media.playing || !media.active) {
                root.musicStale = false
                pauseTimer.stop()
            } else if (!pauseTimer.running && !root.musicStale) {
                pauseTimer.restart()
            }
            root.capPos = media.positionMs()
        }
        function onTrackChanged() { root.musicStale = false }
    }

    // ---- Lyrics follow the song ----
    // The player is read once a second; in between the clock runs on its own, so the lines change on time.
    property real lyricsRead: 0
    property double lyricsReadAt: 0
    Timer {
        interval: 250
        repeat: true
        triggeredOnStart: true
        running: lyrics.state === "synced" && media.active
                 && ((root.mode === "idle" && root.musicShown) || (root.mode === "open" && root.page === "music"))
        onTriggered: {
            const now = Date.now()
            if (!media.playing || now - root.lyricsReadAt > 1000) {
                root.lyricsRead = media.positionMs()
                root.lyricsReadAt = now
            }
            lyrics.setPosition(root.lyricsRead + (media.playing ? now - root.lyricsReadAt : 0))
        }
    }

    // ---- Live activities: what's going on, shown in the closed notch (most important first) ----
    property var call: null // {who, device} while the phone rings
    Timer { id: callTimer; interval: 45000; onTriggered: root.call = null }
    property double liveNow: 0
    Timer {
        interval: 500
        repeat: true
        running: activities.timerKind !== "" && !root.open
        triggeredOnStart: true
        onTriggered: root.liveNow = activities.timerMs()
    }
    function clock(ms) {
        const total = Math.round(ms / 1000)
        const h = Math.floor(total / 3600), m = Math.floor(total / 60) % 60, s = total % 60
        const two = n => (n < 10 ? "0" : "") + n
        return (h > 0 ? h + ":" + two(m) : two(m)) + ":" + two(s)
    }
    function formatBytes(b) {
        return b >= 1073741824 ? (b / 1073741824).toFixed(1) + " GB" : b >= 1048576 ? (b / 1048576).toFixed(1) + " MB"
             : Math.max(1, Math.round(b / 1024)) + " KB"
    }
    // {kind, title, detail, progress (0-1, -1 none), color}
    readonly property var live: {
        if (call)
            return { kind: "call", title: call.who, detail: "Calling", progress: -1, color: theme.success }
        if (wellbeing.state === "break") {
            const tips = wellbeing.tips
            const total = wellbeing.breakMinutes * 60000
            return { kind: "break", color: theme.success, detail: clock(breakLeft), progress: total > 0 ? 1 - breakLeft / total : -1,
                     title: wellbeing.userActive ? "Still on a break" : tips[Math.floor(breakNow / 20000) % tips.length] }
        }
        if (activities.timerKind !== "") {
            const total = activities.timerTotalMs
            return { kind: "timer", title: activities.timerLabel + (activities.timerRunning ? "" : " · paused"),
                     detail: clock(liveNow), progress: total > 0 ? 1 - liveNow / total : -1,
                     color: activities.timerLabel === "Break" ? theme.success : theme.accent }
        }
        const t = nearby.activity
        if (t && t.peer !== undefined)
            return { kind: "transfer", title: (t.direction === "out" ? "Sending to " : "Receiving from ") + t.peer,
                     detail: t.progress >= 0 ? Math.round(t.progress * 100) + "%" : "", progress: t.progress, color: theme.info }
        const downloads = activities.downloads
        if (downloads.length > 0) {
            const d = downloads[0]
            return { kind: "download", title: d.name, progress: -1, color: theme.info,
                     detail: formatBytes(d.bytes) + (d.speed > 1024 ? " · " + formatBytes(d.speed) + "/s" : "")
                             + (downloads.length > 1 ? " · +" + (downloads.length - 1) : "") }
        }
        if (notch.copying > 0)
            return { kind: "copy", title: "Copying " + notch.copying + (notch.copying === 1 ? " file" : " files"),
                     detail: String(notch.count), progress: -1, color: theme.accent }
        return null
    }

    function showToast(kind, title, detail, icon, ms) {
        if (root.open) {
            root.infoText = title + (detail ? " · " + detail : "")
            infoTimer.restart()
            return
        }
        root.toast = { kind: kind, title: title, detail: detail, icon: icon, art: "", level: -1 }
        toastTimer.interval = ms || 3600
        toastTimer.restart()
    }
    Connections {
        target: activities
        function onTimerFinished(title, detail) { root.showToast("timer", title, detail, "chronometer", 6000) }
    }
    property var phoneLowWarned: ({})
    Connections {
        target: kdeconnect
        function onPhoneCall(event, who, device) {
            if (!activities.phoneAlerts)
                return
            if (event === "ringing") {
                root.call = { who: who, device: device }
                callTimer.restart()
                return
            }
            root.call = null
            if (event === "missedCall")
                root.showToast("call", "Missed call", who, "smartphone", 6000)
        }
        // Just who it's from ("WhatsApp · Mom"): a whole message doesn't fit the notch and may be private.
        function onPhoneNotification(app, title, text, device) {
            if (!activities.phoneAlerts || !(app || title))
                return
            root.showToast("phone", app ? "Notification from " + app : "Notification", title || device, "smartphone", 4500)
        }
        function onPhoneBattery(charge, charging, device) {
            if (!activities.phoneAlerts)
                return
            const warned = root.phoneLowWarned
            if (charging || charge > 20) {
                warned[device] = false
            } else if (charge <= 15 && !warned[device]) {
                warned[device] = true
                root.showToast("lowbattery", device + " battery low", charge + "%", "smartphone", 5000)
                sounds.play("warning") // every screen's notch gets here: Sounds plays it once
            }
            root.phoneLowWarned = warned
        }
    }

    Connections {
        target: desktopNotifications
        // Other apps on this computer: who it's from and the title, like the phone's.
        function onNotification(app, title, icon) {
            root.showToast("desktop", app ? "Notification from " + app : "Notification", title, "notifications", 4500)
        }
    }

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
        dbg("engaged=" + engaged)
        if (engaged) {
            if (!open)
                holdTimer.restart()
            closeTimer.stop()
            if ((mode === "toast" && toast && toast.kind === "media") || (mode === "idle" && musicShown))
                page = "music" // pointing at the now-playing alert or the capsule opens just the player
            open = true
        } else {
            closeTimer.restart()
        }
    }
    onOpenChanged: {
        dbg("open=" + open)
        if (open)
            return
        page = "shelf"
        editingCategory = ""
        clearSelection()
        vaultLockTimer.restart()
    }
    // Locked categories close with Notch, after a short grace so a file just dragged out or opened
    // can still be read by the other app.
    Timer {
        id: vaultLockTimer
        interval: 5000
        onTriggered: if (!root.open) notch.lockVault()
    }
    Connections {
        target: media
        function onChanged() { if (!media.active && root.page === "music") root.page = "shelf" }
    }
    onDragOverChanged: if (dragOver) page = "shelf" // a file being dragged in: show where it lands
    onAnyMenuChanged: updateMask()
    Timer { id: closeTimer; interval: 380; onTriggered: root.open = false }
    // Dwell before revealing, so flinging the mouse up to a tab doesn't pop the panel.
    Timer { id: dwellTimer; interval: 160; onTriggered: root.dwelled = true }

    // Everything outside the live area is click-through.
    function updateMask() {
        if (anyMenu)
            desktop.setInputRegion(root, Qt.rect(0, 0, width, height))
        else if (mode === "hidden" || (mode === "idle" && capsuleMode !== "capsule")) // the idle clock is see-through for clicks; the top edge opens Notch (the capsule itself can be pointed at)
            desktop.setInputRegion(root, Qt.rect(edge.x, 0, edge.width, edge.height))
        else
            desktop.setInputRegion(root, Qt.rect(island.x, 0, island.width, Math.max(island.height, edge.height)))
    }
    onVisibleChanged: updateMask()
    onModeChanged: updateMask()

    // Files or text from outside Notch; drags of Notch's own tiles/chips carry a source and are ignored.
    function isFileDrop(drag) {
        return (drag.hasUrls || drag.hasText) && !drag.source
    }

    function acceptDrop(d) {
        dragLive = false
        if (!d.source && !d.hasUrls && d.hasText) { // a text selection, not a file
            notch.addTextNote(d.text, notch.categoryFilter)
            d.acceptProposedAction()
        } else if (isFileDrop(d)) {
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

    function shareFiles(ids) {
        sharePopup.openFor(ids)
        clearSelection()
    }

    // Files in a locked category are deleted for good (no Recently removed): ask first.
    function removeWithConfirm(ids) {
        if (ids.some(id => notch.itemInfo(id).vault)) {
            confirmDelete.ids = ids.slice()
            confirmDelete.open()
        } else {
            notch.removeItems(ids)
        }
    }

    readonly property bool lockedClosed: !notch.vaultUnlocked
        && notch.categories.some(c => c.id === notch.categoryFilter && c.locked)

    // Unlock (or create the vault first), then run `then`, e.g. locking a category.
    function askPassword(mode, then) {
        vaultPopup.mode = mode
        vaultPopup.then = then || null
        vaultPopup.open()
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
            pinned = capsuleMode === "open" || !pinned
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

    function saveNote() {
        if (!notch.saveNote(noteId, noteFileName(), noteText))
            return
        noteId = ""
        noteName = ""
        noteText = ""
        page = "shelf"
    }

    onPageChanged: if (page === "note") notePage.load()
    function restartInfo() { infoTimer.restart() }

    function showPreview(item) {
        if (isEditable(item)) {
            editNote(item)
            return
        }
        preview = item
        previewText = item.isText ? notch.previewText(item.id) : ""
        page = "preview"
    }











    Item {
        id: island
        x: (root.width - width) / 2
        width: root.mode === "open" ? root.panelWidth : root.mode === "prompt" ? 440 : root.mode === "toast" ? 380
             : root.mode === "peek" ? (root.live && root.live.kind !== "copy" ? 320 : 230)
             : root.mode === "idle" ? (root.musicShown ? 400 : systemEvents.batteryLevel >= 0 ? 230 : 180) : 150
        height: root.mode === "open" ? panel.implicitHeight + (root.dragOver ? 8 : 0)
              : root.mode === "prompt" ? 92 : root.mode === "toast" ? 46 : root.mode === "peek" ? 36 : root.mode === "idle" ? (root.musicShown ? 38 : 26) : 0
        focus: true

        readonly property real shoulder: Math.min(10, Math.max(0, height) / 3)
        readonly property real corner: Math.min(24, Math.max(0, height - shoulder))
        readonly property bool closing: root.mode === "hidden"

        // Height is stiffer than width: the drop falls first, then spreads and settles.
        Behavior on height { SpringAnimation { spring: 4.2; damping: island.closing ? 0.9 : 0.32; epsilon: 0.25 } }
        Behavior on width { SpringAnimation { spring: island.closing ? 5 : 2.6; damping: island.closing ? 0.9 : 0.3; epsilon: 0.25 } }
        onWidthChanged: root.updateMask()
        onHeightChanged: root.updateMask()

        Keys.onReleased: (e) => {
            if (e.key === Qt.Key_Space && !e.isAutoRepeat && quickLookCard.visible) {
                quickLookCard.hide()
                e.accepted = true
            }
        }
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
                root.removeWithConfirm(root.selection)
                root.clearSelection()
                e.accepted = true
            } else if (e.key === Qt.Key_Space && list.hoveredId !== "" && root.page === "shelf") {
                if (!e.isAutoRepeat)
                    quickLookCard.show(list.hoveredId) // held Space: Quick Look until released
                e.accepted = true
            } else if (e.key === Qt.Key_Space || e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                root.pinned = root.capsuleMode === "open" || !root.pinned
                e.accepted = true
            } else if (e.key === Qt.Key_Escape) {
                if (root.selecting) {
                    root.clearSelection()
                } else if (root.page !== "shelf") {
                    root.page = "shelf"
                } else {
                    if (root.capsuleMode !== "open") { // "Always open" stays
                        root.pinned = false
                        root.open = false
                    }
                }
                e.accepted = true
            }
        }

        // Declared first so it sits underneath: category chips get first pick of a drop.
        DropArea {
            id: islandDrop
            anchors.fill: parent
            enabled: root.mode !== "hidden"
            onEntered: (drag) => {
                drag.accepted = root.isFileDrop(drag)
                root.dragLive = root.dragLive || drag.accepted
            }
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
                        color: toastRow.t.kind === "charging" ? theme.success : toastRow.t.level <= 20 ? theme.danger : theme.text
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
                radius: theme.radiusMd
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
                Layout.maximumWidth: toastRow.t.kind === "media" || toastRow.t.kind === "file" ? 130 : 150
                elide: Text.ElideRight
                text: toastRow.t.kind === "bluetooth" && toastRow.t.level >= 0
                      ? toastRow.t.detail + " · " + toastRow.t.level + "%" : toastRow.t.detail
                color: toastRow.t.kind === "charging" ? theme.success
                     : toastRow.t.kind === "lowbattery" ? theme.danger
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

        // Peek: the live activity (call, timer, transfer, download, copy) as a compact pill.
        RowLayout {
            id: liveRow
            readonly property var l: root.live || ({ kind: "", title: "", detail: "", progress: -1, color: theme.accent })
            anchors.fill: parent
            anchors.leftMargin: island.shoulder + 16
            anchors.rightMargin: island.shoulder + 16
            anchors.bottomMargin: liveRow.l.progress >= 0 ? 4 : 0
            spacing: 8
            opacity: root.mode === "peek" && island.height > 24 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            PulseDot {
                visible: liveRow.l.kind === "copy" || liveRow.l.kind === "call"
                color: liveRow.l.color
            }
            Text {
                visible: !(liveRow.l.kind === "copy" || liveRow.l.kind === "call")
                text: liveRow.l.kind === "timer" ? "⏱" : liveRow.l.kind === "break" ? "🧘" : liveRow.l.kind === "download" ? "↓" : "⇄"
                color: liveRow.l.color
                font.pixelSize: 13
                font.weight: Font.Bold
            }
            Text {
                Layout.fillWidth: true
                text: liveRow.l.title
                color: root.textPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text {
                text: liveRow.l.detail
                readonly property bool clockLike: liveRow.l.kind === "timer" || liveRow.l.kind === "break"
                color: clockLike || liveRow.l.kind === "call" ? liveRow.l.color : root.textSecondary
                font.pixelSize: 12
                font.weight: clockLike ? Font.DemiBold : Font.Normal
                font.family: clockLike ? "monospace" : Qt.application.font.family
            }
        }
        // Its progress, as a hairline along the bottom of the pill.
        // The fraction is animated, not the width: a width animating while the notch shrinks from the panel
        // to the pill ran past the end of the track and out of the notch.
        Rectangle {
            id: liveTrack
            property real shown: Math.max(0, Math.min(1, liveRow.l.progress))
            Behavior on shown { NumberAnimation { duration: 400 } }
            visible: root.mode === "peek" && island.height > 24 && liveRow.l.progress >= 0
            x: island.shoulder + 18
            y: island.height - 6
            width: Math.max(0, island.width - 2 * (island.shoulder + 18))
            height: 2
            radius: 1
            clip: true
            color: theme.surface
            Rectangle {
                width: liveTrack.width * liveTrack.shown
                height: parent.height
                radius: 1
                color: liveRow.l.color
            }
        }

        // Idle: the time, the microphone and the battery, while nothing else is going on.
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: island.shoulder + 14
            anchors.rightMargin: island.shoulder + 14
            spacing: 8
            opacity: root.mode === "idle" && !root.musicShown && island.height > 20 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            Text {
                text: Qt.formatTime(root.idleNow, Qt.locale().timeFormat(Locale.ShortFormat))
                color: root.textPrimary
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            // Microphone: orange while recording, red and struck through while muted.
            LineIcon {
                visible: activities.micMuted >= 0
                glyph: activities.micMuted === 1 ? "mic-off" : "mic"
                sourceSize: Qt.size(13, 13)
                color: activities.micMuted === 1 ? theme.danger : activities.micApps.length > 0 ? theme.warn : root.textSecondary
            }
            // Battery
            Item {
                visible: systemEvents.batteryLevel >= 0
                implicitWidth: 22
                implicitHeight: 11
                Rectangle {
                    id: idleBattery
                    width: 20
                    height: 11
                    radius: 3
                    color: "transparent"
                    border.width: 1.2
                    border.color: root.textSecondary
                    Rectangle {
                        x: 2; y: 2
                        height: parent.height - 4
                        radius: 1.5
                        width: Math.max(1.5, (parent.width - 4) * systemEvents.batteryLevel / 100)
                        color: systemEvents.batteryCharging ? theme.success : systemEvents.batteryLevel <= 20 ? theme.danger : root.textPrimary
                    }
                }
                Rectangle { x: 20.5; y: 3.5; width: 1.5; height: 4; radius: 0.75; color: root.textSecondary }
            }
            Text {
                visible: systemEvents.batteryLevel >= 0
                text: (systemEvents.batteryCharging ? "⚡" : "") + systemEvents.batteryLevel + "%"
                color: root.textSecondary
                font.pixelSize: 11
            }
        }

        // Idle with music: the wave, the song and where it is, instead of the clock.
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: island.shoulder + 14
            anchors.rightMargin: island.shoulder + 14
            spacing: 10
            opacity: root.mode === "idle" && root.musicShown && island.height > 20 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            Equalizer {
                barWidth: 2.5
                maxHeight: 18
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                Layout.fillWidth: true
                text: lyrics.currentText !== "" ? lyrics.currentText
                      : media.title !== "" ? media.title + (media.artist !== "" ? " · " + media.artist : "") : media.identity
                color: root.textPrimary
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                text: root.clock(root.capPos) + (media.lengthMs > 0 ? " / " + root.clock(media.lengthMs) : "")
                color: root.textSecondary
                font.pixelSize: 13
                font.family: "monospace"
            }
            LineIcon {
                visible: activities.micApps.length > 0
                glyph: "mic"
                sourceSize: Qt.size(15, 15)
                color: theme.warn
            }
        }

        // Prompt: the stretch reminder. The person chooses; nothing happens until they do (or a minute passes = later).
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: island.shoulder + 16
            anchors.rightMargin: island.shoulder + 16
            anchors.topMargin: 10
            anchors.bottomMargin: 12
            spacing: 8
            opacity: root.mode === "prompt" && island.height > 70 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text { text: "🧘"; font.pixelSize: 20 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: "Time to stretch"
                        color: root.textPrimary
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "You've worked for " + Math.round(wellbeing.workedMs / 60000) + " min. A short break helps."
                        color: root.textSecondary
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Item { Layout.fillWidth: true }
                PillButton { text: "Later (" + wellbeing.snoozeMinutes + " min)"; onClicked: wellbeing.later() }
                PillButton { text: "Skip"; onClicked: wellbeing.skip() }
                PillButton { text: "Start break"; checked: true; onClicked: wellbeing.startBreak() }
            }
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
                width: root.panelWidth - 20
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.leftMargin: root.page === "shelf" ? 12 : 6
                    Layout.rightMargin: 8
                    spacing: 8

                    IconButton {
                        visible: root.page !== "shelf"
                        iconName: "back"
                        Accessible.name: "Back"
                        onClicked: root.page = "shelf"
                    }
                    Text {
                        Layout.maximumWidth: 300
                        text: root.selecting ? root.selection.length + " selected"
                            : root.page === "settings" ? "Settings" : root.page === "preview" ? root.preview.name
                            : root.page === "note" ? (root.noteId !== "" ? "Edit note" : "New note")
                            : root.page === "timer" ? "Timer" : root.page === "music" ? "Now playing" : "Olyzano Notch"
                        color: root.textPrimary
                        font.family: theme.displayFont !== "" ? theme.displayFont : Qt.application.font.family
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    Text { // washi: 通知 ("notice"), a quiet subtitle
                        visible: theme.washi && root.page === "shelf" && !root.selecting
                        text: "通知"
                        color: theme.textFaint
                        font.family: theme.displayFont
                        font.pixelSize: 11
                    }
                    Rectangle {
                        visible: root.page === "shelf" && !root.selecting
                        implicitWidth: Math.max(20, countLabel.implicitWidth + 12)
                        implicitHeight: 20
                        radius: theme.washi ? 3 : 10 // washi: a small vermilion hanko seal
                        color: theme.washi ? theme.accent : theme.surface
                        rotation: theme.washi ? -4 : 0
                        Text {
                            id: countLabel
                            anchors.centerIn: parent
                            text: notch.count
                            color: theme.washi ? "white" : root.textSecondary
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
                            radius: theme.chipRadius
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
                        text: "Share"
                        onClicked: root.shareFiles(root.selection)
                    }
                    PillButton {
                        visible: root.selecting && !root.inRemoved
                        text: "Remove"
                        onClicked: {
                            root.removeWithConfirm(root.selection)
                            root.clearSelection()
                        }
                    }
                    IconButton {
                        visible: root.selecting
                        iconName: "close"
                        Accessible.name: "Clear selection"
                        onClicked: root.clearSelection()
                    }
                    PillButton {
                        visible: root.page === "note"
                        text: "Save"
                        checked: true
                        Accessible.name: "Save note"
                        onClicked: root.saveNote()
                    }
                    IconButton {
                        visible: root.page === "shelf" && !root.selecting && notch.vaultUnlocked
                        iconName: "unlock"
                        Accessible.name: "Lock locked categories now"
                        onClicked: notch.lockVault()
                    }
                    // Recently removed: a header button (with count) rather than a chip at the far end.
                    IconButton {
                        id: trashButton
                        visible: root.page === "shelf" && !root.selecting && (notch.removedCount > 0 || root.inRemoved)
                        iconName: "trash"
                        checked: root.inRemoved
                        Accessible.name: "Recently removed"
                        onClicked: notch.categoryFilter = root.inRemoved ? "" : "__removed__"
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: removedChipMenu.popup()
                        }
                        Rectangle {
                            visible: notch.removedCount > 0
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: -4
                            width: Math.max(16, trashCount.implicitWidth + 8)
                            height: 16
                            radius: 8
                            color: theme.danger
                            Text {
                                id: trashCount
                                anchors.centerIn: parent
                                text: notch.removedCount
                                color: "white"
                                font.pixelSize: 9
                                font.bold: true
                            }
                        }
                        // Drop tiles on it to remove them.
                        DropArea {
                            anchors.fill: parent
                            onEntered: (drag) => { drag.accepted = drag.source && drag.source.draggedIds !== undefined }
                            onDropped: (d) => root.removeWithConfirm(d.source.draggedIds)
                        }
                    }
                    IconButton {
                        visible: (root.page === "shelf" || root.page === "music" || root.page === "timer") && !root.selecting
                        text: "⋯"
                        checked: moreMenu.visible
                        Accessible.name: "More"
                        onClicked: moreMenu.popup()
                    }
                    IconButton {
                        visible: (root.page === "shelf" || root.page === "music" || root.page === "timer") && !root.selecting
                        iconName: "gear"
                        Accessible.name: "Settings"
                        onClicked: root.page = "settings"
                    }
                    PillButton {
                        visible: !root.selecting && root.capsuleMode !== "open" // room for the selection actions
                        text: root.pinned ? "Pinned" : "Pin"
                        checked: root.pinned
                        onClicked: root.pinned = !root.pinned
                        Accessible.name: "Keep Olyzano Notch open"
                    }
                }

                // What to look at: one section at a time. Dropping files is an action too (Add).
                ActionBar {
                    visible: !root.selecting && (root.page === "shelf" || root.page === "music" || root.page === "timer")
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    page: root.page
                    musicAvailable: media.active
                    shareAvailable: share.count + nearby.count > 0
                    shareBusy: share.activeCount + nearby.activeCount > 0
                    onPick: (target) => root.page = target
                    onAddFiles: addDialog.open()
                    lyricsOn: lyrics.enabled
                    onToggleLyrics: {
                        if (lyrics.enabled) {
                            lyrics.enabled = false
                        } else {
                            settingsPage.category = "general" // switching it on sends the song to a lyrics service: say so first
                            root.page = "settings"
                        }
                    }
                    onOpenShares: sharePopup.visible && sharePopup.view === "sessions" ? sharePopup.close() : sharePopup.openSessions()
                }

                // Music: the player, and (when lyrics are on) the words beside it, half and half.
                RowLayout {
                    visible: root.page === "music" && media.active
                    Layout.fillWidth: true
                    Layout.leftMargin: 12 // the same edges as the action bar above
                    Layout.rightMargin: 12
                    spacing: 12
                    // Both halves are the same size and height, so the two cards line up.
                    MediaCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.minimumHeight: 214
                    }
                    LyricsPanel {
                        visible: lyrics.enabled
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.minimumHeight: 214
                    }
                }

                Rectangle {
                    visible: notch.errorMessage !== ""
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    implicitHeight: errorText.implicitHeight + 16
                    radius: theme.radiusMd
                    color: theme.dangerBg
                    Text {
                        id: errorText
                        anchors.fill: parent
                        anchors.margins: 8
                        anchors.rightMargin: 28
                        text: notch.errorMessage
                        color: theme.danger
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 10
                        text: "✕"
                        color: theme.danger
                        font.pixelSize: 11
                    }
                    TapHandler { onTapped: notch.clearError() }
                }

                // Washi: a hairline under the header, like a brush rule.
                Rectangle {
                    visible: theme.washi && root.page === "shelf"
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    implicitHeight: 1
                    color: theme.outline
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
                            model: [{ id: "", name: "All", count: notch.count }].concat(notch.categories)
                            delegate: Chip {
                                required property var modelData
                                catId: modelData.id
                                text: modelData.name
                                itemCount: modelData.count
                                special: modelData.special || ""
                                locked: modelData.locked || false
                                onDropDone: root.dragLive = false
                                onDropHoverChanged: {
                                    root.chipDragOver = dropHover
                                    if (dropHover)
                                        root.dragLive = true
                                }
                                // The chip row is rebuilt whenever categories change (e.g. right after a
                                // drop); a chip destroyed mid-hover would otherwise leave Notch stuck open.
                                Component.onDestruction: if (dropHover) root.chipDragOver = false
                                onMenuRequested: {
                                    categoryMenu.catId = catId
                                    categoryMenu.catName = text
                                    categoryMenu.popup()
                                }
                            }
                            anchors.fill: parent
                            WheelScroller { flickable: chips }
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
                        iconName: "plus"
                        Accessible.name: "New category"
                        onClicked: root.beginCategoryEdit("new", "", [])
                    }

                    PillButton {
                        visible: notch.count > 1
                        iconName: "sort"
                        text: ({ date: "Date", name: "Name", size: "Size", type: "Type" })[notch.sortMode]
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

                // ---- Locked category, vault closed ----
                Item {
                    visible: root.page === "shelf" && root.lockedClosed
                    Layout.fillWidth: true
                    Layout.preferredHeight: 144
                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                        LineIcon { anchors.horizontalCenter: parent.horizontalCenter; glyph: "lock"; sourceSize: Qt.size(28, 28); color: theme.textDim }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "This category is locked"
                            color: root.textSecondary
                            font.pixelSize: 13
                        }
                        PillButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Unlock…"
                            checked: true
                            onClicked: root.askPassword("unlock")
                        }
                    }
                }

                // ---- Shelf ----
                Item {
                    id: emptyState
                    visible: root.page === "shelf" && notch.count === 0 && !root.lockedClosed
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.preferredHeight: 76
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
                                radius: theme.radiusLg
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
                    visible: root.page === "shelf" && notch.count > 0 && list.count === 0 && !root.lockedClosed
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
                    // Tiles share the row exactly: as many whole tiles (≥ 96 px) as fit, never a cut-off one.
                    readonly property int tilesPerRow: Math.max(1, Math.floor((width + spacing) / (96 + spacing)))
                    readonly property real tileWidth: (width - (tilesPerRow - 1) * spacing) / tilesPerRow
                    property string hoveredId: ""
                    function quickLook(id) { quickLookCard.show(id) }
                    function endQuickLook() { quickLookCard.hide() }
                    property string renamingId: ""
                    function commitRename(id, name) {
                        renamingId = ""
                        notch.renameItem(id, name)
                        island.forceActiveFocus()
                    }
                    function requestRemove(ids) { root.removeWithConfirm(ids) }
                    function dragIds(id) { return root.selection.indexOf(id) >= 0 ? root.selection : [id] }
                    // Flick-dragging would fight the tiles' drag-out, so scrolling is wheel/touchpad + scrollbar.
                    interactive: false
                    WheelScroller { id: tileScroller; flickable: list; step: list.tileWidth + list.spacing }
                    ScrollBar.horizontal: ScrollBar {
                        id: tileBar
                        onPressedChanged: if (!pressed) tileScroller.snap() // land on a whole tile
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
                    radius: theme.radiusLg
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

                TimerPage {
                    shell: root
                }

                // On a break: how long is left, and a way to end it early.
                Rectangle {
                    visible: root.page === "shelf" && wellbeing.state === "break"
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    implicitHeight: 36
                    radius: theme.radiusMd
                    color: theme.surface
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 5
                        Text {
                            Layout.fillWidth: true
                            text: "🧘  On a break · " + root.clock(root.breakLeft) + " left"
                            color: root.textPrimary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        PillButton { text: "End break"; onClicked: wellbeing.endBreak() }
                    }
                }

                // Who is using the microphone or a camera right now (the dots by the closed notch, spelled out).
                Text {
                    visible: root.page === "shelf" && activities.privacyIndicators
                             && (activities.micApps.length > 0 || activities.cameraApps.length > 0)
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    text: (activities.cameraApps.length > 0 ? "● Camera: " + activities.cameraApps.join(", ") : "")
                          + (activities.cameraApps.length > 0 && activities.micApps.length > 0 ? "    " : "")
                          + (activities.micApps.length > 0 ? "● Microphone: " + activities.micApps.join(", ") : "")
                    color: activities.cameraApps.length > 0 ? theme.success : theme.warn
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }

                NotePage {
                    id: notePage
                    shell: root
                }

                SettingsPage {
                    id: settingsPage
                    shell: root
                }

                Rectangle {
                    visible: root.infoText !== ""
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    implicitHeight: 34
                    radius: theme.radiusMd
                    color: theme.infoBg
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        verticalAlignment: Text.AlignVCenter
                        readonly property bool warning: root.infoText.startsWith("⚠")
                        text: (warning ? "" : "✓  ") + root.infoText
                        color: warning ? theme.warn : theme.info
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
                    radius: theme.radiusMd
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

        // A click on the capsule while music plays goes straight to the player (without waiting for the dwell).
        TapHandler {
            enabled: root.mode === "idle" && root.musicShown
            onTapped: {
                root.page = "music"
                root.dwelled = true
            }
        }

        // Real pointer motion means no drag is in progress (a drag delivers drag events instead).
        // Synthetic hovers (sent while animating) repeat the last position, so they don't count.
        HoverHandler {
            id: islandHover
            property point last
            onHoveredChanged: {
                root.dbg("island hovered=" + hovered)
                if (hovered && root.mode === "idle") {
                    if (root.capsuleMode === "capsule") { // the capsule is there to be pointed at: open at once
                        dwellTimer.stop()
                        root.dwelled = true
                    } else {
                        dwellTimer.restart()
                    }
                } else if (!hovered && !edgeHover.hovered) {
                    dwellTimer.stop()
                    root.dwelled = false
                }
            }
            onPointChanged: {
                const p = point.position
                if (p.x !== last.x || p.y !== last.y) {
                    last = p
                    root.dragLive = false
                }
            }
        }
    }

    // Privacy dot, like iOS / macOS: green while a camera is on. Beside the notch at the top edge, shown even
    // while it's closed. Visual only. (The microphone has no dot: apps often keep it open, and the idle clock's
    // mic icon already turns orange.)
    Rectangle {
        visible: activities.privacyIndicators && root.mode !== "open" && activities.cameraApps.length > 0
        x: root.width / 2 + Math.max(island.width, 44) / 2 + 6
        y: 3
        width: 7; height: 7; radius: 3.5
        color: "#30d158"
    }

    // Black handle hinting where Notch lives; red when there's an unread error. Visual only.
    Rectangle {
        x: (root.width - width) / 2
        y: 0
        width: 44
        height: 3
        radius: 1.5
        color: notch.errorMessage !== "" ? theme.danger : "#000000"
        // A hair of light around the black, so it still shows on a dark wallpaper.
        border.width: 0.5
        border.color: notch.errorMessage !== "" ? "transparent" : Qt.rgba(1, 1, 1, 0.22)
        opacity: root.mode !== "hidden" ? 0 : edgeHover.hovered ? 1 : 0.9
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
                root.dbg("edge hovered=" + hovered)
                if (hovered) {
                    dwellTimer.restart()
                } else if (!islandHover.hovered) { // sliding down from the strip into the capsule is not leaving
                    dwellTimer.stop()
                    root.dwelled = false
                }
            }
        }
        DropArea {
            id: edgeDrop
            anchors.fill: parent
            onEntered: (drag) => {
                drag.accepted = root.isFileDrop(drag)
                root.dragLive = root.dragLive || drag.accepted
            }
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
        Action { text: "Share via QR…"; onTriggered: root.shareFiles(menu.targets) }
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
            text: menu.targets.length > 1 ? "Remove " + menu.targets.length + " files" : "Remove from Olyzano Notch"
            onTriggered: {
                root.removeWithConfirm(menu.targets)
                root.clearSelection()
            }
        }
        DarkSeparator {}
        Action { enabled: false; text: "From: " + (menu.sourcePath || "unknown") }
    }

    // Quick Look: a large preview over the island while Space / the mouse button is held.
    Rectangle {
        id: quickLookCard
        property string itemId
        property var info: ({})
        property string picture: ""
        property string text: ""
        function show(id) {
            info = notch.itemInfo(id)
            itemId = id
            picture = notch.quickLookImage(id)
            text = picture === "" && info.isText ? notch.previewText(id) : ""
            visible = true
        }
        function hide() { visible = false }
        visible: false
        z: 100
        x: (root.width - width) / 2
        y: 8
        width: Math.min(root.width - 40, 640)
        height: root.height - 24
        radius: 22
        color: theme.card
        border.color: theme.outline
        scale: visible ? 1 : 0.92
        opacity: visible ? 1 : 0
        Behavior on scale { SpringAnimation { spring: 5; damping: 0.4 } }
        Behavior on opacity { NumberAnimation { duration: 120 } }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Image {
                    anchors.fill: parent
                    visible: quickLookCard.picture !== ""
                    source: quickLookCard.picture
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 1280
                    asynchronous: true
                    cache: false
                }
                Text {
                    anchors.fill: parent
                    visible: quickLookCard.text !== ""
                    text: quickLookCard.text
                    color: theme.textSoft
                    font.family: "monospace"
                    font.pixelSize: 12
                    wrapMode: Text.WrapAnywhere
                    clip: true
                }
                Image {
                    anchors.centerIn: parent
                    visible: quickLookCard.picture === "" && quickLookCard.text === ""
                    width: 128
                    height: 128
                    source: quickLookCard.visible && quickLookCard.info.iconName ? "image://mime/" + quickLookCard.info.iconName : ""
                    sourceSize: Qt.size(256, 256)
                }
            }
            Text {
                Layout.fillWidth: true
                text: (quickLookCard.info.name || "") + "  ·  " + (quickLookCard.info.sizeText || "") + "  ·  "
                      + (quickLookCard.info.addedText || "")
                color: theme.textDim
                font.pixelSize: 12
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // The header's less-used buttons.
    DarkMenu {
        id: moreMenu
        Action { text: "New note"; onTriggered: root.newNote() }
        Action {
            text: "Pick a color from the screen"
            enabled: colorPicker.supported
            onTriggered: {
                // Get out of the way so anything on screen (even under Notch) can be picked
                // (not in "Always open": it would not come back).
                if (root.capsuleMode !== "open") {
                    root.pinned = false
                    root.open = false
                }
                colorPicker.pick()
            }
        }
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

    SharePopup {
        id: sharePopup
        shell: root
    }

    IncomingPopup {
        id: incomingPopup
        shell: root
    }
    Connections {
        target: nearby
        function onIncomingRequest(id, from, files, total, code, lowDisk) {
            root.pinned = true // a request must not hide behind other windows
            incomingPopup.ask(id, from, files, total, code, lowDisk)
        }
    }

    // Vault password: create (typed twice, with the no-recovery warning) or unlock.
    Popup {
        id: vaultPopup
        property string mode: "unlock" // "create" | "unlock"
        property var then: null
        property string error: ""
        x: (root.width - width) / 2
        y: 64
        width: 340
        padding: 16
        focus: true
        function go() {
            if (mode === "create" && pass1.text !== pass2.text) {
                error = "The passwords don't match"
                return
            }
            // ponytail: gocryptfs runs synchronously (~1 s of key stretching); move to a worker if it ever feels stuck.
            let err = mode === "create" ? notch.createVault(pass1.text) : ""
            if (err === "")
                err = notch.unlockVault(pass1.text)
            if (err !== "") {
                error = err.charAt(0).toUpperCase() + err.slice(1)
                pass1.selectAll()
                return
            }
            const next = then
            close()
            if (next)
                next()
        }
        onOpened: {
            error = ""
            pass1.forceActiveFocus()
        }
        onClosed: {
            pass1.clear()
            pass2.clear()
            then = null
        }
        background: Rectangle { radius: 16; color: theme.surface; border.color: theme.outline }
        contentItem: ColumnLayout {
            spacing: 10
            SectionLabel { text: vaultPopup.mode === "create" ? "Lock with a password" : "Unlock locked categories" }
            Caption {
                Layout.fillWidth: true
                visible: vaultPopup.mode === "create"
                text: "Files here are encrypted with this password (at least 8 characters). Forget it and nobody can recover them."
            }
            TextField {
                id: pass1
                Layout.fillWidth: true
                implicitHeight: 30
                leftPadding: 12
                echoMode: TextInput.Password
                placeholderText: "Password"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 13
                background: Rectangle { radius: 15; color: theme.well; border.width: 1; border.color: theme.accent }
                onAccepted: vaultPopup.mode === "create" ? pass2.forceActiveFocus() : vaultPopup.go()
                Keys.onEscapePressed: vaultPopup.close()
            }
            TextField {
                id: pass2
                visible: vaultPopup.mode === "create"
                Layout.fillWidth: true
                implicitHeight: 30
                leftPadding: 12
                echoMode: TextInput.Password
                placeholderText: "Repeat password"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 13
                background: Rectangle { radius: 15; color: theme.well; border.width: 1; border.color: theme.accent }
                onAccepted: vaultPopup.go()
                Keys.onEscapePressed: vaultPopup.close()
            }
            Caption { Layout.fillWidth: true; visible: vaultPopup.error !== ""; color: theme.danger; text: vaultPopup.error }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: vaultPopup.close() }
                PillButton { text: vaultPopup.mode === "create" ? "Lock" : "Unlock"; checked: true; onClicked: vaultPopup.go() }
            }
        }
    }

    Popup {
        id: confirmDelete
        property var ids: []
        x: (root.width - width) / 2
        y: 64
        width: 320
        padding: 16
        background: Rectangle { radius: 16; color: theme.surface; border.color: theme.outline }
        contentItem: ColumnLayout {
            spacing: 10
            SectionLabel { text: "Delete permanently?" }
            Caption {
                Layout.fillWidth: true
                text: "Locked files skip Recently removed, so this can't be undone."
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: confirmDelete.close() }
                PillButton {
                    text: "Delete"
                    checked: true
                    onClicked: {
                        notch.removeItems(confirmDelete.ids)
                        confirmDelete.close()
                    }
                }
            }
        }
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
        Action {
            readonly property bool locked: notch.categories.some(c => c.id === categoryMenu.catId && c.locked)
            enabled: notch.vaultSupported
            text: !notch.vaultSupported ? "Lock with password… (not available here)"
                  : locked ? (notch.vaultUnlocked ? "Lock now" : "Unlock…") : "Lock with password…"
            onTriggered: {
                const id = categoryMenu.catId
                if (locked)
                    return notch.vaultUnlocked ? notch.lockVault() : root.askPassword("unlock")
                const lockIt = () => notch.setCategoryLocked(id, true)
                if (!notch.vaultExists)
                    root.askPassword("create", lockIt)
                else if (!notch.vaultUnlocked)
                    root.askPassword("unlock", lockIt)
                else
                    lockIt()
            }
        }
        Action {
            enabled: notch.vaultUnlocked && notch.categories.some(c => c.id === categoryMenu.catId && c.locked)
            text: "Remove lock"
            onTriggered: notch.setCategoryLocked(categoryMenu.catId, false)
        }
        Action {
            enabled: !notch.categories.some(c => c.id === categoryMenu.catId && c.locked) // remove the lock first
            text: "Delete category"
            onTriggered: notch.deleteCategory(categoryMenu.catId)
        }
        DarkSeparator {}
        Action { enabled: false; text: "Its files stay in Olyzano Notch" }
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

    FileDialog {
        id: addDialog
        title: "Add files"
        fileMode: FileDialog.OpenFiles
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        onAccepted: notch.addDrop(selectedFiles, "", notch.categoryFilter)
    }
    FolderDialog {
        id: saveDialog
        property var targetIds: []
        title: "Save to folder"
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        onAccepted: notch.saveTo(targetIds, selectedFolder)
    }
}
