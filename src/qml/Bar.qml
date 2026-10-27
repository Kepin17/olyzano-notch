import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The status bar: a glass strip along the top edge of each monitor with the app in front on the left, the status icons
// and the clock on the right, and the middle left clear for the notch. It stays (Always show, optionally keeping
// windows out from under it) or slides away until the pointer touches the top edge (Auto-hide). Its window is taller
// than the strip, with room for Quick Settings; only what is visible takes the pointer. Settings: Settings → Panel.
Window {
    id: root
    readonly property int stripH: topbar.height
    width: Screen.width > 0 ? Screen.width : 1920
    height: stripH + 760 // the strip, and room below it for the Quick Settings card and the app's menus
    visible: false
    color: "transparent"
    title: "Olyzano Notch Bar"

    property bool quickOpen: false
    property int menuOpen: -1 // which of the app's menus is down (an index into appMenu.menus)
    property real menuX: 0
    readonly property bool menuShown: menuOpen >= 0 && menuOpen < appMenu.menus.length
    property bool calOpen: false // the calendar, from the clock
    property bool trayMenu: false // the menu of a tray item
    property bool trayMore: false // the tray items that did not fit
    property var trayItems: []
    property real trayX: 0
    readonly property int trayShown: 6
    onQuickOpenChanged: if (quickOpen) { quick2.active = true; menuOpen = -1; calOpen = false; trayMenu = false; trayMore = false; launcherOpen = false; kdeOpen = false }
    onMenuOpenChanged: if (menuOpen >= 0) { quickOpen = false; calOpen = false; trayMenu = false; trayMore = false; launcherOpen = false; kdeOpen = false }
    onCalOpenChanged: if (calOpen) { quickOpen = false; menuOpen = -1; trayMenu = false; trayMore = false; launcherOpen = false; kdeOpen = false }
    onTrayMenuChanged: if (trayMenu) { quickOpen = false; calOpen = false; menuOpen = -1; trayMore = false; launcherOpen = false; kdeOpen = false }
    onTrayMoreChanged: if (trayMore) { quickOpen = false; calOpen = false; menuOpen = -1; trayMenu = false; launcherOpen = false; kdeOpen = false }
    property bool launcherOpen: false // the app launcher, from the grid button
    function toggleLauncher() { if (topbar.launcher) launcherOpen = !launcherOpen } // the Super key
    property bool kdeOpen: false // the KDE Connect card, from the phone icon in the tray
    onKdeOpenChanged: if (kdeOpen) { quickOpen = false; calOpen = false; menuOpen = -1; trayMenu = false; trayMore = false; launcherOpen = false }
    Timer { id: kdeCloser; interval: 900; onTriggered: if (!kdeHover.hovered && !kdeIconHover.hovered) root.kdeOpen = false }
    onLauncherOpenChanged: {
        if (launcherOpen) { launch.active = true; quickOpen = false; calOpen = false; menuOpen = -1; trayMenu = false; trayMore = false; kdeOpen = false }
        desktop.setKeyboardInteractive(root, launcherOpen) // typing in the search field
    }
    onActiveChanged: if (!active && launcherOpen) launcherOpen = false // the keyboard went elsewhere: it is done
    Timer { id: trayCloser; interval: 900; onTriggered: if (!trayHover.hovered && !moreHover.hovered) { root.trayMenu = false; root.trayMore = false } }
    function openTray(key, item) { // right click on a tray icon: its menu under it
        const m = trayHost.menu(key)
        if (m.length === 0) return
        trayItems = m
        trayX = Math.max(4, Math.min(width - 304, item.mapToItem(null, 0, 0).x - 8))
        trayMore = false
        trayMenu = true
        trayCloser.restart()
    }
    Timer { id: calCloser; interval: 700; onTriggered: if (!clockHover.hovered && !calHover.hovered) root.calOpen = false }
    Connections { target: appMenu; function onChanged() { if (root.menuOpen >= appMenu.menus.length) root.menuOpen = -1 } }
    Timer { id: menuCloser; interval: 600; onTriggered: if (!menuHover.hovered && !titlesHover.hovered) root.menuOpen = -1 }
    // Quick Settings opens from the status cluster (a click, or hovering it), and closes a moment after the pointer has left both.
    Timer { id: quickCloser; interval: 700; onTriggered: if (!clusterHover.hovered && !quickHover.hovered) root.quickOpen = false }
    Timer { id: quickOpener; interval: 160; onTriggered: if (clusterHover.hovered) root.quickOpen = true }
    property bool shown: topbar.behavior === "always"
    property bool edgeDwelled: false
    readonly property bool engaged: topbar.behavior === "always" || edgeDwelled || stripHover.hovered || quickOpen || menuShown || calOpen || trayMenu || trayMore || launcherOpen || kdeOpen
    onEngagedChanged: {
        if (engaged) {
            hideTimer.stop()
            shown = true
        } else {
            hideTimer.restart()
        }
    }
    Connections {
        target: topbar
        function onChanged() { root.shown = topbar.behavior === "always" || root.engaged }
    }
    Timer { id: hideTimer; interval: 500; onTriggered: root.shown = false }
    Timer { id: dwellTimer; interval: 150; onTriggered: root.edgeDwelled = true }

    // ---- the pointer: only what is visible (and nothing at all over a full-screen game) ----
    readonly property var maskRects: gaming.active ? [Qt.rect(0, 0, 1, 1)]
                                   : menuShown ? [Qt.rect(0, 0, width, stripH), Qt.rect(menuPop.x, menuPop.y, menuPop.reachW, menuPop.reachH)]
                                   : kdeOpen ? [Qt.rect(0, 0, width, stripH), Qt.rect(kdeCard.x, kdeCard.y, kdeCard.width, kdeCard.height)]
                                   : launcherOpen ? [Qt.rect(0, 0, width, stripH), Qt.rect(launch.x, launch.y, launch.width, launch.height)]
                                   : trayMenu ? [Qt.rect(0, 0, width, stripH), Qt.rect(trayPop.x, trayPop.y, trayPop.reachW, trayPop.reachH)]
                                   : trayMore ? [Qt.rect(0, 0, width, stripH), Qt.rect(moreCard.x, moreCard.y, moreCard.width, moreCard.height)]
                                   : calOpen ? [Qt.rect(0, 0, width, stripH), Qt.rect(cal.x, cal.y, cal.width, cal.height)]
                                   : quickOpen ? [Qt.rect(0, 0, width, stripH), Qt.rect(quick2.x, quick2.y, quick2.width, quick2.height)]
                                   : shown ? [Qt.rect(0, 0, width, stripH)]
                                   : [Qt.rect(0, 0, width, 2)]
    onMaskRectsChanged: desktop.setInputRegions(root, maskRects)
    onVisibleChanged: if (visible) { desktop.setInputRegions(root, maskRects); desktop.setBlurRegion(root, blurRect, 0) }
    readonly property rect blurRect: shown && topbar.blur ? Qt.rect(0, 0, width, stripH) : Qt.rect(0, 0, 0, 0)
    onBlurRectChanged: desktop.setBlurRegion(root, blurRect, 0)

    // ---- the clock ----
    property date now: new Date()
    Timer {
        interval: topbar.showSeconds ? 1000 : 10000
        repeat: true
        running: root.visible
        triggeredOnStart: true
        onTriggered: root.now = new Date()
    }

    // ---- the strip ----
    Rectangle {
        id: strip
        x: 0
        y: root.shown ? 0 : -root.stripH
        width: root.width
        height: root.stripH
        color: theme.dark ? Qt.rgba(0.07, 0.07, 0.08, topbar.opacity) : Qt.rgba(1, 1, 1, topbar.opacity)
        Behavior on y { enabled: activities.animations; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Rectangle { // the glass edge
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: theme.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)
        }
        HoverHandler { id: stripHover }

        // far left: the app launcher
        AbstractButton {
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            id: launchButton
            visible: topbar.launcher
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 30; height: 26
            Accessible.name: "Applications"
            onClicked: root.launcherOpen = !root.launcherOpen
            background: Rectangle { radius: 8; color: root.launcherOpen ? theme.accentSoft : parent.hovered ? theme.surfaceHover : "transparent" }
            contentItem: Item { LineIcon { anchors.centerIn: parent; glyph: "grid"; sourceSize: Qt.size(16, 16); color: root.launcherOpen ? theme.accent : theme.text } }
        }

        // left: the app in front
        RowLayout {
            anchors { left: parent.left; leftMargin: topbar.launcher ? 46 : 14; verticalCenter: parent.verticalCenter }
            width: Math.max(0, parent.width / 2 - 260)
            spacing: 10
            visible: topbar.showApp && dock.activeAppName !== ""
            Text {
                text: dock.activeAppName
                color: theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.maximumWidth: 220
            }
            Row { // the app's own menu
                id: titles
                visible: topbar.showMenu && appMenu.available
                spacing: 0
                HoverHandler {
                    id: titlesHover
                    onHoveredChanged: if (!hovered && root.menuShown) menuCloser.restart()
                }
                Repeater {
                    model: appMenu.menus
                    delegate: Rectangle {
                        id: title
                        required property var modelData
                        required property int index
                        width: label.implicitWidth + 18
                        height: root.stripH - 10
                        radius: 6
                        color: root.menuOpen === index || titleHover.hovered ? theme.accentSoft : "transparent"
                        Text {
                            id: label
                            anchors.centerIn: parent
                            text: title.modelData.label
                            color: title.modelData.enabled ? theme.text : theme.textFaint
                            font.pixelSize: 13
                        }
                        function open() {
                            appMenu.aboutToShow(modelData.id)
                            root.menuX = Math.max(4, Math.min(root.width - 304, title.mapToItem(null, 0, 0).x))
                            root.menuOpen = index
                        }
                        HoverHandler {
                            id: titleHover
                            onHoveredChanged: if (hovered && root.menuShown && root.menuOpen !== title.index && title.modelData.enabled) title.open()
                        }
                        TapHandler {
                            enabled: title.modelData.enabled
                            onTapped: root.menuOpen === title.index ? root.menuOpen = -1 : title.open()
                        }
                    }
                }
            }
            Text {
                visible: !(topbar.showMenu && appMenu.available)
                Layout.fillWidth: true
                text: dock.activeTitle
                color: theme.textDim
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        // right: status and the clock; pointing at it, or a click, opens Quick Settings
        RowLayout {
            id: right
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            spacing: 12
            Row { // the apps in the background
                id: trayRow
                visible: topbar.showTray && (trayHost.items.length > 0 || kdeconnect.deviceList.length > 0)
                spacing: 4
                Item { // KDE Connect: the phone, with its battery
                    id: kdeIcon
                    visible: kdeconnect.deviceList.length > 0
                    width: kdeRow.implicitWidth + 12; height: 24
                    HoverHandler {
                        id: kdeIconHover
                        onHoveredChanged: if (!hovered && root.kdeOpen) kdeCloser.restart()
                    }
                    Rectangle { anchors.fill: parent; radius: 6; color: theme.accentSoft; visible: kdeIconHover.hovered || root.kdeOpen }
                    Row {
                        id: kdeRow
                        anchors.centerIn: parent
                        spacing: 4
                        LineIcon { anchors.verticalCenter: parent.verticalCenter; glyph: "phone"; sourceSize: Qt.size(14, 14); color: theme.text }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: text !== ""
                            readonly property var first: kdeconnect.deviceList.length > 0 ? kdeconnect.deviceList[0] : null
                            text: first && first.charge >= 0 ? first.charge + "%" : ""
                            color: first && first.charge >= 0 && first.charge <= 20 && !first.charging ? theme.danger : theme.textDim
                            font.pixelSize: 11
                        }
                    }
                    TapHandler { onTapped: root.kdeOpen = !root.kdeOpen }
                }
                Repeater {
                    model: trayHost.items.slice(0, root.trayShown)
                    TrayIcon { }
                }
                TrayButton {
                    visible: trayHost.items.length > root.trayShown
                    onClicked: root.trayMore = !root.trayMore
                }
            }
            RowLayout {
            id: icons
            spacing: 12
            HoverHandler {
                id: clusterHover
                onHoveredChanged: {
                    if (hovered && topbar.openOn === "hover") quickOpener.restart()
                    if (!hovered) { quickOpener.stop(); if (root.quickOpen) quickCloser.restart() }
                }
            }
            TapHandler { onTapped: root.quickOpen = !root.quickOpen }
            LineIcon { visible: activities.micApps.length > 0; glyph: "mic"; sourceSize: Qt.size(15, 15); color: theme.warn }
            LineIcon { visible: activities.cameraApps.length > 0; glyph: "camera"; sourceSize: Qt.size(15, 15); color: theme.success }
            // Wi-Fi, Bluetooth, sound
            LineIcon { visible: quick.ethState === "connected"; glyph: "ethernet"; sourceSize: Qt.size(16, 16); color: theme.text }
            SignalIcon { visible: quick.wifiAvailable && (quick.wifiName !== "" || quick.ethState !== "connected"); size: 15
                         bars: quick.wifiEnabled ? quick.wifiBars : 0; off: !quick.wifiEnabled || quick.wifiName === ""
                         color: quick.wifiEnabled && quick.wifiName !== "" ? theme.text : theme.textFaint }
            LineIcon { visible: quick.btAvailable && quick.btEnabled; glyph: "bluetooth"; sourceSize: Qt.size(15, 15); color: quick.btConnected !== "" ? theme.accent : theme.textDim }
            LineIcon { visible: quick.audioAvailable; glyph: quick.muted || quick.volume === 0 ? "volume-off" : "volume"; sourceSize: Qt.size(16, 16); color: quick.muted ? theme.danger : theme.text }
            // battery
            RowLayout {
                visible: systemEvents.batteryLevel >= 0
                spacing: 5
                Item {
                    implicitWidth: 22
                    implicitHeight: 11
                    Rectangle {
                        width: 20; height: 11; radius: 3
                        color: "transparent"
                        border.width: 1.2
                        border.color: theme.textDim
                        Rectangle {
                            x: 2; y: 2
                            height: parent.height - 4
                            radius: 1.5
                            width: Math.max(1.5, (parent.width - 4) * Math.max(0, systemEvents.batteryLevel) / 100)
                            color: systemEvents.batteryCharging ? theme.success : systemEvents.batteryLevel <= 20 ? theme.danger : theme.text
                        }
                    }
                    Rectangle { x: 20.5; y: 3.5; width: 1.5; height: 4; radius: 0.75; color: theme.textDim }
                }
                Text {
                    visible: topbar.showBattery
                    text: (systemEvents.batteryCharging ? "⚡" : "") + systemEvents.batteryLevel + "%"
                    color: theme.textDim
                    font.pixelSize: 12
                }
            }
            }
            ColumnLayout {
                id: clock
                spacing: -1
                HoverHandler {
                    id: clockHover
                    onHoveredChanged: if (!hovered && root.calOpen) calCloser.restart()
                }
                TapHandler { onTapped: root.calOpen = !root.calOpen }
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: topbar.showSeconds ? Qt.formatTime(root.now, "hh:mm:ss") : Qt.formatTime(root.now, Qt.locale().timeFormat(Locale.ShortFormat))
                    color: theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: topbar.showDate
                    Layout.alignment: Qt.AlignRight
                    text: Qt.formatDate(root.now, "ddd d MMM")
                    color: theme.textDim
                    font.pixelSize: 10
                }
            }
        }
    }

    // ---- the app's menu ----
    AppMenuPopup {
        id: menuPop
        x: root.menuX
        y: root.stripH + 2
        visible: root.menuShown && root.shown
        items: root.menuShown ? appMenu.menus[root.menuOpen].children : []
        onItemsChanged: openSub = -1
        onCloseAll: root.menuOpen = -1
        HoverHandler {
            id: menuHover
            onHoveredChanged: if (!hovered && root.menuShown) menuCloser.restart()
        }
    }

    // ---- KDE Connect ----
    Rectangle {
        id: kdeCard
        width: 320
        height: kdeBody.implicitHeight + 28
        x: Math.max(8, root.width - width - 150)
        y: root.stripH + 6
        radius: theme.radiusLg
        visible: opacity > 0
        opacity: root.kdeOpen && root.shown ? 1 : 0
        Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 130 } }
        color: theme.dark ? Qt.rgba(0.09, 0.09, 0.10, Math.min(1, topbar.opacity + 0.25)) : Qt.rgba(1, 1, 1, Math.min(1, topbar.opacity + 0.25))
        border.width: 1
        border.color: theme.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.12)
        HoverHandler {
            id: kdeHover
            onHoveredChanged: if (!hovered && root.kdeOpen) kdeCloser.restart()
        }
        ColumnLayout {
            id: kdeBody
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: "KDE Connect"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                PillButton {
                    visible: kdeconnect.hasApp
                    text: "Open"
                    iconName: "external"
                    onClicked: { kdeconnect.openApp(); root.kdeOpen = false }
                }
            }
            Repeater {
                model: kdeconnect.deviceList
                Rectangle {
                    id: dev
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: devRow.implicitHeight + 20
                    radius: 14
                    color: theme.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.06)
                    RowLayout {
                        id: devRow
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                        spacing: 10
                        LineIcon { glyph: dev.modelData.type === "desktop" || dev.modelData.type === "laptop" ? "laptop" : "phone"; sourceSize: Qt.size(20, 20); color: theme.accent }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { Layout.fillWidth: true; text: dev.modelData.name; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight }
                            RowLayout {
                                spacing: 6
                                Rectangle { // battery bar
                                    visible: dev.modelData.charge >= 0
                                    Layout.preferredWidth: 60; Layout.preferredHeight: 6; radius: 3
                                    color: theme.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.12)
                                    Rectangle { width: parent.width * Math.max(0, dev.modelData.charge) / 100; height: parent.height; radius: 3; color: dev.modelData.charge <= 20 && !dev.modelData.charging ? theme.danger : dev.modelData.charging ? theme.success : theme.accent }
                                }
                                Text { text: dev.modelData.charge >= 0 ? (dev.modelData.charging ? "⚡ " : "") + dev.modelData.charge + "%" : "Connected"; color: theme.textDim; font.pixelSize: 11 }
                            }
                        }
                        PillButton { text: "Ping"; onClicked: kdeconnect.ping(dev.modelData.id) }
                        PillButton { visible: dev.modelData.type === "phone"; text: "Find"; onClicked: kdeconnect.findPhone(dev.modelData.id) }
                    }
                }
            }
        }
    }

    // ---- the app launcher ----
    // Built the first time it is opened, then kept (memory); the Loader takes the card's size, so it can be hovered and masked.
    Loader {
        id: launch
        readonly property string query: item ? item.query : ""
        active: false
        x: 8
        y: root.stripH + 6
        sourceComponent: Launcher {
            open: root.launcherOpen && root.shown
            onCloseRequested: root.launcherOpen = false
        }
        data: HoverHandler { id: launchHover; onHoveredChanged: if (!hovered && root.launcherOpen && launch.query === "") launcherCloser.restart() }
    }
    Timer { id: launcherCloser; interval: 2500; onTriggered: if (!launchHover.hovered && launch.query === "") root.launcherOpen = false }

    // ---- tray: one icon, its menu, and the overflow ----
    component TrayIcon: Item {
        id: ti
        required property var modelData
        width: 24; height: 24
        opacity: modelData.status === "NeedsAttention" ? 1 : 0.92
        HoverHandler { id: tih }
        Rectangle { anchors.fill: parent; radius: 6; color: theme.accentSoft; visible: tih.hovered }
        Image {
            anchors.centerIn: parent
            source: ti.modelData.icon
            sourceSize: Qt.size(32, 32)
            width: 16; height: 16
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
        Rectangle { visible: ti.modelData.status === "NeedsAttention"; x: 15; y: 3; width: 6; height: 6; radius: 3; color: theme.danger }
        MouseArea {
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            onClicked: mouse => {
                const p = ti.mapToItem(null, mouse.x, mouse.y)
                if (mouse.button === Qt.RightButton || (mouse.button === Qt.LeftButton && ti.modelData.isMenu)) root.openTray(ti.modelData.key, ti)
                else if (mouse.button === Qt.MiddleButton) trayHost.secondary(ti.modelData.key, p.x, p.y)
                else { root.trayMore = false; trayHost.activate(ti.modelData.key, p.x, p.y) }
            }
        }
    }
    component TrayButton: AbstractButton {
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        implicitWidth: 20; implicitHeight: 24
        contentItem: Item { LineIcon { anchors.centerIn: parent; glyph: "chevron"; sourceSize: Qt.size(13, 13); color: theme.textDim; rotation: -90 } }
    }
    AppMenuPopup {
        id: trayPop
        sink: trayHost
        x: root.trayX
        y: root.stripH + 2
        visible: root.trayMenu && root.shown
        items: root.trayItems
        onItemsChanged: openSub = -1
        onCloseAll: root.trayMenu = false
        HoverHandler {
            id: trayHover
            onHoveredChanged: if (!hovered && root.trayMenu) trayCloser.restart()
        }
    }
    Rectangle {
        id: moreCard
        width: moreFlow.implicitWidth + 16
        height: moreFlow.implicitHeight + 16
        x: Math.max(4, root.width - width - 160)
        y: root.stripH + 2
        radius: 12
        visible: root.trayMore && root.shown
        color: theme.dark ? Qt.rgba(0.09, 0.09, 0.10, Math.min(1, topbar.opacity + 0.25)) : Qt.rgba(1, 1, 1, Math.min(1, topbar.opacity + 0.25))
        border.width: 1
        border.color: theme.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.12)
        HoverHandler {
            id: moreHover
            onHoveredChanged: if (!hovered && root.trayMore) trayCloser.restart()
        }
        Flow {
            id: moreFlow
            x: 8; y: 8
            width: Math.min(implicitWidth, 200)
            spacing: 4
            Repeater {
                model: trayHost.items.slice(root.trayShown)
                TrayIcon { }
            }
        }
    }

    // ---- the calendar ----
    CalendarCard {
        id: cal
        x: root.width - width - 10
        y: root.stripH + 6
        open: root.calOpen && root.shown
        HoverHandler {
            id: calHover
            onHoveredChanged: if (!hovered && root.calOpen) calCloser.restart()
        }
    }

    // ---- Quick Settings ----
    Loader {
        id: quick2
        active: false // built the first time Quick Settings is opened, then kept
        x: root.width - width - 10
        y: root.stripH + 6
        sourceComponent: QuickSettings { open: root.quickOpen && root.shown }
        data: HoverHandler {
            id: quickHover
            onHoveredChanged: if (!hovered && root.quickOpen) quickCloser.restart()
        }
    }
    Connections {
        target: gaming
        function onChanged() { if (gaming.active) { root.quickOpen = false; root.menuOpen = -1; root.calOpen = false; root.trayMenu = false; root.trayMore = false; root.launcherOpen = false; root.kdeOpen = false } }
    }

    // ---- the top edge, while the bar is away ----
    Item {
        x: 0
        width: root.width
        height: 2
        visible: !root.shown
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
}
