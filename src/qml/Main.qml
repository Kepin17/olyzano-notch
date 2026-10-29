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
    // The panel is as wide as its page needs, and never narrower than the row of section buttons (Files, Music, Timer, Tasks,
    // Color, Sharing, Lyrics, Note, Add): with every button there, it grows (up to the window) instead of cutting them off.
    readonly property int panelWidth: Math.max(lyricsSplit ? 780 : page === "settings" ? 720 : page === "api" ? 720 : page === "tasks" ? 620 : openWidth,
                                               Math.min(width - 20, Math.max(actionBarLoader.item && actionBarLoader.item.visible ? Math.ceil(actionBarLoader.item.implicitWidth) + 44 : 0,
                                                                              Math.ceil(headerRow.implicitWidth) + 36)))

    property bool pinned: false
    // The panel's place in the window, for the share dialog: it is centred inside the panel and dims only the panel, and the
    // panel is at least as tall as the dialog needs (so the dialog never hangs out of it onto the desktop).
    readonly property rect panelRect: Qt.rect(island.x, island.y, island.width, island.height)
    readonly property real shareNeed: shareLoader.item !== null && shareLoader.item.visible ? shareLoader.item.height + 40 : 0
    readonly property string capsuleMode: activities.capsuleMode
    onCapsuleModeChanged: {
        pinned = capsuleMode === "open" // "Always open" is a pin that stays
        updateMask()
    }
    Component.onCompleted: {
        if (capsuleMode === "open") pinned = true
        loadWidgetPos()
        applyGeometry()
        desktop.setClickThrough(root, gaming.active)
        applyReserve()
    }

    // ---- Room at the top: other windows keep clear of the capsule (Settings → General) ----
    readonly property int barReservePx: topbar.enabled && topbar.behavior === "always" && topbar.reserve ? topbar.height : 0
    function applyReserve() { desktop.reserve(root, capsuleMode, activities.reserveSpace, activities.capsuleScale, barReservePx) }
    Connections { target: activities; function onSettingsChanged() { root.applyReserve() } }
    Connections { target: topbar; function onChanged() { root.applyReserve() } }

    // ---- Widget mode (beta): the notch as a floating card you drag anywhere ----
    // The window follows the card: a small one while it floats, the big one while its panel is open (the panel grows
    // from the card's spot); (widgetCx, widgetY) is the card's middle and top on this screen. `docked` is the island's
    // own place at the top centre (the stretch reminder).
    readonly property bool widgetMode: capsuleMode === "widget"
    property real widgetCx: 0
    property real widgetY: 14
    property rect winRect: Qt.rect(0, 0, 840, 620)
    property bool geoWidget: false // the window is placed by the card (not at the top centre)
    // "Pill" capsule shape (Settings -> Appearance): the capsule is a fully rounded pill instead of the notch with concave
    // shoulders. It sits exactly where the notch does (on the top edge, same size), so the strip kept free for it and the
    // windows below are the same for both. The card of Widget mode is already round and floating, so it keeps its own look.
    readonly property bool pillLook: activities.capsuleLook === "pill" && !geoWidget
    // How big the island is drawn: the capsule by its size setting, the open panel by "Interface size" (and never wider than the
    // screen when enlarged).
    // The spring damping, as the Bounce setting wants it (Settings -> Appearance): Off lifts every spring to no overshoot.
    function damp(base) { return activities.damp(base, activities.bounceFactor) }
    readonly property real uiK: activities.uiScale > 1 ? Math.min(activities.uiScale, Math.max(1, (width - 20) / Math.max(1, panelWidth))) : activities.uiScale
    readonly property real islandK: mode === "idle" || floating ? activities.capsuleScale : mode === "open" ? uiK : 1
    property bool dragging: false
    property string dragMask: ""
    readonly property bool docked: !widgetMode || mode === "prompt" || mode === "search" || mode === "hidden"
    readonly property bool floating: !docked && mode !== "open"
    readonly property string screenName: root.screen ? root.screen.name : ""
    function loadWidgetPos() {
        const size = desktop.screenSize(root)
        const p = activities.widgetPos(screenName, size.width, size.height)
        const q = desktop.snapWidget(p.x, p.y, 400, 38, root) // back on the screen if it changed size
        widgetCx = q.x === p.x ? p.x : Math.max(200, Math.min(size.width - 200, p.x))
        widgetY = Math.max(0, Math.min(size.height - 38, p.y))
    }
    function applyGeometry() {
        if (widgetMode && !docked) {
            // The card's window: elsewhere than Wayland it is the big one always. Switching from the small window to the big
            // one when the panel opens moves the window's top up (at the bottom of the screen) and the panel jumped there.
            winRect = desktop.placeWidget(root, dragging && desktop.layerShell ? 2 : (mode === "open" || !desktop.layerShell) ? 1 : 0, widgetCx, widgetY)
            geoWidget = true
        } else if (geoWidget) {
            geoWidget = false
            desktop.placeDocked(root)
        }
    }
    onWidgetModeChanged: { if (widgetMode) loadWidgetPos(); applyGeometry(); updateMask() }
    onDockedChanged: { applyGeometry(); updateMask() }
    // Wayland, while dragging: the window is the whole screen and the card moves inside it, nothing to place per step.
    // Windows and X11: the small window itself follows the pointer.
    property bool placingBatch: false // x and y are being set together: place the window once, when both are in
    onWidgetCxChanged: if (geoWidget && !placingBatch && !(dragging && desktop.layerShell)) applyGeometry()
    onWidgetYChanged: if (geoWidget && !placingBatch && !(dragging && desktop.layerShell)) applyGeometry()
    onDraggingChanged: { applyGeometry(); updateMask() }
    onWinRectChanged: updateMask()
    onGeoWidgetChanged: updateMask()
    onScreenChanged: { loadWidgetPos(); applyGeometry(); applyReserve() }
    Connections { target: activities; function onWidgetReset() { root.loadWidgetPos() } }
    // The same three steps for the card and for the open panel's title bar.
    property real dragStartCx: 0
    property real dragStartY: 0
    property point dragStartCursor
    function dragBegin() {
        dragStartCx = widgetCx
        dragStartY = widgetY
        dragStartCursor = desktop.cursorPos()
        dragPending = false
        dragging = true
    }
    // (lx, ly): how far the pointer is from where it was picked up, in the dragged item's own units (Wayland knows
    // nothing else); elsewhere the real pointer is read, so the window cannot run away from it.
    function dragMove(lx, ly, k) {
        if (desktop.layerShell) {
            dragStep(lx * k, ly * k)
            return
        }
        dragPending = true // the window is moved once per frame (dragFrame), not once per pointer event
    }
    property bool dragPending: false
    function dragFrame() {
        if (!dragPending)
            return
        dragPending = false
        const c = desktop.cursorPos()
        dragTo(c.x - dragStartCursor.x, c.y - dragStartCursor.y)
    }
    // Windows and X11: the real pointer is read, so the window cannot run away from it. Set from where the card was
    // picked up, x and y together, so the window is placed once per move (it used to go back to the start first).
    function dragTo(dx, dy) {
        const size = desktop.screenSize(root)
        const k = island.scale
        const w = island.targetWidth * k, h = island.targetHeight * k
        placingBatch = true
        widgetCx = Math.max(w / 2, Math.min(size.width - w / 2, dragStartCx + dx))
        widgetY = Math.max(0, Math.min(size.height - h, dragStartY + dy))
        placingBatch = false
        applyGeometry()
    }
    FrameAnimation { running: root.dragging && !desktop.layerShell; onTriggered: root.dragFrame() }
    function dragEnd() {
        dragFrame() // the last pointer move may not have had its frame yet
        dragging = false
        finishDrag()
    }
    function dragStep(dx, dy) { // the card moved by (dx, dy) screen pixels
        const size = desktop.screenSize(root)
        const k = island.scale
        const w = island.targetWidth * k, h = island.targetHeight * k
        widgetCx = Math.max(w / 2, Math.min(size.width - w / 2, widgetCx + dx))
        widgetY = Math.max(0, Math.min(size.height - h, widgetY + dy))
    }
    function finishDrag() {
        const k = island.scale
        const q = desktop.snapWidget(widgetCx, widgetY, island.targetWidth * k, island.targetHeight * k, root)
        widgetCx = q.x
        widgetY = q.y
        activities.setWidgetPos(screenName, widgetCx, widgetY)
    }
    property bool open: false
    property bool dwelled: false
    // Search from the notch (Shift + Space): the island turns into a search field with the matching apps under it.
    property bool searchOpen: false
    property bool searchFocused: false // the window has had the keys since the search opened
    function openSearch() {
        if (gaming.active)
            return
        open = false
        searchFocused = false
        searchOpen = true
        desktop.grabKeyboard(root, true) // exclusive for a moment, so the keys arrive without a click first
        searchGuard.restart()
        searchSettle.restart() // the window may already be "active": no change will tell us, so look again in a moment
    }
    function closeSearch() {
        searchGuard.stop()
        searchOpen = false
        desktop.grabKeyboard(root, false)
        island.forceActiveFocus()
    }
    function debugSearch(text) { openSearch(); if (searchPanelLoader.item) searchPanelLoader.item.setQuery(text) } // `notch --shot-search fire`
    // Once the window has the keys it lets the exclusivity go: a click on another window then takes them, and that closes the search.
    onActiveChanged: {
        dbg("window active=" + active + " searchOpen=" + searchOpen)
        if (!searchOpen)
            return
        if (active) {
            searchFocused = true
            desktop.grabKeyboard(root, false)
        } else if (searchFocused) {
            closeSearch()
        }
    }
    Timer {
        id: searchSettle
        interval: 350
        onTriggered: if (root.searchOpen && root.active) { root.searchFocused = true; desktop.grabKeyboard(root, false) }
    }
    Timer { id: searchGuard; interval: 4000; onTriggered: if (root.searchOpen && !root.searchFocused) root.closeSearch() } // never got the keys: do not hang there
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
    readonly property bool anyMenu: menu.visible || sendMenu.visible || assignMenu.visible || categoryMenu.visible
                                    || toolsMenu.visible || extractPopup.visible || (settingsLoader.item !== null && settingsLoader.item.overlayOpen)
                                    || quickLookCard.visible || vaultPopup.visible || confirmDelete.visible
                                    || (shareLoader.item !== null && shareLoader.item.visible) || incomingPopup.visible
                                    || sortMenu.visible || removedMenu.visible || (tasksPageLoader.item && tasksPageLoader.item.modalOpen)
                                    || (apiPageLoader.item && apiPageLoader.item.modalOpen)
                                    || removedChipMenu.visible
    readonly property bool dragOver: dragLive && (edgeDrop.containsDrag || islandDrop.containsDrag || chipDragOver)
    // Once it has started to open it stays open a moment, so a hover that blinks while the panel grows under a
    // still pointer (the input region changes with it) can't shut it again.
    Timer { id: holdTimer; interval: 450 }
    // `notch --debug-hover` prints what the pointer does at the top edge.
    readonly property bool debugHover: Qt.application.arguments.indexOf("--debug-hover") >= 0
    function dbg(what) { if (debugHover) console.log("hover", Date.now() % 100000, what, "mode=" + mode, "open=" + open, "dwelled=" + dwelled) }
    // With --debug-hover, frames that take longer than ~1.5 refreshes while the island moves.
    property double lastFrame: 0
    onFrameSwapped: {
        if (!debugHover)
            return
        const now = Date.now()
        if (lastFrame > 0 && now - lastFrame > 24 && (Math.abs(island.width - island.targetWidth) > 0.5 || Math.abs(island.height - island.targetHeight) > 0.5))
            dbg("slow frame " + (now - lastFrame) + " ms")
        if (dragging) // the time between frames while the card is being moved
            dbg("dragframe " + (lastFrame > 0 ? now - lastFrame : 0))
        lastFrame = now
    }
    readonly property bool engaged: !gaming.active && (pinned || dwelled || holdTimer.running || (islandHover.hovered && mode !== "idle" && mode !== "prompt" && mode !== "search" && !floating)
                                    || dragOver || anyMenu || (dragging && mode === "open")
                                    || (open && page === "api") // the API tester stays open while you work in it; back or another page lets it close
                                    || saveDialog.visible || addDialog.visible)
    // hidden: only the top-edge strip is live. peek: copy in progress. open: full panel.
    // toast: a short system alert (charger, Bluetooth). Shown even while hidden, like Dynamic Island.
    property var toast: null // {kind, title, detail, icon, art, level, ms}
    // One place for what each kind of toast looks like: its colour and its glyph.
    function toastColorFor(kind, t) {
        switch (kind) {
        case "charging": case "battery": case "call": case "phone": case "wellbeing": return theme.success
        case "lowbattery": return theme.danger
        case "bluetooth": case "update": case "task": case "prayer": return accent
        case "timer": return theme.accent
        case "media": return theme.media
        case "file": case "desktop": return theme.info
        case "color": return t && t.detail ? t.detail : theme.textDim
        }
        return theme.textDim
    }
    function toastGlyphFor(kind) {
        switch (kind) {
        case "bluetooth": return "bluetooth"
        case "timer": return "timer"
        case "wellbeing": return "heart"
        case "call": case "phone": case "lowbattery": return "phone"
        case "task": return "check"
        case "update": return "download"
        case "media": return "music"
        case "file": return "folder"
        }
        return "notifications"
    }

    // ---- The toast on screen, the ones waiting, and how they change ----
    property var toastQueue: []          // waiting toasts (at most 5); a low battery or a missed call goes first
    property var shownToast: null        // what the card shows: it stays until the next one comes, so it does not blank while the island closes
    property bool toastShowing: false
    property real toastRoll: 0           // 0 at rest; 0 -> 1 the old toast going down; -1 -> 0 the new one coming from above
    property real toastLeft: 1           // 1 -> 0 over the toast's time: the line along the bottom of the card
    function startToast(t) {
        toast = t
        toastTimer.interval = t.ms
        toastTimer.restart()
        toastClock.stop()
        toastClock.duration = t.ms
        toastClock.start()
    }
    function pushToast(t, ms) {
        t.ms = ms || 3600
        if (gaming.active)
            return
        if (toast && toast.kind === t.kind && toast.title === t.title) { // the same news again: it replaces the one on screen
            startToast(t)
            return
        }
        if (toast) {
            const urgent = t.kind === "lowbattery" || t.kind === "call"
            const q = toastQueue.slice()
            const same = q.findIndex(x => x.kind === t.kind && x.title === t.title)
            if (same >= 0)
                q[same] = t
            else if (urgent)
                q.unshift(t)
            else
                q.push(t)
            if (q.length > 5)
                q.splice(urgent ? q.length - 1 : 0, 1) // the oldest waiting one goes (or the last, for an urgent newcomer)
            toastQueue = q
            return
        }
        startToast(t)
    }
    function nextToast() {
        if (toastQueue.length > 0) {
            const q = toastQueue.slice()
            const n = q.shift()
            toastQueue = q
            startToast(n)
        } else {
            toast = null
        }
    }
    NumberAnimation { id: toastClock; target: root; property: "toastLeft"; from: 1; to: 0; duration: 3200 }
    onToastChanged: {
        if (!toast) {
            toastShowing = false
            return
        }
        if (toastShowing && shownToast && shownToast.kind === toast.kind && shownToast.title === toast.title) {
            shownToast = toast // the same news again: no roll
        } else if (toastShowing) {
            if (activities.animations) {
                toastChange.restart()
            } else {
                shownToast = toast
                toastRoll = 0
            }
        } else {
            shownToast = toast
            toastShowing = true
            if (activities.animations)
                toastIn.restart()
            else
                toastRoll = 0
        }
    }
    NumberAnimation { id: toastIn; target: root; property: "toastRoll"; from: -1; to: 0; duration: 320; easing.type: Easing.OutCubic }
    SequentialAnimation {
        id: toastChange
        NumberAnimation { target: root; property: "toastRoll"; to: 1; duration: 130; easing.type: Easing.InQuad }
        ScriptAction { script: { root.shownToast = root.toast; root.toastRoll = -1 } }
        NumberAnimation { target: root; property: "toastRoll"; to: 0; duration: 260; easing.type: Easing.OutCubic }
    }
    // prompt: the stretch reminder asking. idle: the small clock pill when nothing else is going on.
    readonly property string mode: wellbeing.state === "reminding" || doomscroll.reminding ? "prompt" : searchOpen ? "search" : open ? "open" : toast && !gaming.active ? "toast"
                                 : live ? "peek" : idleShown ? "idle" : "hidden"

    // ---- Doomscrolling: quiet in a game or on a call ----
    Binding { target: doomscroll; property: "quiet"; value: gaming.active || root.call !== null }

    // ---- Tasks: a heads-up 15 minutes before a deadline and a note when it comes ----
    Connections {
        target: tasks
        function onTaskWarning(title) { root.showToast("task", title, "Due in 15 minutes", "check", 6000) }
        function onTaskDue(title) { root.showToast("task", title, "Deadline now", "check", 8000) }
    }

    // ---- Prayer times: a note when one comes, and a heads-up before ----
    Connections {
        target: prayer
        function onPrayerTime(name, time) { root.showToast("prayer", name, "It is time · " + time, "notifications", 9000); sounds.play("notify") }
        function onPrayerSoon(name, minutes) { root.showToast("prayer", name, "In " + minutes + " minute" + (minutes === 1 ? "" : "s"), "notifications", 6000) }
        function onPlaceNote(text) { root.showToast("prayer", "Prayer times", text, "notifications", 7000) }
    }

    // ---- A new version: once, and not in the middle of a game, a call or a timer ----
    Binding { target: updates; property: "quiet"; value: gaming.active || root.call !== null || activities.timerKind !== "" }
    Connections {
        target: updates
        function onUpdateFound(version) {
            root.showToast("update", "Notch " + version + " is available", "Settings → About has the details", "download", 9000)
        }
    }

    // ---- Stretch reminder ----
    // A film, a call or a Pomodoro holds the reminder back (each can be switched off in Settings).
    Binding {
        target: wellbeing
        property: "busy"
        value: (wellbeing.pauseForMedia && media.playing)
               || (wellbeing.pauseForCalls && (root.call !== null || activities.micApps.length > 0 || activities.cameraApps.length > 0))
               || (wellbeing.pauseForPomodoro && activities.timerKind === "pomodoro")
               || gaming.active // never over a game
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
    readonly property bool idleShown: capsuleMode === "capsule" || capsuleMode === "widget" || (activities.idleClock && !idleCovered)
    readonly property bool taskShown: activities.taskInCapsule && tasks.hasNext
    readonly property bool prayerShown: prayer.enabled && prayer.ready && prayer.inCapsule && prayer.hasNext
    readonly property bool infoShown: taskShown || prayerShown
    // With a task and a prayer both to show, the capsule says one, then the other, every 8 seconds.
    property int infoTurn: 0
    Timer { interval: 8000; repeat: true; running: root.taskShown && root.prayerShown && root.mode === "idle"; onTriggered: root.infoTurn++ }
    readonly property bool showPrayer: prayerShown && (!taskShown || infoTurn % 2 === 1)
    property date idleNow: new Date()
    Timer {
        interval: 5000
        repeat: true
        running: root.mode === "idle"
        triggeredOnStart: true
        onTriggered: root.idleNow = new Date()
    }

    // ---- Now playing in the capsule ----
    // The song takes the capsule over while it plays, and for 5 seconds after it stops or is paused (a change of song
    // or a short pause must not flash the clock); if nothing plays again by then, the idle capsule returns.
    property bool musicStale: false
    property bool musicGrace: false // the player went away a moment ago: the song stays a few seconds more
    Timer { id: graceTimer; interval: 5000; onTriggered: root.musicGrace = false }
    readonly property bool musicShown: (media.active && !musicStale) || musicGrace
    // The capsule has been showing music for a moment: from here a new song is a roll in the capsule, not a toast.
    property bool capSongSeen: false
    Timer { id: seenTimer; interval: 1500; onTriggered: root.capSongSeen = true }
    onMusicShownChanged: { if (musicShown) seenTimer.restart(); else { seenTimer.stop(); capSongSeen = false } }
    property real capPos: 0 // ms, read once a second while the capsule shows the song
    // What the capsule says: line 1 the sung line (else the title), line 2 the artist and album (else the song). A line
    // longer than the room glides sideways (MarqueeText); the capsule only widens for a sung line, up to nearly the window.
    readonly property bool capLyric: lyrics.enabled && lyrics.currentText !== "" // only a sung line widens the capsule; the song name stays at 400
    readonly property string capTitle: capLyric ? lyrics.currentText : media.title !== "" ? media.title : media.identity
    readonly property string capSub: capLyric ? (media.title !== "" ? media.title + (media.artist !== "" ? " · " + media.artist : "") : "")
        : [media.artist, media.album].filter(t => t !== "").join(" · ") || (media.title !== "" ? media.identity : "")
    readonly property string capTime: clock(capPos) + (media.lengthMs > 0 ? " / " + clock(media.lengthMs) : "")
    // which app is playing: what the second line says while the pointer rests on the capsule
    readonly property string capHover: media.identity
    TextMetrics { id: capLineSize; font.pixelSize: 13; font.weight: Font.DemiBold; text: root.capTitle }
    // margins + cover + wave + gaps + the microphone, camera, Discord and share icons, and a little spare
    TextMetrics { id: capTimeSize; font.pixelSize: 11; font.features: { "tnum": 1 }; text: root.capTime }
    readonly property real capOverhead: 2 * 24 + 30 + 26 + capTimeSize.advanceWidth + 4 * 10
                                        + (activities.micApps.length > 0 ? 25 : 0) + (activities.cameraApps.length > 0 ? 25 : 0) + (activities.discordState !== "off" ? 24 : 0) + (sharingNow ? 24 : 0) + 16
    readonly property real capMaxWidth: ((widgetMode ? 720 : root.width) - 40) / activities.capsuleScale
    readonly property real capWidth: !capLyric ? 430 : Math.min(capMaxWidth, Math.max(430, capLineSize.advanceWidth + capOverhead))
    // the colour of the cover, for the wave and the progress line
    ArtColor { id: capColor; source: media.artUrl; fallback: theme.media }
    // A new song: the old one leaves downward and the new one comes in from above (the cover and both lines together).
    // What is shown is swapped at the middle of the roll; a sung line changing is not a new song and just fades.
    property real capRoll: 0 // 0 at rest; 0 -> 1 the old song going down; -1 -> 0 the new one arriving
    property bool capSwitching: false
    property string shownTitle: capTitle
    property string shownSub: capSub
    property url shownArt: media.artUrl
    onCapTitleChanged: if (!capSwitching && media.active) shownTitle = capTitle // when the player goes, the last song stays
    onCapSubChanged: if (!capSwitching && media.active) shownSub = capSub
    readonly property url artNow: media.artUrl
    onArtNowChanged: if (!capSwitching && media.active) shownArt = artNow
    // The song the capsule shows. A plain value on purpose: written as a binding it was already the new key when the handler below
    // compared it, so the roll never started. Title and artist only: the cover arrives a moment later and just swaps in.
    property string capLastKey: ""
    SequentialAnimation {
        id: capChange
        ScriptAction { script: root.capSwitching = true }
        NumberAnimation { target: root; property: "capRoll"; to: 1; duration: 150; easing.type: Easing.InQuad }
        ScriptAction { script: { root.shownTitle = root.capTitle; root.shownSub = root.capSub; root.shownArt = root.artNow; root.capRoll = -1 } }
        NumberAnimation { target: root; property: "capRoll"; to: 0; duration: 300; easing.type: Easing.OutCubic }
        ScriptAction { script: { root.capSwitching = false; root.shownArt = root.artNow } } // a cover that came during the roll
    }
    Timer { id: pauseTimer; interval: 5000; onTriggered: root.musicStale = true }
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
            if (media.active) {
                root.musicGrace = false
                graceTimer.stop()
            } else if (!root.musicGrace && root.mode === "idle") {
                root.musicGrace = true // the song stays on the capsule for a moment
                graceTimer.restart()
            }
            if (media.playing || !media.active) {
                root.musicStale = false
                pauseTimer.stop()
            } else if (!pauseTimer.running && !root.musicStale) {
                pauseTimer.restart()
            }
            root.capPos = media.positionMs()
            const key = media.title + "|" + media.artist
            if (media.active && key !== root.capLastKey) {
                const firstSong = root.capLastKey === ""
                root.capLastKey = key
                if (!firstSong && activities.animations && root.mode === "idle" && root.musicShown)
                    capChange.restart()
                else {
                    root.shownTitle = root.capTitle; root.shownSub = root.capSub; root.shownArt = root.artNow
                }
            }
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
    readonly property int shareSessions: share.activeCount + nearby.activeCount
    readonly property bool sharingNow: shareSessions > 0
    // {kind, title, detail, progress (0-1, -1 none), color}
    readonly property var live: {
        if (assistant.state !== "idle") // a voice command: listening, thinking, or what it did
            return { kind: "assistant", progress: -1, detail: "", color: assistant.state === "error" ? theme.warn : theme.accent,
                     title: assistant.state === "listening" ? "Say: “" + assistant.hint + "”" : assistant.state === "thinking" ? (assistant.heard !== "" ? assistant.heard : "Thinking…") : assistant.result }
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
        // Something is being shared by QR or to a computer: the way back to it once the panel is closed. (Under a song the
        // capsule keeps its place and shows a small mark instead.)
        if (sharingNow && !musicShown)
            return { kind: "share", title: "Sharing " + shareSessions + (shareSessions === 1 ? " session" : " sessions"), detail: "tap to open", progress: -1, color: theme.accent }
        // Scrolling a short-form feed for a while (under a song the capsule keeps its place: the warning still comes)
        if (doomscroll.site !== "" && doomscroll.minutes >= 5 && !musicShown)
            return { kind: "doomscroll", title: "Scrolling " + doomscroll.siteName, detail: doomscroll.minutes + " min", progress: -1, color: theme.warn }
        if (notch.copying > 0)
            return { kind: "copy", title: "Copying " + notch.copying + (notch.copying === 1 ? " file" : " files"),
                     detail: String(notch.count), progress: -1, color: theme.accent }
        return null
    }

    // Settings and the share window are built the first time they are needed, not at start-up (memory).
    function settingsPage() { settingsLoader.active = true; return settingsLoader.item }
    function sharePopup() { shareLoader.active = true; return shareLoader.item }
    function debugSettings(section) { // "general" or "general:300" (scrolled down by that much)
        const parts = section.split(":")
        settingsPage().category = parts[0]
        if (parts.length > 1) settingsPage().scrollTo(Number(parts[1]))
    }
    function _unusedDebugSettings(section) { settingsPage().category = section } // `notch --shot-settings file.png <section>`
    // `notch --shot-page file.png share-ready|share-pin|share-many|share-list`: a note is shared on "This WiFi" and the dialog shown
    function debugShareReady(kind) {
        const count = kind === "many" || kind === "list" ? 3 : 1
        for (let i = 0; i < count; ++i)
            notch.addTextNote("A note to share through the QR code, number " + (i + 1) + ".\n")
        Qt.callLater(() => {
            const pop = sharePopup()
            const ids = notch.visibleIds()
            pop.openFor(ids.slice(0, count))
            pop.debugBegin(kind === "pin" ? "482193" : "")
            if (kind === "list") {
                pop.openFor(ids.slice(0, 1)) // a second share
                pop.debugBegin("")
                pop.view = "sessions"
            }
        })
    }
    function debugShare() { sharePopup().openFor([]) } // `notch --shot-page file.png share`: the share window, for a picture
    function showShares() {
        pinned = true // opens the panel and keeps it while the shares are looked at
        page = "shelf"
        Qt.callLater(() => sharePopup().openSessions())
    }

    // `notch --shot-page file.png toast-<kind>`: one toast of that kind (or "queue": three in a row), for a picture.
    function debugToast(kind, art) {
        const demo = {
            charging: { kind: "charging", title: "Charger connected", detail: "82% · charging", icon: "", level: 82 },
            lowbattery: { kind: "lowbattery", title: "Battery low", detail: "12% left", icon: "battery-low", level: 12 },
            bluetooth: { kind: "bluetooth", title: "WH-1000XM5", detail: "Connected", icon: "audio-headphones", level: 74 },
            media: { kind: "media", title: "Lebih Beruntung Sendiri", detail: "Pasha Dewangga", icon: "", level: -1, art: art },
            file: { kind: "file", title: "Added to the shelf", detail: "report.pdf", icon: "application-pdf", level: -1, art: "" },
            color: { kind: "color", title: "Color copied", detail: "#3FA7F5", icon: "color", level: -1 },
            timer: { kind: "timer", title: "Tea is ready", detail: "Timer finished", icon: "", level: -1 },
            call: { kind: "call", title: "Missed call", detail: "Ana", icon: "", level: -1 },
            task: { kind: "task", title: "Send the report", detail: "Due in 15 minutes", icon: "", level: -1 },
            prayer: { kind: "prayer", title: "Dhuhr · Zuhur", detail: "It is time · 11:39", icon: "", level: -1 },
            update: { kind: "update", title: "Notch 0.12.4 is available", detail: "Settings → About has the details", icon: "", level: -1 },
            wellbeing: { kind: "wellbeing", title: "Rest your eyes", detail: "Look at something far away for 20 seconds", icon: "", level: -1 },
            desktop: { kind: "desktop", title: "Notification from Thunderbird", detail: "Weekly planning", icon: "", level: -1 }
        }
        const list = kind === "queue" ? ["task", "charging", "lowbattery", "prayer"] : [kind]
        for (const k of list)
            if (demo[k])
                pushToast(Object.assign({ art: "" }, demo[k]), 30000)
    }

    function showToast(kind, title, detail, icon, ms) {
        if (root.open) {
            root.infoText = title + (detail ? " · " + detail : "")
            infoTimer.restart()
            return
        }
        root.pushToast({ kind: kind, title: title, detail: detail, icon: icon, art: "", level: -1 }, ms || 3600)
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

    Timer { id: toastTimer; interval: 3200; onTriggered: root.nextToast() }
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
            root.pushToast({ kind: icon === "color" ? "color" : "file", title: title, detail: detail, icon: icon, art: art, level: -1 }, 3200)
        }
    }
    Connections {
        target: media
        function onTrackChanged(title, artist, art) {
            if (!notch.alerts || root.open)
                return
            if (root.capSongSeen && root.mode === "idle" && root.capsuleMode !== "hidden")
                return // the capsule has been showing songs: it rolls to the new one itself, no second notice
            // (the first song after a silence is announced by the toast, and the capsule takes over from it)
            root.pushToast({ kind: "media", title: title, detail: artist, icon: "", art: art, level: -1 }, 3200)
        }
    }
    Connections {
        target: systemEvents
        function onAlert(kind, title, detail, icon, level) {
            if (!notch.alerts)
                return
            root.pushToast({ kind: kind, title: title, detail: detail, icon: icon, art: "", level: level }, kind === "lowbattery" ? 5000 : 3200) // warnings stay a little longer
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
            else if (mode === "toast" && toast && toast.kind === "task")
                page = "tasks" // pointing at a task reminder opens the list
            else if (mode === "peek" && live && live.kind === "timer")
                page = "timer" // and the running timer's pill opens the timer
            open = true
        } else {
            closeTimer.restart()
        }
    }
    onOpenChanged: {
        dbg("open=" + open)
        applyGeometry()
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
    onAnyMenuChanged: { updateMask(); if (anyMenu) desktop.raiseOnTop(root) } // a popup must not open under a peer window
    Timer { id: closeTimer; interval: 110; onTriggered: root.open = false }
    // Dwell before revealing, so flinging the mouse up to a tab doesn't pop the panel.
    Timer { id: dwellTimer; interval: 160; onTriggered: root.dwelled = true }

    // Everything outside the live area is click-through. Opening changes the mode and both target sizes in one go: the
    // mask is set once afterwards (on Windows each call is a synchronous SetWindowRgn that can hold up the first frame).
    function updateMask() { Qt.callLater(applyMask) }
    function applyMask() {
        // Gaming mode over a full-screen app: the notch stays on screen but nothing can be reached. On Wayland an empty input
        // region does it (one corner pixel stays, as an empty mask means none). Elsewhere (Windows) a mask also clips what is
        // drawn, so the mask stays the notch's own shape and the window is made click-through instead (setClickThrough).
        if (gaming.active && desktop.layerShell)
            desktop.setInputRegion(root, Qt.rect(0, height - 1, 1, 1))
        else if (anyMenu)
            desktop.setInputRegion(root, Qt.rect(0, 0, width, height))
        else if (geoWidget) { // the card (or its panel) in its own window
            if (dragging) { // once: a mask change at every step makes a Windows window flash
                if (dragMask !== width + "x" + height) {
                    dragMask = width + "x" + height
                    desktop.setInputRegion(root, Qt.rect(0, 0, width, height))
                }
            } else {
                dragMask = ""
                // Where the card is and where it is going (a panel opening at the bottom grows upward, so its top moves): the
                // mask covers both, with a margin, and stays inside the window.
                const slack = maskSettle.running ? 24 : 6
                const k = root.islandK
                const w = Math.min(width, Math.max(island.width, island.targetWidth) * k + 2 * slack)
                const goalTop = Math.max(0, Math.min(height - island.targetHeight * k - 2, widgetY - winRect.y))
                const top = Math.max(0, Math.min(island.y, goalTop) - slack)
                const bottom = Math.min(height, Math.max(island.y + island.height * k, goalTop + island.targetHeight * k) + slack)
                const mid = Math.max(w / 2, Math.min(width - w / 2, widgetCx - winRect.x))
                desktop.setInputRegion(root, Qt.rect(mid - w / 2, top, w, bottom - top))
            }
        } else if (mode === "hidden" || (mode === "idle" && capsuleMode !== "capsule")) // the idle clock is see-through for clicks; the top edge opens Notch (the capsule itself can be pointed at)
            desktop.setInputRegion(root, Qt.rect(edge.x, 0, edge.width, edge.height))
        else {
            // Covers where the island is going, not where it is this frame: a native mask change per animation
            // frame is what made opening lag on Windows. A little slack while it moves, so the spring's overshoot
            // is not clipped; maskSettle tightens it once the island has come to rest.
            const slack = maskSettle.running ? 24 : 0
            const k = root.mode === "idle" || root.mode === "open" ? root.islandK : 1
            const w = Math.max(island.width, island.targetWidth) * k + 2 * slack
            const h = Math.max(island.height, island.targetHeight, edge.height) * k + slack
            desktop.setInputRegion(root, Qt.rect((width - w) / 2, 0, w, h))
        }
    }
    // Glass: the desktop blurs what is behind the island (KWin; nothing where it cannot) while the island is see-through.
    property bool blurOn: false // the desktop has been asked to blur behind this window
    function updateBlur() {
        if (!activities.glass || !desktop.canBlur || island.height < 4 || mode === "hidden") {
            if (blurOn) { // only undo what was asked: with Glass off the window is never touched
                blurOn = false
                desktop.setBlurRegion(root, Qt.rect(0, 0, 0, 0), 0)
            }
            return
        }
        blurOn = true
        // The island as it is now (not where it is going), so the blur never shows past its edge; the edge look's concave
        // shoulders are left out.
        const k = root.islandK
        const inset = island.roundShape ? 0 : island.shoulder * k
        const w = island.width * k - 2 * inset, h = island.height * k
        desktop.setBlurRegion(root, Qt.rect(island.x + (island.width * (1 - k)) / 2 + inset, island.y, w, h), island.roundShape ? island.roundRadius : Math.min(24, h / 2))
    }
    // While the island springs to its size the blur follows it, a few times a second at most.
    Timer { id: blurFollow; interval: 40; onTriggered: root.updateBlur() }
    // ...and once more when it has stopped, so the blur is never left at an in-between size (a bigger rectangle than the island,
    // seen after changing page) when the last change fell while blurFollow was still counting.
    Timer { id: blurSettle; interval: 120; onTriggered: root.updateBlur() }
    Connections {
        target: island
        function onWidthChanged() { if (activities.glass) { if (!blurFollow.running) blurFollow.start(); blurSettle.restart() } }
        function onHeightChanged() { if (activities.glass) { if (!blurFollow.running) blurFollow.start(); blurSettle.restart() } }
    }
    Connections { target: activities; function onSettingsChanged() { root.updateBlur() } }
    function maskMoving() {
        maskSettle.restart()
        updateMask()
    }
    Timer { id: maskSettle; interval: 350; onTriggered: { root.updateMask(); root.updateBlur() } }
    onVisibleChanged: { updateMask(); applyReserve() }
    onModeChanged: { applyGeometry(); updateMask(); updateBlur(); if (mode === "idle" || mode === "hidden") trimTimer.restart() }
    // A little after the panel has closed, freed memory goes back to the system.
    Timer { id: trimTimer; interval: 15000; onTriggered: memtune.trim() }
    // Settings and the share dialog are built when first wanted; a minute or two after they are closed they go again (they are
    // some 20 MB), and are built anew the next time.
    property string settingsCategory: "general" // which Settings section was open, kept while the page is unloaded
    Timer {
        interval: 60000
        repeat: true
        running: settingsLoader.active || shareLoader.active
        onTriggered: {
            if (settingsLoader.active && root.page !== "settings" && !(settingsLoader.item && settingsLoader.item.overlayOpen))
                settingsLoader.active = false
            if (shareLoader.active && !(shareLoader.item && shareLoader.item.visible))
                shareLoader.active = false
            trimTimer.restart()
        }
    }
    Connections {
        target: gaming
        function onChanged() {
            desktop.setClickThrough(root, gaming.active)
            root.updateMask()
            if (gaming.active) { // a full-screen app arrived: let go of the pointer and close
                root.toastQueue = []
                root.toast = null
                toastTimer.stop()
                root.dwelled = false
                root.open = false
            }
        }
    }

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
        sharePopup().openFor(ids)
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
        if (typeof target === "string" && target.indexOf("settings") === 0) {
            page = "settings"
            pinned = true
            const parts = target.split(":")
            if (parts.length > 1)
                settingsPage().category = parts[1]
            return
        }
        pinned = capsuleMode === "open" || !pinned
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

    onPageChanged: {
        if (page === "note") {
            notePageLoader.active = true
            if (notePageLoader.item)
                notePageLoader.item.load()
        } else if (page === "settings")
            settingsLoader.active = true
    }
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
        // Widget mode: inside the window by its drawn (scaled) size with a 2 px margin (WidgetModel::WinMargin), so the edge line is
        // never cut off at the bottom or the sides, and a growing panel at the bottom grows upward from the bottom edge.
        x: root.geoWidget ? Math.max(width * scale / 2, Math.min(root.width - width * scale / 2 - 2, root.widgetCx - root.winRect.x)) - width / 2 : (root.width - width) / 2
        y: root.geoWidget ? Math.max(0, Math.min(root.height - height * scale - 2, root.widgetY - root.winRect.y)) : 0
        // Where the island is heading; width and height follow it with the springs below.
        readonly property real targetWidth: root.mode === "open" ? root.panelWidth : root.mode === "search" ? 560 : root.mode === "prompt" ? 440 : root.mode === "toast" ? 400
             : root.mode === "peek" ? (root.live && root.live.kind === "share" ? 400 : root.live && root.live.kind !== "copy" ? 320 : 230)
             : root.mode === "idle" ? (root.musicShown ? root.capWidth : root.infoShown ? 390 : systemEvents.batteryLevel >= 0 ? 230 : 180) : 150
        readonly property real targetHeight: root.mode === "open" ? Math.max(panel.implicitHeight + (root.dragOver ? 8 : 0), root.shareNeed)
              : root.mode === "search" ? (searchPanelLoader.item ? searchPanelLoader.item.wantedHeight : 0) : root.mode === "prompt" ? 92 : root.mode === "toast" ? 56 : root.mode === "peek" ? 36 : root.mode === "idle" ? 48 : 0
        width: targetWidth
        height: targetHeight
        focus: true
        // The capsule's size is the person's choice (Settings); everything in it scales together, glued to the top edge.
        scale: root.islandK
        transformOrigin: Item.Top
        Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        readonly property real shoulder: Math.min(10, Math.max(0, height) / 3)
        readonly property real corner: Math.min(24 * theme.cornerScale, Math.max(0, height - shoulder))
        readonly property bool closing: root.mode === "hidden"
        // How far the background is from its target size. The open panel's content appears only once the
        // background is nearly full (revealed), so it is never seen outside it; moving to another page of the
        // open panel resizes it again without hiding the content.
        readonly property real progress: Math.max(0, Math.min(1, Math.min(targetWidth > 0 ? width / targetWidth : 1,
                                                                          targetHeight > 0 ? height / targetHeight : 1)))
        property bool revealed: false
        Component.onCompleted: { revealed = root.mode === "open"; curMode = root.mode }
        readonly property real revealAt: 0.7 // share of the target size at which the content appears
        onProgressChanged: if (root.mode === "open" && progress >= revealAt) revealed = true
        onRevealedChanged: root.dbg("revealed=" + revealed) // --debug-hover: pointer arrives -> content shown, for timing the opening
        Connections {
            target: root
            function onModeChanged() {
                island.revealed = root.mode === "open" && island.progress >= island.revealAt
                island.prevMode = island.curMode // the layer that is going away, and where the island was when it began
                island.curMode = root.mode
                island.fromW = island.width
                island.fromH = island.height
                island.transitioning = true
                transitionEnd.restart()
            }
        }
        // Content and background move together: while the island changes from one mode to another, the layer that is leaving fades out as
        // the background starts to move and the one that arrives fades in as it gets there, both following the background's own size
        // (not a timer), so no item is ever seen before or after the shape it belongs to.
        property string curMode: ""
        property string prevMode: ""
        property real fromW: 0
        property real fromH: 0
        property bool transitioning: false
        Timer { id: transitionEnd; interval: 900; onTriggered: island.transitioning = false }
        // How much of the way from the old size to the new one the background has gone, 0..1.
        readonly property real covered: {
            const dh = targetHeight - fromH, dw = targetWidth - fromW
            const ch = Math.abs(dh) < 2 ? 1 : (height - fromH) / dh
            const cw = Math.abs(dw) < 2 ? 1 : (width - fromW) / dw
            return Math.max(0, Math.min(1, Math.min(ch, cw))) // both axes there, so content never shows outside the shape
        }
        function fade(mode) {
            if (!transitioning)
                return root.mode === mode ? 1 : 0
            if (root.mode === mode)
                return Math.max(0, Math.min(1, (covered - 0.4) / 0.4))
            return prevMode === mode ? Math.max(0, 1 - covered * 2.4) : 0
        }

        // Height is stiffer than width: the drop falls first, then spreads and settles. The same on every platform (an extra
        // Windows-only damping did not make it faster, only less even); the floating pill settles without a bounce. Timed with
        // --debug-hover on Windows, pointer arriving -> content shown: 424 ms with width spring 3.4 and reveal at 85 %, 324 ms
        // with 4.8 and 70 % (160 ms of it is the dwell on the hidden edge). epsilon 0.4 ends the tail of the animation earlier
        // (invisible at that size).
        Behavior on height { enabled: activities.animations; SpringAnimation { spring: island.closing ? 7 : 5.5; damping: root.damp(island.closing ? 0.9 : root.mode === "idle" ? 0.8 : root.pillLook ? 0.6 : 0.32); epsilon: 0.4 } }
        Behavior on width { enabled: activities.animations; SpringAnimation { spring: island.closing ? 6 : 4.8; damping: root.damp(island.closing ? 0.9 : root.mode === "idle" ? 0.8 : root.pillLook ? 0.6 : 0.3); epsilon: 0.4 } }
        onTargetWidthChanged: root.maskMoving()
        onTargetHeightChanged: root.maskMoving()
        Connections { target: activities; function onSettingsChanged() { root.maskMoving() } }

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
        // The pill and the Widget card are plain rounded shapes, with no shadow; the notch is the silhouette below.
        readonly property bool roundShape: root.geoWidget || root.pillLook
        readonly property real roundRadius: root.pillLook ? Math.min(28 * theme.cornerScale, Math.max(0, height) / 2) : Math.min(20 * theme.cornerScale, Math.max(0, height) / 2)
        // The edge of the capsule and the card: the system accent (Windows / KDE) if chosen in Settings, else the pill's soft
        // outline or the look's own outline.
        readonly property color edgeColor: root.dragOver ? root.accent
            : activities.borderMatchesSystem ? theme.systemAccent
            : root.pillLook ? (theme.dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0, 0, 0, 0.09)) : theme.outline
        Rectangle {
            anchors.fill: parent
            visible: island.roundShape && island.height > 0.5
            radius: island.roundRadius
            color: root.pillLook && theme.dark && !theme.glass ? "#0a0a0a" : theme.island
            border.width: 1
            border.color: island.edgeColor
        }
        GlassLayer {
            anchors.fill: parent
            visible: theme.glass && island.roundShape && island.height > 0.5
            radius: island.roundRadius
        }
        Shape {
            anchors.fill: parent
            visible: island.height > 0.5 && !island.roundShape
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: sp
                readonly property real s: island.shoulder
                readonly property real r: island.corner
                readonly property real w: island.width
                readonly property real h: Math.max(0, island.height)
                fillColor: theme.island
                strokeColor: island.edgeColor
                strokeWidth: 1
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
            ShapePath { // Glass: the same silhouette with the sheen falling from the top
                id: sheenPath
                fillGradient: LinearGradient {
                    x1: 0; y1: 0; x2: 0; y2: Math.max(1, sp.h)
                    GradientStop { position: 0; color: Qt.rgba(1, 1, 1, theme.glassSheen) }
                    GradientStop { position: 0.55; color: Qt.rgba(1, 1, 1, 0) }
                }
                strokeColor: "transparent"
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

        // Toast: a two-line card. A chip in the colour of the kind (the battery, a device, a cover or a swatch where there is
        // one), the title and a dimmer line under it, a thin line along the bottom that runs out with the toast's time.
        Item {
            id: toastRow
            anchors.fill: parent
            opacity: island.fade("toast")
            visible: opacity > 0
            readonly property var t: root.shownToast || ({ kind: "", title: "", detail: "", icon: "", art: "", level: -1 })
            readonly property bool isPower: t.kind === "charging" || t.kind === "battery"
                                            || (t.kind === "lowbattery" && t.icon === "battery-low")
            readonly property bool isCover: t.kind === "media" || t.kind === "file"
            readonly property color tint: root.toastColorFor(t.kind, t)
            readonly property bool deviceIcon: t.kind === "bluetooth" && t.icon !== ""

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: island.shoulder + 14
                anchors.rightMargin: island.shoulder + 16
                anchors.topMargin: 2
                anchors.bottomMargin: 8
                spacing: 12
                opacity: 1 - Math.abs(root.toastRoll)
                transform: Translate { y: root.toastRoll * 12 }

                Item { // the chip
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    Layout.alignment: Qt.AlignVCenter
                    Rectangle {
                        anchors.fill: parent
                        radius: 11
                        visible: !toastRow.isCover && toastRow.t.kind !== "color"
                        color: Qt.alpha(toastRow.tint, 0.18)
                    }
                    // The drawn battery: it fills to the level, a charging one glows
                Item {
                    visible: toastRow.isPower
                    anchors.centerIn: parent
                    width: 30
                    height: 14
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
                            Behavior on width { enabled: activities.animations; NumberAnimation { duration: 700; easing.type: Easing.OutCubic } }
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
                            running: toastRow.visible && toastRow.t.kind === "charging" && activities.animations
                            loops: Animation.Infinite
                            NumberAnimation { to: 1.25; duration: 450; easing.type: Easing.OutQuad }
                            NumberAnimation { to: 1; duration: 450; easing.type: Easing.InQuad }
                        }
                    }
                }

                    Image { // a Bluetooth device's own icon, white
                        visible: toastRow.deviceIcon && !toastRow.isPower
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        source: toastRow.t.icon ? "image://mime/" + toastRow.t.icon : ""
                        sourceSize: Qt.size(44, 44)
                        // Device icons are monochrome theme icons drawn for light backgrounds: render them white.
                        // Software mode cannot tint (no shader): show the icon as it is, better visible than gone.
                        layer.enabled: visible && !memtune.running
                        layer.effect: MultiEffect { brightness: theme.dark ? 1.0 : -1.0 } // white on dark, black on light
                    }
                    LineIcon {
                        visible: !toastRow.isPower && !toastRow.isCover && !toastRow.deviceIcon && toastRow.t.kind !== "color"
                        anchors.centerIn: parent
                        glyph: root.toastGlyphFor(toastRow.t.kind)
                        sourceSize: Qt.size(18, 18)
                        color: toastRow.tint
                    }
                    CoverArt { // the cover of a new song, or the thumbnail of a file
                        visible: toastRow.isCover
                        anchors.fill: parent
                        radius: 11
                        source: toastRow.t.art || (toastRow.t.icon ? "image://mime/" + toastRow.t.icon : toastRow.t.kind === "media" ? "image://mime/audio-x-generic" : "")
                    }
                    Rectangle { // a picked colour
                        visible: toastRow.t.kind === "color"
                        anchors.fill: parent
                        radius: 11
                        color: toastRow.t.kind === "color" ? toastRow.t.detail : "transparent"
                        border.width: 1.5
                        border.color: theme.outline
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText // a name can look like markup
                        text: toastRow.t.title
                        color: root.textPrimary
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: toastRow.t.kind === "color" ? "" : toastRow.t.kind === "bluetooth" && toastRow.t.level >= 0
                              ? toastRow.t.detail + " · " + toastRow.t.level + "%" : toastRow.t.detail
                        color: toastRow.t.kind === "charging" ? theme.success
                             : toastRow.t.kind === "lowbattery" ? theme.danger
                             : toastRow.t.kind === "bluetooth" && toastRow.t.detail !== "Disconnected" ? root.accent
                             : root.textSecondary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
                Equalizer {
                    visible: toastRow.t.kind === "media"
                    Layout.alignment: Qt.AlignVCenter
                    color: toastRow.tint
                    maxHeight: 16
                }
                Rectangle { // more are waiting
                    visible: root.toastQueue.length > 0
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: moreText.implicitWidth + 12
                    implicitHeight: 18
                    radius: 9
                    color: Qt.rgba(1, 1, 1, 0.12)
                    Text {
                        id: moreText
                        anchors.centerIn: parent
                        text: "+" + root.toastQueue.length
                        color: root.textSecondary
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }
            Rectangle { // how long it stays
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: island.shoulder + 16
                anchors.rightMargin: island.shoulder + 16
                anchors.bottomMargin: 5
                height: 2
                radius: 1
                color: Qt.rgba(1, 1, 1, 0.1)
                Rectangle {
                    width: parent.width * root.toastLeft
                    height: parent.height
                    radius: 1
                    color: toastRow.tint
                }
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
            opacity: island.fade("peek")
            visible: opacity > 0
            PulseDot {
                visible: liveRow.l.kind === "copy" || liveRow.l.kind === "call"
                color: liveRow.l.color
            }
            LineIcon {
                visible: liveRow.l.kind === "share"
                glyph: "share"
                sourceSize: Qt.size(14, 14)
                color: liveRow.l.color
            }
            LineIcon {
                visible: !(liveRow.l.kind === "copy" || liveRow.l.kind === "call" || liveRow.l.kind === "share")
                glyph: liveRow.l.kind === "assistant" ? (assistant.state === "error" ? "mic-off" : "mic") : liveRow.l.kind === "timer" ? "timer" : liveRow.l.kind === "doomscroll" ? "phone" : liveRow.l.kind === "break" ? "heart" : liveRow.l.kind === "download" ? "download" : "share"
                sourceSize: Qt.size(14, 14)
                color: liveRow.l.color
            }
            Row { // the voice, while listening
                visible: liveRow.l.kind === "assistant" && assistant.state === "listening"
                height: 16
                spacing: 2
                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        y: (parent.height - height) / 2
                        width: 3
                        radius: 1.5
                        height: 4 + 12 * Math.min(1, assistant.level * (0.7 + 0.25 * index))
                        color: liveRow.l.color
                        Behavior on height { enabled: activities.animations; NumberAnimation { duration: 70 } }
                    }
                }
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
                textFormat: Text.PlainText // a name can look like markup
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
            Behavior on shown { enabled: activities.animations; NumberAnimation { duration: 400 } }
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
            opacity: root.musicShown ? 0 : island.fade("idle")
            visible: opacity > 0
            Behavior on opacity { enabled: activities.animations && !island.transitioning; NumberAnimation { duration: 140 } }
            Text {
                text: Qt.formatTime(root.idleNow, Qt.locale().timeFormat(Locale.ShortFormat))
                color: activities.clockAccent ? theme.accent : root.textPrimary
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Text { // the next task and what is left to it, or the next prayer
                visible: root.infoShown
                Layout.fillWidth: true
                Layout.leftMargin: 6
                text: root.showPrayer ? prayer.nextName + " " + prayer.nextTime + " · " + prayer.nextRemaining
                                      : "• " + tasks.nextTitle + " · " + tasks.nextRemaining
                color: root.showPrayer ? (prayer.soon ? theme.accent : root.textSecondary)
                       : tasks.nextState === "overdue" ? theme.danger : tasks.nextState === "soon" ? theme.accent : root.textSecondary
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Item { Layout.fillWidth: true; visible: !root.infoShown }
            DiscordBadge { Layout.alignment: Qt.AlignVCenter }
            // Microphone: orange while recording, red and struck through while muted.
            LineIcon {
                visible: activities.micMuted >= 0
                glyph: activities.micMuted === 1 ? "mic-off" : "mic"
                sourceSize: Qt.size(13, 13)
                color: activities.micMuted === 1 ? theme.danger : activities.micSpeaking ? theme.success : activities.micApps.length > 0 ? theme.warn : root.textSecondary
                scale: activities.micSpeaking ? 1.25 : 1
                Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 120 } }
            }
            LineIcon { // a camera is on
                visible: activities.privacyIndicators && activities.cameraApps.length > 0
                glyph: "camera"
                sourceSize: Qt.size(13, 13)
                color: theme.success
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

        // Idle with music: the cover, the song on two lines, the wave and a thin progress line, instead of the clock.
        RowLayout {
            id: capRow
            anchors.fill: parent
            anchors.leftMargin: island.shoulder + 14
            anchors.rightMargin: island.shoulder + 14
            anchors.topMargin: 3
            anchors.bottomMargin: 9 // the progress line runs below
            spacing: 10
            opacity: root.musicShown ? island.fade("idle") : 0
            visible: opacity > 0
            Behavior on opacity { enabled: activities.animations && !island.transitioning; NumberAnimation { duration: 140 } }
            CoverArt {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                Layout.alignment: Qt.AlignVCenter
                source: root.shownArt
                radius: 8
                opacity: 1 - Math.abs(root.capRoll)
                transform: Translate { y: root.capRoll * 12 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 0
                opacity: 1 - Math.abs(root.capRoll)
                transform: Translate { y: root.capRoll * 12 }
                MarqueeText {
                    Layout.fillWidth: true
                    text: root.shownTitle
                    color: root.textPrimary
                    pixelSize: 13
                    weight: Font.DemiBold
                    running: capRow.visible
                }
                MarqueeText {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: islandHover.hovered ? root.capHover : root.shownSub
                    color: root.textSecondary
                    pixelSize: 11
                    running: capRow.visible && !islandHover.hovered
                }
            }
            Text { // where it is in the song
                visible: media.lengthMs > 0
                text: root.capTime
                color: root.textSecondary
                font.pixelSize: 11
                font.features: { "tnum": 1 }
                Layout.alignment: Qt.AlignVCenter
            }
            Equalizer {
                barWidth: 2.5
                maxHeight: 18
                color: capColor.color
                Layout.alignment: Qt.AlignVCenter
            }
            DiscordBadge { Layout.alignment: Qt.AlignVCenter }
            LineIcon { // something is being shared
                visible: root.sharingNow
                glyph: "share"
                sourceSize: Qt.size(14, 14)
                color: theme.accent
            }
            LineIcon {
                visible: activities.privacyIndicators && activities.cameraApps.length > 0
                glyph: "camera"
                sourceSize: Qt.size(15, 15)
                color: theme.success
            }
            LineIcon {
                visible: activities.micApps.length > 0
                glyph: "mic"
                sourceSize: Qt.size(15, 15)
                color: activities.micSpeaking ? theme.success : theme.warn
                scale: activities.micSpeaking ? 1.25 : 1
                Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 120 } }
            }
        }
        Rectangle { // how far into the song: a thin line along the bottom edge
            visible: capRow.visible && media.lengthMs > 0
            opacity: capRow.opacity
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: island.shoulder + 16
            anchors.rightMargin: island.shoulder + 16
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            height: 2
            radius: 1
            color: Qt.rgba(1, 1, 1, 0.14)
            Rectangle {
                // straight to the value, never animated: a width that lags behind a capsule that is changing size would stick out of it
                width: Math.min(parent.width, parent.width * Math.max(0, root.capPos / Math.max(1, media.lengthMs)))
                height: parent.height
                radius: 1
                color: capColor.color
            }
        }

        // Search: Shift + Space. Apps matching what is typed, under the field; Enter opens one.
        Item { // the panel keeps its full size; the island's growing edge uncovers it, so rows and background arrive together
            anchors.fill: parent
            anchors.leftMargin: island.shoulder + 16
            anchors.rightMargin: island.shoulder + 16
            anchors.topMargin: 12
            anchors.bottomMargin: 12
            clip: true
            opacity: island.fade("search")
            visible: opacity > 0
            Loader {
                id: searchPanelLoader
                active: root.searchOpen || root.mode === "search"
                width: parent.width
                height: item ? item.wantedHeight - 24 : 0
                sourceComponent: SearchPanel {
                    id: searchPanel
                    width: parent.width
                    active: root.searchOpen
                    onCloseRequested: root.closeSearch()
                }
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
            opacity: island.fade("prompt")
            visible: opacity > 0
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LineIcon { glyph: doomscroll.reminding ? "phone" : "heart"; sourceSize: Qt.size(22, 22); color: theme.accent }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: doomscroll.reminding ? "Still scrolling?" : "Time to stretch"
                        color: root.textPrimary
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: doomscroll.reminding ? doomscroll.message + " Time for something else?" : "You've worked for " + Math.round(wellbeing.workedMs / 60000) + " min. A short break helps."
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
                PillButton { text: doomscroll.reminding ? "5 more min" : "Later (" + wellbeing.snoozeMinutes + " min)"; onClicked: doomscroll.reminding ? doomscroll.snooze(5) : wellbeing.later() }
                PillButton { text: doomscroll.reminding ? "Not today" : "Skip"; onClicked: doomscroll.reminding ? doomscroll.stopToday() : wellbeing.skip() }
                PillButton {
                    text: doomscroll.reminding ? "Take a break" : "Start break"
                    checked: true
                    onClicked: { if (doomscroll.reminding) doomscroll.dismiss(); wellbeing.startBreak() }
                }
            }
        }

        // Open: full panel. Fixed width, centered and clipped so it doesn't reflow mid-spring.
        Item {
            x: island.shoulder
            width: Math.max(0, island.width - 2 * island.shoulder)
            height: Math.max(0, island.height)
            clip: true
            opacity: island.fade("open")
            visible: opacity > 0

            ColumnLayout {
                id: panel
                x: (parent.width - width) / 2
                width: root.panelWidth - 20
                spacing: 10

                RowLayout {
                    id: headerRow
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.leftMargin: root.page === "shelf" ? 12 : 6
                    Layout.rightMargin: 8
                    spacing: 8

                    // Widget mode: the open panel is moved by its title bar too (the buttons keep their clicks).
                    DragHandler {
                        enabled: root.geoWidget && root.mode === "open"
                        target: null
                        acceptedButtons: Qt.LeftButton
                        dragThreshold: 8
                        onActiveChanged: if (active) root.dragBegin(); else root.dragEnd()
                        onCentroidChanged: if (active) root.dragMove(centroid.position.x - centroid.pressPosition.x,
                                                                      centroid.position.y - centroid.pressPosition.y, 1)
                    }

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
                            : root.page === "timer" ? "Timer" : root.page === "tasks" ? "Tasks" : root.page === "music" ? "Now playing" : root.page === "color" ? "Color picker" : root.page === "clipboard" ? "Clipboard" : root.page === "camera" ? "Camera" : root.page === "api" ? "API Tester" : "Olyzano Notch"
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
                        Behavior on Layout.preferredWidth { enabled: activities.animations; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
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
                        visible: assistant.available && assistant.enabled && assistant.modelState === "ready" && !root.selecting
                        iconName: "mic"
                        checked: assistant.state === "listening" || assistant.state === "thinking"
                        Accessible.name: "Voice command"
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: "Say a command: open an app, search, volume, timer…"
                        onClicked: assistant.start()
                    }
                    IconButton {
                        visible: (root.page === "shelf" || root.page === "music" || root.page === "timer" || root.page === "tasks" || root.page === "color" || root.page === "clipboard" || root.page === "camera" || root.page === "api") && !root.selecting
                        iconName: "gamepad"
                        checked: gaming.enabled
                        Accessible.name: gaming.enabled ? "Gaming mode is on: turn it off" : "Gaming mode: keep away from full-screen games"
                        onClicked: {
                            gaming.enabled = !gaming.enabled
                            root.infoText = gaming.enabled ? "Gaming mode on: Notch stays out of the way of an app that fills the screen" : "Gaming mode off"
                            infoTimer.restart()
                        }
                    }
                    IconButton {
                        visible: (root.page === "shelf" || root.page === "music" || root.page === "timer" || root.page === "tasks" || root.page === "color" || root.page === "clipboard" || root.page === "camera" || root.page === "api") && !root.selecting
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
                // Loader: not built while Settings/theme is in use — fewer live theme bindings on a swatch click.
                Loader {
                    id: actionBarLoader
                    visible: active
                    active: !root.selecting && (root.page === "shelf" || root.page === "music" || root.page === "timer" || root.page === "tasks" || root.page === "color" || root.page === "clipboard" || root.page === "camera" || root.page === "api")
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    sourceComponent: ActionBar {
                        page: root.page
                        musicAvailable: media.active
                        shareAvailable: share.count + nearby.count > 0
                        shareBusy: share.activeCount + nearby.activeCount > 0
                        tasksBadge: tasks.overdueCount
                        onPick: (target) => root.page = target
                        colorAvailable: colorPicker.supported || notch.recentColors.length > 0
                        onAddFiles: addDialog.open()
                        onNewNote: root.newNote()
                        onOpenShares: {
                            const s = root.sharePopup()
                            if (s.visible && s.view === "sessions")
                                s.close()
                            else
                                s.openSessions()
                        }
                    }
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
                        Layout.minimumHeight: 206
                    }
                    LyricsPanel {
                        visible: lyrics.enabled
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.minimumHeight: 206
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
                        Layout.preferredHeight: 30
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

                // ---- Clipboard: what was copied lately ----
                Loader {
                    active: root.page === "clipboard"
                    visible: active // an idle Loader is still a layout item: its spacing left a gap above the page
                    Layout.fillWidth: true
                    Layout.maximumWidth: 100000
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    sourceComponent: ClipboardPage { shell: root }
                }

                // ---- Camera: a mirror, and a photo booth ----
                Loader {
                    active: root.page === "camera"
                    visible: active // an idle Loader is still a layout item: its spacing left a gap above the page
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    sourceComponent: CameraPage { shell: root }
                }

                // ---- Color: pick one from the screen, and the ones picked before ----
                Loader {
                    active: root.page === "color"
                    visible: active // an idle Loader is still a layout item: its spacing left a gap above the page
                    Layout.fillWidth: true
                    Layout.maximumWidth: 100000
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    sourceComponent: ColorPage { shell: root }
                }

                // ---- API tester: HTTP requests, collections, environments ----
                Loader {
                    id: apiPageLoader
                    active: root.page === "api"
                    visible: active
                    Layout.fillWidth: true
                    Layout.maximumWidth: 100000
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.minimumHeight: 360
                    sourceComponent: ApiPage { shell: root }
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
                    Layout.preferredHeight: 104
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
                        spacing: 5
                        LineIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            glyph: "download"
                            sourceSize: Qt.size(24, 24)
                            color: root.dragOver ? root.accent : root.textSecondary
                            scale: root.dragOver ? 1.15 : 1
                            Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 140; easing.type: Easing.OutBack } }
                        }
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

                // The files, as many rows as there are: the grid grows to three rows, then scrolls up and down.
                GridView {
                    id: list
                    visible: root.page === "shelf" && count > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    readonly property int maxRows: 3
                    Layout.preferredHeight: Math.max(144, Math.min(contentHeight, maxRows * cellHeight))
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: notch.items
                    property var selectedIds: root.selection
                    // Tiles share the row exactly: as many whole tiles (>= 96 px) as fit, never a cut-off one.
                    readonly property real gap: 4
                    // The row is always this much narrower than the grid (room for the scrollbar, shown or not), so the cells
                    // add up to less than the width and GridView never fits one column fewer than meant (which left a gap).
                    readonly property real usable: Math.max(96, width - 10)
                    readonly property int tilesPerRow: Math.max(1, Math.floor((usable + gap) / (96 + gap)))
                    cellWidth: usable / tilesPerRow
                    readonly property real tileWidth: cellWidth - gap
                    cellHeight: 140
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
                    WheelHandler {
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: (e) => {
                            const d = e.pixelDelta.y !== 0 ? e.pixelDelta.y : e.angleDelta.y / 120 * list.cellHeight / 2
                            list.contentY = Math.max(list.originY, Math.min(Math.max(list.originY, list.originY + list.contentHeight - list.height), list.contentY - d))
                        }
                    }
                    ScrollBar.vertical: ScrollBar {
                        id: tileBar
                        policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        padding: 2
                        contentItem: Rectangle {
                            implicitWidth: tileBar.hovered || tileBar.pressed ? 8 : 5
                            radius: width / 2
                            color: tileBar.pressed ? theme.textDim : theme.textFaint
                            Behavior on implicitWidth { enabled: activities.animations; NumberAnimation { duration: 120 } }
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
                        NumberAnimation { properties: "x,y"; duration: 280; easing.type: Easing.OutCubic }
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

                Loader {
                    id: timerPageLoader
                    active: root.page === "timer"
                    visible: active // an idle Loader is still a layout item: its spacing left a gap above the page
                    Layout.fillWidth: true
                    Layout.maximumWidth: 100000
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    sourceComponent: TimerPage { shell: root }
                }

                Loader {
                    id: tasksPageLoader
                    active: root.page === "tasks"
                    visible: root.page === "tasks" // an idle Loader still keeps its minimum height, which left a gap above the other pages
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    Layout.minimumHeight: 300
                    sourceComponent: TasksPage { shell: root }
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
                            text: "On a break · " + root.clock(root.breakLeft) + " left"
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

                Loader {
                    id: notePageLoader
                    active: root.page === "note"
                    visible: active // an idle Loader is still a layout item: its spacing left a gap above the page
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    sourceComponent: NotePage { shell: root }
                }

                Loader {
                    id: settingsLoader
                    active: false // switched on the first time Settings is opened, then kept
                    visible: root.page === "settings"
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.preferredHeight: 480
                    sourceComponent: SettingsPage { shell: root }
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

        // A click on the "Sharing" pill opens the panel on the running shares, however long ago it was closed.
        TapHandler {
            enabled: root.mode === "peek" && root.live && root.live.kind === "share"
            onTapped: root.showShares()
        }

        // A click on the capsule while music plays goes straight to the player (without waiting for the dwell).
        TapHandler {
            enabled: root.mode === "idle" && root.musicShown
            onTapped: {
                root.page = "music"
                root.dwelled = true
            }
        }

        // Widget mode: a click opens the panel beside the card, a drag moves the card.
        TapHandler {
            enabled: root.floating && !root.dragging
            onTapped: {
                if (root.musicShown) root.page = "music"
                else if (root.live && root.live.kind === "timer") root.page = "timer"
                else if (root.taskShown && !root.showPrayer) root.page = "tasks"
                root.dwelled = true
            }
        }
        DragHandler {
            id: widgetDrag
            enabled: root.floating
            target: null
            acceptedButtons: Qt.LeftButton
            dragThreshold: 8
            onActiveChanged: if (active) root.dragBegin(); else root.dragEnd()
            onCentroidChanged: if (active) root.dragMove(centroid.position.x - centroid.pressPosition.x,
                                                         centroid.position.y - centroid.pressPosition.y, island.scale)
        }

        // Real pointer motion means no drag is in progress (a drag delivers drag events instead).
        // Synthetic hovers (sent while animating) repeat the last position, so they don't count.
        HoverHandler {
            id: islandHover
            property point last
            onHoveredChanged: {
                root.dbg("island hovered=" + hovered)
                if (hovered && root.mode === "idle") {
                    if (root.widgetMode) {
                        // the card opens on a click: pointing at it is how it is picked up to be moved
                    } else if (root.capsuleMode === "capsule") { // the capsule is there to be pointed at: open at once
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
        x: (root.geoWidget ? island.x + island.width / 2 : root.width / 2) + Math.max(island.width, 44) / 2 + 6
        y: root.geoWidget ? island.y + 3 : 3
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
        Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 200 } }
    }

    // The only live area while hidden: a thin strip on the top edge.
    Item {
        id: edge
        enabled: !root.widgetMode // widget mode: pointing at the top edge opens nothing, the card is what you click
        visible: !root.widgetMode
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
                    text: modelData.name
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
        Behavior on scale { enabled: activities.animations; SpringAnimation { spring: 5; damping: root.damp(0.4) } }
        Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 120 } }

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
                textFormat: Text.PlainText // a name can look like markup
                text: (quickLookCard.info.name || "") + "  ·  " + (quickLookCard.info.sizeText || "") + "  ·  "
                      + (quickLookCard.info.addedText || "")
                color: theme.textDim
                font.pixelSize: 12
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignHCenter
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

    Loader {
        id: shareLoader
        active: false // built the first time something is shared, then kept
        sourceComponent: SharePopup { shell: root }
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
