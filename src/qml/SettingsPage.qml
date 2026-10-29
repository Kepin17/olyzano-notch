import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Settings page: a list of sections on the left (with a search for a setting), the chosen section on the right.
RowLayout {
    id: settingsScroll
    required property var shell
    // Its menu / folder dialog are open: Notch keeps the whole window clickable meanwhile.
    property bool keysOpen: false // the member-keys window
    property bool guideOpen: false // the "set up your own server" guide
    readonly property bool overlayOpen: ruleMenu.visible || folderDialog.visible || keysOpen || guideOpen
    property string category: shell.settingsCategory // the section shown; kept by Main while Notch runs (this page may be unloaded)
    onCategoryChanged: shell.settingsCategory = category
    property string query: ""
    visible: shell.page === "settings"
    Layout.fillWidth: true
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    Layout.preferredHeight: 480
    spacing: 14

    // id, name, icon, what it is for
    // id, name, icon, what it is for, and the heading that starts a group in the list. In the order a new user reaches for them:
    // the basics first, then what is on the screen, then what is kept and shared, then you.
    readonly property var sections: [
        ["general", "General", "gear", "Start-up, how the notch sits on the screen, gaming mode and sounds.", "Basics"],
        ["appearance", "Appearance", "palette", "Themes, size, corners and light or dark.", ""],
        ["panel", "Panel", "panel", "A glass status bar along the top, with Quick Settings.", "On the screen"],
        ["dock", "Dock", "dock", "The app dock at the edge of the screen.", ""],
        ["alerts", "Alerts & activities", "notifications", "What the notch shows you: chargers, downloads, calls, the microphone and more.", ""],
        ["files", "Files", "folder", "How long files stay, where they go and what is added by itself.", "Files & sharing"],
        ["sharing", "Sharing & Nearby", "share", "Servers for Share via QR, and the name other computers see.", ""],
        ["wellbeing", "Wellbeing", "heart", "Break and eye reminders.", "You"],
        ["about", "About", "info", "Version, author, links and support.", ""]
    ]
    // What a search can find: section, setting, words that also describe it.
    readonly property var index: [
        ["about", "Updates", "update upgrade new version release check download latest github notify"], ["wellbeing", "Doomscrolling", "doomscroll scrolling instagram tiktok youtube shorts reels feed warn screen time"], ["general", "Start at login", "autostart boot startup"], ["general", "How the notch sits on the screen", "hidden capsule always open dynamic island pin"],
        ["general", "Gaming mode", "game fullscreen full screen maximised hide"], ["general", "Search from the notch", "search apps launcher spotlight shortcut win space meta keyboard open app"], ["general", "Low memory mode", "memory ram performance software renderer gpu graphics lower usage"], ["general", "Clipboard history", "clipboard copy paste history remember klipper"], ["general", "Sounds", "sound effects volume quiet mute"],
        ["appearance", "Theme", "style colors classic washi frost ocean forest sunset lavender mocha sakura matcha ai indigo cheongja hanbok hanji japanese korean"], ["general", "Keep windows clear of the capsule", "reserve space taskbar work area maximise maximize windows below auto align strip"], ["general", "Widget (beta)", "widget floating card drag move position beta"], ["general", "Spectrum style", "spectrum equalizer bars wave dots pulse song capsule style visualizer"], ["general", "Voice assistant", "voice assistant commands speech microphone whisper offline say listen"], ["general", "Capsule size", "capsule size scale bigger smaller dynamic island"], ["appearance", "Capsule shape", "capsule shape notch pill rounded dynamic island edge look widget"],["about", "About Olyzano Notch", "version author website demo source license coffee donate support traktir qr"], ["appearance", "Light or dark", "brightness dark light system mode"], ["appearance", "Glass", "glass glassmorphism translucent transparent blur frosted see-through"], ["appearance", "Border matches my system accent", "border outline edge accent windows color capsule widget"], ["appearance", "Clock follows theme colour", "clock time hour accent colour colour color theme status bar idle"], ["appearance", "Bounce", "bounce overshoot spring elastic animation capsule panel soft bouncy"], ["appearance", "Animations and transitions", "animation animations transition motion reduce disable off smooth instant bounce spring"],
        ["panel", "Status bar", "panel top bar glass clock quick settings"], ["panel", "Always show or auto-hide", "bar hide slide reserve space"],
        ["panel", "Transparency and blur", "glass opacity bar"], ["panel", "What the bar shows", "app name date seconds battery percentage"],
        ["panel", "Quick Settings", "wifi bluetooth volume brightness power click hover system cpu ram memory gpu disk network monitor usage"],
        ["alerts", "Prayer times", "prayer salat sholat solat adzan azan fajr subuh dhuhr zuhur asr ashar maghrib isha isya mosque muslim reminder city kemenag"], ["alerts", "Charger & Bluetooth alerts", "battery plug device connect low"], ["alerts", "Clock when idle", "time clock capsule"],
        ["alerts", "Downloads in the notch", "download browser progress"], ["alerts", "Phone calls & notifications", "kde connect phone"],
        ["alerts", "Microphone & camera indicators", "privacy dot mic camera"], ["alerts", "Notifications from this computer", "desktop apps slack"],
        ["files", "Keep files for", "retention days expire delete"], ["files", "Sort new files by type", "rules category pdf images"],
        ["files", "Storage folder", "where files are stored directory"], ["files", "Add new screenshots", "spectacle screenshot"],
        ["files", "Add new downloads", "browser downloads folder"], ["files", "Add files from KDE Connect", "phone"],
        ["dock", "Show the app dock", "dock enable"], ["dock", "Always show", "auto hide dock"], ["dock", "Position", "edge bottom left right"],
        ["dock", "Icon size", "size"], ["dock", "Transparency", "opacity glass"], ["dock", "Blur behind the dock", "glass blur"],
        ["dock", "Magnification", "zoom"], ["dock", "Animation", "motion smooth spring"], ["dock", "Show running apps", "windows"],
        ["dock", "Show Trash", "bin"], ["dock", "Apps kept in the dock", "pinned"],
        ["sharing", "Nearby device name", "computer name receive"], ["sharing", "Sharing servers", "relay vps share qr server"], ["sharing", "Set up your own server (guide)", "tutorial guide how to setup server docker vps domain self host install key"],
        ["wellbeing", "Stretch reminder", "break work minutes"], ["wellbeing", "Don't interrupt", "video call pomodoro"],
        ["wellbeing", "20-20-20 eye reminder", "eyes"], ["wellbeing", "Today", "breaks count"]
    ]
    readonly property var results: {
        const q = query.trim().toLowerCase()
        if (q === "")
            return []
        return index.filter(e => (e[1] + " " + e[2]).toLowerCase().indexOf(q) >= 0)
    }
    function sectionOf(id) { return sections.find(s => s[0] === id) || sections[0] }

    // ---- the list on the left ----
    ColumnLayout {
        Layout.preferredWidth: 176
        Layout.maximumWidth: 176
        Layout.fillHeight: true
        spacing: 8
        TextField {
            id: searchField
            Layout.fillWidth: true
            implicitHeight: 30
            leftPadding: 12
            placeholderText: "Search settings"
            placeholderTextColor: theme.placeholder
            color: theme.text
            selectionColor: theme.accent
            font.pixelSize: 13
            text: settingsScroll.query
            onTextChanged: settingsScroll.query = text
            background: Rectangle { radius: 15; color: theme.well; border.width: 1; border.color: searchField.activeFocus ? theme.accent : theme.outline }
        }
        // the sections ...
        Repeater {
            model: settingsScroll.query === "" ? settingsScroll.sections : []
            delegate: ColumnLayout {
              id: group
              required property var modelData
              Layout.fillWidth: true
              spacing: 0
              Text { // the heading that starts a group
                visible: group.modelData[4] !== ""
                Layout.topMargin: group.modelData[0] === "general" ? 0 : 4
                Layout.leftMargin: 6
                text: group.modelData[4].toUpperCase()
                color: theme.textFaint
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1
              }
              AbstractButton {
                  HoverHandler { cursorShape: Qt.PointingHandCursor }
                id: side
                property var modelData: group.modelData
                readonly property bool on: settingsScroll.category === modelData[0]
                Layout.fillWidth: true
                implicitHeight: 32
                onClicked: settingsScroll.category = modelData[0]
                background: Rectangle { radius: theme.radiusSm + 2; color: side.on ? theme.accentSoft : side.hovered ? theme.surfaceHover : "transparent" }
                contentItem: RowLayout {
                    spacing: 9
                    Item { Layout.preferredWidth: 6 }
                    LineIcon { glyph: side.modelData[2]; sourceSize: Qt.size(16, 16); color: side.on ? theme.accent : theme.textDim }
                    Text {
                        Layout.fillWidth: true
                        text: side.modelData[1]
                        color: theme.text
                        font.pixelSize: 13
                        font.weight: side.on ? Font.DemiBold : Font.Medium
                        elide: Text.ElideRight
                    }
                    Rectangle { // an update is waiting
                        visible: side.modelData[0] === "about" && updates.newer && !updates.skipped
                        Layout.preferredWidth: 8; Layout.preferredHeight: 8; Layout.rightMargin: 8
                        radius: 4
                        color: theme.accent
                    }
                }
            }
            }
        }
        // ... or what the search found
        Repeater {
            model: settingsScroll.results
            delegate: AbstractButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                id: hit
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 44
                onClicked: { settingsScroll.category = modelData[0]; settingsScroll.query = "" }
                background: Rectangle { radius: theme.radiusSm + 2; color: hit.hovered ? theme.surfaceHover : "transparent" }
                contentItem: ColumnLayout {
                    spacing: 0
                    Text { Layout.fillWidth: true; text: hit.modelData[1]; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight; leftPadding: 6 }
                    Text { Layout.fillWidth: true; text: settingsScroll.sectionOf(hit.modelData[0])[1]; color: theme.textDim; font.pixelSize: 11; leftPadding: 6 }
                }
            }
        }
        Caption {
            visible: settingsScroll.query !== "" && settingsScroll.results.length === 0
            Layout.fillWidth: true
            text: "Nothing matches."
        }
        Item { Layout.fillHeight: true }
    }
    Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: theme.outline }

    // ---- the chosen section ----
    function scrollTo(y) { scrollTimer.target = y; scrollTimer.restart() } // for screenshots, once the section has laid out
    Timer { id: scrollTimer; property real target: 0; interval: 400; onTriggered: scroller.contentItem.contentY = target }
    ScrollView {
        id: scroller
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentWidth: availableWidth
        clip: true
    ColumnLayout {
        id: settingsColumn
        width: scroller.availableWidth - 8
        spacing: 16

        // What this section is, in one line.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: settingsScroll.sectionOf(settingsScroll.category)[1]
                color: theme.text
                font.family: theme.displayFont !== "" ? theme.displayFont : Qt.application.font.family
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            Caption { Layout.fillWidth: true; text: settingsScroll.sectionOf(settingsScroll.category)[3] }
        }

        Loader {
            active: settingsScroll.category === "general"
            visible: active // an idle Loader is still a layout item: its spacing would leave a gap
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                Layout.fillWidth: true
                spacing: 16

                SettingCard {
                    title: "Startup & behaviour"
                    SettingToggle {
                        title: "Start at login"
                        checked: notch.autostart
                        onToggled: notch.autostart = !notch.autostart
                    }
                    SettingToggle {
                        title: "Keep away from full-screen apps and games"
                        caption: "While an app or game fills the screen, Olyzano Notch and the dock cannot be reached, so a stray click never lands in them."
                        checked: gaming.enabled
                        onToggled: gaming.enabled = !gaming.enabled
                    }
                }

                // One choice for how the notch sits; only the options that belong to it appear below.
                SettingCard {
                    title: "Search from the notch"
                    SettingToggle {
                        title: "Search apps with a shortcut"
                        caption: "Press " + launcher.searchKeyLabel + ", type, and the apps that match drop down from the notch; Enter opens one."
                                 + (Qt.platform.os === "windows" || launcher.superKeyAvailable ? "" : " Needs KDE Plasma for the shortcut; elsewhere bind a key to: qdbus6 org.olyzano.Notch /Launcher openSearch")
                        checked: launcher.searchEnabled
                        onToggled: launcher.searchEnabled = !launcher.searchEnabled
                    }
                    // The key: press Change, then the combination you want (it needs Win, Ctrl, Alt or Shift with another key).
                    RowLayout {
                        id: keyRow
                        visible: launcher.searchEnabled
                        Layout.fillWidth: true
                        spacing: 8
                        property bool capturing: false
                        property string hint: ""
                        Text { text: "Shortcut"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                        Item { Layout.fillWidth: true }
                        FocusScope {
                            id: keyBox
                            Layout.preferredWidth: 170
                            Layout.preferredHeight: 30
                            focus: keyRow.capturing
                            Keys.onPressed: (e) => {
                                if (!keyRow.capturing)
                                    return
                                e.accepted = true
                                if (e.key === Qt.Key_Escape) { keyRow.capturing = false; keyRow.hint = ""; return }
                                const t = launcher.describeKey(e.key, e.modifiers)
                                if (t === "") { keyRow.hint = e.key === Qt.Key_Shift || e.key === Qt.Key_Control || e.key === Qt.Key_Alt || e.key === Qt.Key_Meta ? "" : "Add Win, Ctrl, Alt or Shift"; return }
                                launcher.searchKey = t
                                keyRow.capturing = false
                                keyRow.hint = ""
                            }
                            onActiveFocusChanged: if (!activeFocus) keyRow.capturing = false
                            Rectangle {
                                anchors.fill: parent
                                radius: theme.radiusSm + 2
                                color: theme.well
                                border.width: 1
                                border.color: keyRow.capturing ? theme.accent : theme.hairline
                                Text {
                                    anchors.centerIn: parent
                                    text: keyRow.capturing ? "Press the keys…" : launcher.searchKeyLabel
                                    color: keyRow.capturing ? theme.accentText : theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                        PillButton { text: keyRow.capturing ? "Cancel" : "Change"; onClicked: { keyRow.hint = ""; keyRow.capturing = !keyRow.capturing; if (keyRow.capturing) keyBox.forceActiveFocus() } }
                        PillButton { visible: !launcher.searchKeyIsDefault; text: "Reset"; iconName: "undo"; onClicked: launcher.resetSearchKey() }
                    }
                    Caption { visible: keyRow.hint !== ""; Layout.fillWidth: true; text: keyRow.hint; color: theme.warn }
                    Caption { visible: keyRow.visible && Qt.platform.os !== "windows" && launcher.superKeyAvailable; Layout.fillWidth: true; text: "On KDE it can also be changed in System Settings \u2192 Shortcuts \u2192 KWin \u2192 Search apps (Notch)." }
                    Caption { visible: launcher.searchNote !== ""; Layout.fillWidth: true; text: launcher.searchNote; color: theme.danger }
                    PillButton { visible: launcher.searchEnabled; text: "Open it now"; iconName: "search"; onClicked: shell.openSearch() }
                }

                SettingCard {
                    title: "Notch"
                    Segmented {
                        Layout.fillWidth: true
                        model: [["hidden", "Hidden"], ["capsule", "Capsule"], ["widget", "Widget · Beta"], ["open", "Always open"]]
                        value: activities.capsuleMode
                        onPicked: (v) => activities.capsuleMode = v
                    }
                    Caption {
                        Layout.fillWidth: true
                        text: activities.capsuleMode === "capsule" ? "A small capsule stays at the top, like a Dynamic Island. Point at it to open."
                            : activities.capsuleMode === "widget" ? "Beta: a floating card you can drag anywhere. Click it to open the panel beside it."
                            : activities.capsuleMode === "open" ? "The panel stays open at the top of the screen."
                            : "Only a thin line shows at the top edge. Rest the pointer there to open."
                    }
                    PillButton { visible: activities.capsuleMode === "widget"; text: "Reset position"; iconName: "undo"; onClicked: activities.resetWidgetPos() }
                DockSlider {
                    visible: activities.capsuleMode === "capsule" || activities.capsuleMode === "widget"
                    title: "Capsule size"
                    valueText: Math.round(activities.capsuleScale * 100) + "%"
                    from: 80; to: 130; stepSize: 5
                    value: Math.round(activities.capsuleScale * 100)
                    onMoved: activities.capsuleScale = value / 100
                }
                SettingToggle {
                    visible: desktop.canReserve && activities.capsuleMode === "capsule"
                    title: "Keep other windows clear of the capsule"
                    caption: "Maximised windows start below the capsule, like under a taskbar. Windows and KDE Plasma (Wayland)."
                    checked: activities.reserveSpace
                    onToggled: activities.reserveSpace = !activities.reserveSpace
                }
                }

                SettingCard {
                    title: "On the capsule"
                    visible: activities.capsuleMode !== "hidden"
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "Spectrum beside the song"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [["bars", "Bars"], ["thin", "Thin"], ["dots", "Dots"], ["wave", "Wave"], ["pulse", "Pulse"], ["off", "Off"]]
                                PillButton {
                                    required property var modelData
                                    text: modelData[1]
                                    checked: activities.eqStyle === modelData[0]
                                    onClicked: activities.eqStyle = modelData[0]
                                }
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 64; Layout.preferredHeight: 34
                            radius: 17
                            color: "#000000"
                            border.width: 1; border.color: theme.outline
                            Equalizer { anchors.centerIn: parent; preview: true; barWidth: 3; maxHeight: 18 }
                        }
                    }
                }
                SettingToggle {
                    title: "Next task in the capsule"
                    caption: "While nothing is playing and no timer runs, the capsule shows your next task and how long is left."
                    checked: activities.taskInCapsule
                    onToggled: activities.taskInCapsule = !activities.taskInCapsule
                }
                SettingToggle {
                    visible: Qt.platform.os === "windows" || Qt.platform.os === "linux"
                    title: "Discord muted / deafened"
                    caption: !activities.discordAvailable ? "This build has no Discord app, so the island can only show that you are in a Discord call."
                        : activities.discordRpc ? (activities.discordStatus !== "" ? activities.discordStatus : "Starting")
                        : "While you are in a Discord call, show a slashed microphone when you are muted and slashed headphones when you are deafened. Discord asks you to approve Olyzano Notch once; nothing else is read."
                    enabled: activities.discordAvailable
                    checked: activities.discordRpc
                    onToggled: activities.discordRpc = !activities.discordRpc
                }
                }

                SettingCard {
                    title: "Voice assistant"
                    visible: assistant.available
                    SettingToggle {
                        title: "Voice commands"
                        caption: "Press the microphone in the notch and say a command: open an app, search the web, set the volume, start a timer, add a task or a note, control the music. It works offline: the words are recognised on this computer, and the microphone is on only while it listens."
                        checked: assistant.enabled
                        onToggled: assistant.enabled = !assistant.enabled
                    }
                    ColumnLayout {
                        visible: assistant.enabled
                        Layout.fillWidth: true
                        spacing: 8
                        SectionLabel { text: "Model" }
                        Row {
                            spacing: 6
                            Repeater {
                                model: [["base", "Base · better"], ["tiny", "Tiny · faster"]]
                                PillButton {
                                    required property var modelData
                                    text: modelData[1]
                                    checked: assistant.model === modelData[0]
                                    enabled: assistant.modelState !== "downloading"
                                    onClicked: assistant.model = modelData[0]
                                }
                            }
                        }
                        Row {
                            spacing: 8
                            PillButton {
                                visible: assistant.modelState === "missing"
                                text: "Download the model (" + assistant.modelSizeText + ")"
                                iconName: "download"
                                checked: true
                                onClicked: assistant.downloadModel()
                            }
                            PillButton { visible: assistant.modelState === "downloading"; text: "Cancel"; onClicked: assistant.cancelDownload() }
                        }
                        Rectangle {
                            visible: assistant.modelState === "downloading"
                            Layout.fillWidth: true
                            height: 4
                            radius: 2
                            color: theme.hairline
                            Rectangle { width: parent.width * assistant.modelProgress; height: parent.height; radius: 2; color: theme.accent }
                        }
                        Caption {
                            Layout.fillWidth: true
                            color: assistant.modelError !== "" ? theme.danger : theme.textDim
                            text: assistant.modelError !== "" ? assistant.modelError
                                : assistant.modelState === "ready" ? "Ready. The model stays on this computer."
                                : assistant.modelState === "downloading" ? "Downloading… " + Math.round(assistant.modelProgress * 100) + "%"
                                : "The model is a file downloaded once (from Hugging Face). Nothing else is ever sent."
                        }
                        SectionLabel { text: "Language"; Layout.topMargin: 4 }
                        Segmented {
                            Layout.fillWidth: true
                            model: [["auto", "Automatic"], ["id", "Indonesia"], ["en", "English"]]
                            value: assistant.language
                            onPicked: (v) => assistant.language = v
                        }
                        SectionLabel { text: "Search with"; Layout.topMargin: 4 }
                        Segmented {
                            Layout.fillWidth: true
                            model: [["duckduckgo", "DuckDuckGo"], ["google", "Google"], ["bing", "Bing"], ["youtube", "YouTube"]]
                            value: assistant.searchEngine
                            onPicked: (v) => assistant.searchEngine = v
                        }
                        Caption {
                            Layout.fillWidth: true
                            text: "Try: “open the browser”, “open YouTube”, “search channel Nadia on YouTube”, “volume 40”, “timer 10 minutes”, “add task buy milk tomorrow 5”, “pause the music”. Nothing that deletes or shuts down is understood."
                        }
                    }
                }

                SettingCard {
                    title: "Performance"
                    visible: memtune.available
                    SettingToggle {
                        title: "Low memory mode"
                        caption: "Draws with Qt's software renderer instead of the graphics card: Notch then needs roughly 50 MB less memory, but uses a little more processor while the panel moves. Takes effect after a restart."
                        checked: memtune.lowMemory
                        onToggled: memtune.lowMemory = !memtune.lowMemory
                    }
                    RowLayout {
                        visible: memtune.lowMemory !== memtune.running
                        Layout.fillWidth: true
                        spacing: 8
                        Caption { Layout.fillWidth: true; text: memtune.lowMemory ? "Restart Notch to switch to low memory mode." : "Restart Notch to go back to the graphics card." }
                        PillButton { text: "Restart now"; iconName: "undo"; checked: true; onClicked: memtune.restart() }
                    }
                    Caption { visible: memtune.running && memtune.lowMemory; Layout.fillWidth: true; text: "Low memory mode is on." }
                }

                SettingCard {
                    title: "Clipboard"
                    SettingToggle {
                        title: "Remember what I copy"
                        caption: "Keeps the text and pictures you copy on this computer, so you can copy them again from the Clipboard page. Anything you copy is kept, so a password could be too. Off by default."
                        checked: clipboardHistory.enabled
                        onToggled: clipboardHistory.enabled = !clipboardHistory.enabled
                    }
                    ColumnLayout {
                        visible: clipboardHistory.enabled
                        Layout.fillWidth: true
                        spacing: 6
                        SectionLabel { text: "Forget after" }
                        Segmented {
                            Layout.fillWidth: true
                            model: [[24, "1 day"], [168, "7 days"], [0, "Never"]]
                            value: clipboardHistory.maxAgeHours
                            onPicked: (v) => clipboardHistory.maxAgeHours = v
                        }
                        Caption { Layout.fillWidth: true; text: "Pinned items never expire. At most 50 items; text up to 20 KB and pictures up to 5 MB." }
                        PillButton { text: "Clear history"; iconName: "trash"; enabled: clipboardHistory.total > 0; onClicked: clipboardHistory.clear() }
                    }
                }

                SettingCard {
                    title: "Sounds"
                    SettingToggle {
                        title: "Sound effects"
                        caption: "Short sounds for a dropped file, a finished timer, a call, a notification and more."
                        checked: sounds.enabled
                        onToggled: sounds.enabled = !sounds.enabled
                    }
                    DockSlider {
                        visible: sounds.enabled
                        title: "Volume"
                        valueText: sounds.volume + "%"
                        from: 0; to: 100; stepSize: 5
                        value: sounds.volume
                        onMoved: sounds.volume = value
                    }
                    Repeater {
                        // [setting, title, caption, sound to try]
                        model: [["shelf", "Shelf & tools", "Dropping or pasting, emptying the Trash, picking a colour", "drop"],
                                ["timer", "Timer & wellbeing", "Timer and Pomodoro done, stretch reminder, break over", "timer"],
                                ["phone", "Calls & notifications", "Incoming calls, notifications, files sent and received", "ring"],
                                ["system", "System alerts", "Charger, Bluetooth devices, low battery", "plug-in"]]
                        RowLayout {
                            required property var modelData
                            visible: sounds.enabled
                            Layout.fillWidth: true
                            spacing: 8
                            PillButton {
                                text: "▶"
                                onClicked: sounds.preview(parent.modelData[3])
                                Accessible.name: "Play " + parent.modelData[1]
                            }
                            SettingToggle {
                                Layout.fillWidth: true
                                title: parent.modelData[1]
                                caption: parent.modelData[2]
                                checked: sounds[parent.modelData[0]]
                                onToggled: sounds[parent.modelData[0]] = !sounds[parent.modelData[0]]
                            }
                        }
                    }
                    SettingToggle {
                        visible: sounds.enabled
                        title: "Quiet during calls and full-screen apps"
                        caption: "No sounds while a video, game or slideshow fills the screen (a call still rings over a video)."
                        checked: sounds.quietWhenBusy
                        onToggled: sounds.quietWhenBusy = !sounds.quietWhenBusy
                    }
                    SettingToggle {
                        visible: sounds.enabled && sounds.quietWhenBusy
                        title: "Also quiet while the microphone is in use"
                        caption: "Off by default: apps like Discord or a browser keep the microphone open all day, even outside a call."
                        checked: sounds.quietOnMic
                        onToggled: sounds.quietOnMic = !sounds.quietOnMic
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Caption { Layout.fillWidth: true; text: "Olyzano Notch " + Qt.application.version }
                    PillButton { text: "Quit Olyzano Notch"; onClicked: Qt.quit() }
                }
            }
        }

        Loader {
            active: settingsScroll.category === "alerts"
            visible: active // an idle Loader is still a layout item: its spacing would leave a gap
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                Layout.fillWidth: true
                spacing: 16

                SettingToggle {
                    title: "Charger & Bluetooth alerts"
                    caption: "Olyzano Notch peeks out when you plug in, a device connects, or a battery runs low."
                    checked: notch.alerts
                    onToggled: notch.alerts = !notch.alerts
                }

                SettingToggle {
                    title: "Clock when idle"
                    caption: "While nothing else is going on, the notch shows the time, the microphone and the battery."
                    checked: activities.idleClock
                    onToggled: activities.idleClock = !activities.idleClock
                }

                SettingToggle {
                    title: "Downloads in the notch"
                    caption: "Files your browser is still downloading show in the closed notch, with their size and speed."
                    checked: activities.showDownloads
                    onToggled: activities.showDownloads = !activities.showDownloads
                }

                SettingToggle {
                    title: "Phone calls & notifications"
                    caption: "Through KDE Connect: incoming calls, your phone's notifications and a low phone battery show in the notch."
                    checked: activities.phoneAlerts
                    onToggled: activities.phoneAlerts = !activities.phoneAlerts
                }

                SettingToggle {
                    title: "Microphone & camera indicators"
                    caption: "A green dot beside the notch while a camera is on; the open panel names the apps using the camera or the microphone."
                    checked: activities.privacyIndicators
                    onToggled: activities.privacyIndicators = !activities.privacyIndicators
                }
                RowLayout {
                    visible: Qt.platform.os === "linux" && activities.privacyIndicators
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "Voice sensitivity"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                    Repeater {
                        model: [[0, "Low"], [1, "Normal"], [2, "High"]]
                        PillButton { required property var modelData; text: modelData[1]; checked: activities.micSensitivity === modelData[0]; onClicked: activities.micSensitivity = modelData[0] }
                    }
                    Caption { Layout.fillWidth: true; text: "How clearly a voice must stand out from the room before the microphone icon turns green. Lower it if it stays on, raise it if it misses you." }
                }

                SettingCard {
                    id: prayerCard
                    title: "Prayer times"
                    SettingToggle {
                        title: "Prayer time reminders"
                        caption: !prayer.ready ? "Choose your city below. Notch then tells you when each prayer time comes, here in the notch."
                                 : "Calculated here for " + prayer.place + ", offline. May differ by a minute or two from your local authority."
                        checked: prayer.enabled
                        onToggled: prayer.enabled = !prayer.enabled
                    }
                    ColumnLayout {
                        visible: prayer.enabled
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: "City"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                        TextField {
                            id: cityField
                            Layout.fillWidth: true
                            implicitHeight: 30
                            leftPadding: 12
                            topPadding: 0
                            bottomPadding: 0
                            verticalAlignment: TextInput.AlignVCenter
                            placeholderText: prayer.city !== "" ? prayer.city : "Search a city, e.g. Bandung"
                            placeholderTextColor: theme.placeholder
                            color: theme.text
                            font.pixelSize: 12
                            background: Rectangle { radius: theme.radiusMd; color: theme.surface; border.width: 1; border.color: cityField.activeFocus ? theme.accent : "transparent" }
                        }
                        Flickable {
                            visible: cityField.text.trim() !== ""
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(150, cityList.implicitHeight)
                            contentHeight: cityList.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            Column {
                                id: cityList
                                width: parent.width
                                Repeater {
                                    model: prayer.cities.filter(c => (c.name + " " + c.country).toLowerCase().indexOf(cityField.text.trim().toLowerCase()) >= 0).slice(0, 30)
                                    Rectangle {
                                        required property var modelData
                                        width: cityList.width
                                        height: 28
                                        radius: theme.radiusMd
                                        color: cityMouse.containsMouse ? theme.surfaceHover : "transparent"
                                        Text { anchors.verticalCenter: parent.verticalCenter; x: 10; text: modelData.name + " · " + modelData.country; color: theme.text; font.pixelSize: 12 }
                                        MouseArea { id: cityMouse; anchors.fill: parent; hoverEnabled: true; onClicked: { prayer.city = modelData.name; cityField.text = "" } }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Or your own place:"; color: theme.textDim; font.pixelSize: 11 }
                            TextField { id: latField; implicitWidth: 86; implicitHeight: 26; topPadding: 0; bottomPadding: 0; placeholderText: "latitude"; placeholderTextColor: theme.placeholder; color: theme.text; font.pixelSize: 11
                                        background: Rectangle { radius: theme.radiusMd; color: theme.surface } }
                            TextField { id: lonField; implicitWidth: 86; implicitHeight: 26; topPadding: 0; bottomPadding: 0; placeholderText: "longitude"; placeholderTextColor: theme.placeholder; color: theme.text; font.pixelSize: 11
                                        background: Rectangle { radius: theme.radiusMd; color: theme.surface } }
                            PillButton { text: "Use"; enabled: latField.text !== "" && lonField.text !== ""; onClicked: prayer.setCustomPlace(Number(latField.text.replace(",", ".")), Number(lonField.text.replace(",", "."))) }
                        }
                        Text { text: "Method"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: prayer.methods
                                PillButton { required property var modelData; text: modelData.name; checked: prayer.method === modelData.id; onClicked: prayer.method = modelData.id }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text { text: "Asr"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                            PillButton { text: "Shafi'i, Maliki, Hanbali"; checked: prayer.asr === 1; onClicked: prayer.asr = 1 }
                            PillButton { text: "Hanafi"; checked: prayer.asr === 2; onClicked: prayer.asr = 2 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text { text: "Remind me before"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                            Repeater {
                                model: [[0, "Off"], [5, "5 min"], [10, "10 min"], [15, "15 min"]]
                                PillButton { required property var modelData; text: modelData[1]; checked: prayer.beforeMinutes === modelData[0]; onClicked: prayer.beforeMinutes = modelData[0] }
                            }
                        }
                        SettingToggle {
                            title: "Next prayer in the capsule"
                            caption: "While nothing plays and no timer runs, the capsule shows the next prayer and the time left (alternating with the next task)."
                            checked: prayer.inCapsule
                            onToggled: prayer.inCapsule = !prayer.inCapsule
                        }
                        Text { visible: prayer.ready; text: "Today"; color: theme.textDim; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Flow {
                            visible: prayer.ready
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: prayer.today
                                Rectangle {
                                    required property var modelData
                                    implicitWidth: dayCol.implicitWidth + 20
                                    implicitHeight: 38
                                    radius: theme.radiusMd
                                    color: modelData.next ? theme.accentSoft : theme.surface
                                    border.width: modelData.next ? 1 : 0
                                    border.color: theme.accent
                                    Column {
                                        id: dayCol
                                        anchors.centerIn: parent
                                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.time; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.name; color: modelData.prayer ? theme.textDim : theme.textFaint; font.pixelSize: 10 }
                                    }
                                }
                            }
                        }
                    }
                }

                SettingCard {
                    title: "Notifications from this computer"
                    SettingToggle {
                        title: "Show them in the notch"
                        caption: desktopNotifications.enabled && !desktopNotifications.available
                                 ? (Qt.platform.os === "windows" ? "Not available on this Windows install: Windows doesn't let Olyzano Notch read notifications."
                                                                 : "Not available: the session bus doesn't allow watching notifications.")
                                 : "Other apps' notifications show as \"Notification from <app> · <title>\", never the message itself."
                        checked: desktopNotifications.enabled
                        onToggled: desktopNotifications.enabled = !desktopNotifications.enabled
                    }
                }
            }
        }

        Loader {
            active: settingsScroll.category === "files"
            visible: active // an idle Loader is still a layout item: its spacing would leave a gap
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                Layout.fillWidth: true
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
                                text: (shell.categoryName(notch.typeRules[modelData[0]]) || "No category") + "  ▾"
                                checked: shell.categoryName(notch.typeRules[modelData[0]]) !== ""
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
                            color: shell.textSecondary
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
                        color: warn ? theme.danger : theme.textDim
                        text: "Remove all files from the shelf first, then change the folder."
                    }
                }

                SettingToggle {
                    title: "Add new screenshots"
                    caption: notch.linuxDesktop ? "Spectacle screenshots land in the Screenshots category."
                                                : "Screenshots saved to Pictures\\Screenshots (Win + PrtSc) land in the Screenshots category."
                    checked: notch.autoScreenshots
                    onToggled: notch.autoScreenshots = !notch.autoScreenshots
                }

                // Shown under its toggle while Spectacle isn't writing files.
                Rectangle {
                    visible: notch.autoScreenshots && !notch.spectacleAutoSave
                    Layout.fillWidth: true
                    Layout.topMargin: -6
                    implicitHeight: warnRow.implicitHeight + 16
                    radius: theme.radiusMd
                    color: theme.warnBg
                    RowLayout {
                        id: warnRow
                        anchors.fill: parent
                        anchors.margins: 8
                        anchors.leftMargin: 12
                        spacing: 10
                        Caption {
                            Layout.fillWidth: true
                            color: theme.warn
                            text: "Spectacle doesn't save screenshots automatically, so they never reach a folder Olyzano Notch can see."
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
            }
        }

        Loader {
            active: settingsScroll.category === "dock"
            visible: active // an idle Loader is still a layout item: its spacing would leave a gap
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                Layout.fillWidth: true
                spacing: 16

                SettingCard {
                    title: "Dock"
                    SettingToggle {
                        title: "Show the app dock"
                        caption: "Pinned and running apps on an edge of the screen, like the macOS Dock."
                        checked: dock.enabled
                        onToggled: dock.enabled = !dock.enabled
                    }
                    SettingToggle {
                        visible: dock.enabled
                        title: "Always show"
                        caption: dock.autoHide ? "Off: the dock hides past the edge and comes back when the pointer touches it."
                                               : "On: the dock stays on screen."
                        checked: !dock.autoHide
                        onToggled: dock.autoHide = !dock.autoHide
                    }
                }

                SettingCard {
                    visible: dock.enabled
                    title: "Layout"
                    SectionLabel { text: "Position" }
                    Row {
                        spacing: 6
                        Repeater {
                            model: [["bottom", "Bottom"], ["left", "Left"], ["right", "Right"]]
                            PillButton {
                                required property var modelData
                                text: modelData[1]
                                checked: dock.position === modelData[0]
                                onClicked: dock.position = modelData[0]
                            }
                        }
                    }
                    DockSlider {
                        title: "Icon size"
                        valueText: dock.iconSize + " px"
                        from: 32; to: 80; stepSize: 2
                        value: dock.iconSize
                        onMoved: dock.iconSize = value
                    }
                    Caption { Layout.fillWidth: true; text: "The dock's length follows the apps it holds; icon size sets its thickness." }
                }

                SettingCard {
                    visible: dock.enabled
                    title: "Look"
                    DockSlider {
                        title: "Transparency"
                        valueText: Math.round((1 - dock.backgroundOpacity) * 100) + "%"
                        from: 0; to: 0.8; stepSize: 0.05
                        value: 1 - dock.backgroundOpacity
                        onMoved: dock.backgroundOpacity = 1 - value
                    }
                    SettingToggle {
                        title: "Blur behind the dock"
                        caption: desktop.canBlur ? "Frosted glass: what's behind the dock is blurred."
                                                 : "Needs KDE Plasma with its blur effect on (and Olyzano Notch built with KF6WindowSystem)."
                        checked: dock.blur
                        onToggled: dock.blur = !dock.blur
                    }
                }

                SettingCard {
                    visible: dock.enabled
                    title: "Motion"
                    DockSlider {
                        title: "Magnification"
                        valueText: dock.magnification <= 1.001 ? "Off" : "+" + Math.round((dock.magnification - 1) * 100) + "%"
                        from: 1; to: 1.25; stepSize: 0.01
                        value: dock.magnification
                        onMoved: dock.magnification = value
                    }
                    SectionLabel { text: "Animation"; Layout.topMargin: 4 }
                    Row {
                        spacing: 6
                        Repeater {
                            model: [["none", "Off"], ["smooth", "Smooth"], ["bouncy", "Bouncy"]]
                            PillButton {
                                required property var modelData
                                text: modelData[1]
                                checked: dock.animation === modelData[0]
                                onClicked: dock.animation = modelData[0]
                            }
                        }
                    }
                }

                SettingCard {
                    id: dockApps
                    visible: dock.enabled
                    title: "Apps"
                    property bool adding: false
                    property string filter: ""
                    property int revision: 0
                    SettingToggle {
                        title: "Show running apps"
                        caption: "Apps that are open but not kept in the dock appear after the divider."
                        checked: dock.showRunning
                        onToggled: dock.showRunning = !dock.showRunning
                    }
                    SettingToggle {
                        title: "Show Trash"
                        caption: "At the end of the dock: drop files on it to move them to the trash, click to open it."
                        checked: dock.showTrash
                        onToggled: dock.showTrash = !dock.showTrash
                    }
                    SectionLabel { text: "Kept in the dock"; Layout.topMargin: 4 }
                    Caption {
                        Layout.fillWidth: true
                        text: dock.pinnedApps.length === 0 ? "None yet." : "Drag icons on the dock itself to change their order."
                    }
                    Repeater {
                        model: dock.pinnedApps
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10
                            Image {
                                source: modelData.iconSource
                                sourceSize: Qt.size(48, 48)
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: theme.text
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            IconButton {
                                iconName: "close"
                                Accessible.name: "Remove " + modelData.name + " from the dock"
                                onClicked: dock.unpin(modelData.id)
                            }
                        }
                    }
                    PillButton {
                        text: dockApps.adding ? "Done" : "Add apps…"
                        iconName: dockApps.adding ? "" : "plus"
                        onClicked: {
                            dockApps.adding = !dockApps.adding
                            dockApps.filter = ""
                            appSearch.text = ""
                        }
                    }
                    TextField {
                        id: appSearch
                        visible: dockApps.adding
                        Layout.fillWidth: true
                        implicitHeight: 30
                        leftPadding: 12
                        placeholderText: "Search apps"
                        placeholderTextColor: theme.placeholder
                        color: theme.text
                        selectionColor: theme.accent
                        font.pixelSize: 13
                        background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: appSearch.activeFocus ? theme.accent : theme.outline }
                        onTextChanged: dockApps.filter = text
                    }
                    ListView {
                        id: appList
                        visible: dockApps.adding
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: dockApps.adding ? (dockApps.revision, dock.installedApps(dockApps.filter)) : []
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: ItemDelegate {
                            id: appEntry
                            required property var modelData
                            width: appList.width
                            height: 32
                            padding: 4
                            onClicked: {
                                modelData.pinned ? dock.unpin(modelData.id) : dock.pin(modelData.id)
                                dockApps.revision++
                            }
                            background: Rectangle { radius: 8; color: appEntry.hovered ? theme.surfaceHover : "transparent" }
                            contentItem: RowLayout {
                                spacing: 10
                                Image {
                                    source: appEntry.modelData.iconSource
                                    sourceSize: Qt.size(48, 48)
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: appEntry.modelData.name
                                    color: theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    visible: appEntry.modelData.pinned
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

        Loader {
            active: settingsScroll.category === "panel"
            visible: active // an idle Loader is still a layout item: its spacing would leave a gap
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                Layout.fillWidth: true
                spacing: 16

                SettingCard {
                    title: "Status bar"
                    SettingToggle {
                        title: "Show the status bar"
                        caption: "A glass strip along the top edge of every screen: the app in front, the clock and the status icons. The middle stays free for the notch."
                        checked: topbar.enabled
                        onToggled: topbar.enabled = !topbar.enabled
                    }
                    ColumnLayout {
                        visible: topbar.enabled
                        Layout.fillWidth: true
                        spacing: 6
                        SectionLabel { text: "When it shows" }
                        Row {
                            spacing: 6
                            PillButton { text: "Always show"; checked: topbar.behavior === "always"; onClicked: topbar.behavior = "always" }
                            PillButton { text: "Auto-hide"; checked: topbar.behavior === "auto"; onClicked: topbar.behavior = "auto" }
                        }
                        Caption {
                            Layout.fillWidth: true
                            text: topbar.behavior === "auto" ? "It slides away. Touch the top edge of the screen to bring it back."
                                                             : "It stays on screen."
                        }
                    }
                    SettingToggle {
                        visible: topbar.enabled && topbar.behavior === "always"
                        title: "Keep windows out from under it"
                        caption: "Maximised windows stop below the bar. Leave this off if Plasma's own panel is at the top."
                        checked: topbar.reserve
                        onToggled: topbar.reserve = !topbar.reserve
                    }
                }

                SettingCard {
                    visible: topbar.enabled
                    title: "Look"
                    DockSlider {
                        title: "Transparency"
                        valueText: Math.round(topbar.opacity * 100) + "%"
                        from: 20; to: 100; stepSize: 5
                        value: topbar.opacity * 100
                        onMoved: topbar.opacity = value / 100
                    }
                    SettingToggle {
                        title: "Blur behind the bar"
                        caption: "Frosted glass, with KDE's blur effect (Desktop Effects → Blur)."
                        checked: topbar.blur
                        onToggled: topbar.blur = !topbar.blur
                    }
                }

                SettingCard {
                    visible: topbar.enabled
                    title: "What it shows"
                    SettingToggle { title: "The app in front"; caption: "Its name and the title of its window, on the left."; checked: topbar.showApp; onToggled: topbar.showApp = !topbar.showApp }
                    SettingToggle { title: "The app's menu"; caption: "File, Edit, View... of the app in front, beside its name (for apps that share their menu, such as Chrome)."; checked: topbar.showMenu; onToggled: topbar.showMenu = !topbar.showMenu }
                    SettingToggle { title: "Apps in the tray"; caption: "The apps running in the background (Steam, Discord...), left of the status icons. Click to open, right-click for their menu."; checked: topbar.showTray; onToggled: topbar.showTray = !topbar.showTray }
                    SettingToggle { title: "The date"; caption: "Under the time."; checked: topbar.showDate; onToggled: topbar.showDate = !topbar.showDate }
                    SettingToggle { title: "Seconds"; caption: "The clock counts seconds."; checked: topbar.showSeconds; onToggled: topbar.showSeconds = !topbar.showSeconds }
                    SettingToggle { title: "Battery percentage"; caption: "Next to the battery icon."; checked: topbar.showBattery; onToggled: topbar.showBattery = !topbar.showBattery }
                }

                SettingCard {
                    visible: topbar.enabled
                    title: "Quick Settings"
                    SectionLabel { text: "Open them" }
                    Row {
                        spacing: 6
                        PillButton { text: "On click"; checked: topbar.openOn === "click"; onClicked: topbar.openOn = "click" }
                        PillButton { text: "On hover"; checked: topbar.openOn === "hover"; onClicked: topbar.openOn = "hover" }
                    }
                    SectionLabel { text: "What it shows"; Layout.topMargin: 4 }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: [["system", "System", "CPU, RAM, graphics, disk and network"], ["wifi", "Wi-Fi", ""], ["bt", "Bluetooth", ""], ["eth", "Ethernet", ""],
                                    ["volume", "Volume", ""], ["mic", "Microphone", ""], ["brightness", "Brightness", ""], ["power", "Power", ""]]
                            PillButton {
                                required property var modelData
                                text: modelData[1]
                                checked: topbar.qsHidden.indexOf(modelData[0]) < 0
                                onClicked: topbar.setQsShown(modelData[0], topbar.qsHidden.indexOf(modelData[0]) >= 0)
                                ToolTip.visible: hovered && modelData[2] !== ""
                                ToolTip.text: modelData[2]
                            }
                        }
                    }
                    Caption {
                        Layout.fillWidth: true
                        text: "Choose which parts the card has. System shows how busy the computer is (processor, memory, graphics, disk, network); tap a row for details. A part your computer does not have is left out anyway."
                    }
                }

                SettingCard {
                    visible: topbar.enabled
                    title: "App launcher"
                    SettingToggle { title: "Show the launcher button"; caption: "A grid button at the left end of the bar: search and open any app."; checked: topbar.launcher; onToggled: topbar.launcher = !topbar.launcher }
                    SettingToggle {
                        visible: launcher.superKeyAvailable
                        title: "Open with the Windows key"
                        caption: "A tap on the Windows (Super) key opens this launcher instead of Plasma's. Now: "
                                 + (launcher.superKeyStatus === "notch" ? "this launcher." : launcher.superKeyStatus === "plasma" ? "Plasma's launcher." : "another action you set in System Settings.")
                        checked: launcher.superKey
                        onToggled: launcher.superKey = !launcher.superKey
                    }
                    SectionLabel { text: "Show apps as" }
                    Row {
                        spacing: 6
                        PillButton { text: "Grid"; checked: topbar.launcherView === "grid"; onClicked: topbar.launcherView = "grid" }
                        PillButton { text: "List"; checked: topbar.launcherView === "list"; onClicked: topbar.launcherView = "list" }
                    }
                    DockSlider {
                        visible: topbar.launcherView === "grid"
                        title: "Icon size"
                        valueText: topbar.launcherIcon + " px"
                        from: 32; to: 64; stepSize: 4
                        value: topbar.launcherIcon
                        onMoved: topbar.launcherIcon = value
                    }
                    SettingToggle { title: "Recent apps"; caption: "The apps you opened last, above the list."; checked: topbar.launcherRecent; onToggled: topbar.launcherRecent = !topbar.launcherRecent }
                    SettingToggle { title: "Groups"; caption: "Filters such as Internet, Media and Development."; checked: topbar.launcherCategories; onToggled: topbar.launcherCategories = !topbar.launcherCategories }
                    Caption {
                        Layout.fillWidth: true
                        text: "Pin an app with the pin that shows when you point at it; pinned apps come first."
                    }
                }

                SettingCard {
                    visible: topbar.enabled
                    title: "Calendar"
                    SectionLabel { text: "The week starts on" }
                    Row {
                        spacing: 6
                        PillButton { text: "System"; checked: topbar.weekStart === "system"; onClicked: topbar.weekStart = "system" }
                        PillButton { text: "Monday"; checked: topbar.weekStart === "monday"; onClicked: topbar.weekStart = "monday" }
                        PillButton { text: "Sunday"; checked: topbar.weekStart === "sunday"; onClicked: topbar.weekStart = "sunday" }
                    }
                    SettingToggle { title: "Week numbers"; caption: "A column with the week of the year."; checked: topbar.weekNumbers; onToggled: topbar.weekNumbers = !topbar.weekNumbers }
                    Caption {
                        Layout.fillWidth: true
                        text: "Click the clock on the bar to open it."
                    }
                }
            }
        }

        Loader {
            active: settingsScroll.category === "about"
            visible: active // an idle Loader is still a layout item: its spacing would leave a gap
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                onVisibleChanged: if (visible) updates.loadContributors()
                Layout.fillWidth: true
                spacing: 16

                SettingCard {
                    title: "Olyzano Notch"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Image {
                            Layout.preferredWidth: 56; Layout.preferredHeight: 56
                            source: "icons/notch.png"
                            sourceSize: Qt.size(112, 112)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: "Olyzano Notch"; color: theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                            Text { text: "Version " + Qt.application.version; color: theme.textDim; font.pixelSize: 12 }
                            Text { text: "A Dynamic-Island style notch for your files, music and desktop."; color: theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                    }
                }

                SettingCard {
                    title: "Updates"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 10; Layout.preferredHeight: 10; radius: 5
                            color: updates.checking ? theme.textFaint : updates.newer && !updates.skipped ? theme.accent : theme.success
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text { Layout.fillWidth: true; text: updates.status; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium; wrapMode: Text.Wrap }
                            Text { text: "You have " + updates.installed + " · last checked " + updates.lastChecked; color: theme.textDim; font.pixelSize: 11 }
                        }
                        PillButton { text: updates.checking ? "Checking…" : "Check now"; iconName: "search"; enabled: !updates.checking; onClicked: updates.check() }
                    }
                    ColumnLayout { // what the new version brings
                        visible: updates.newer
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: "What's new in " + updates.latest; color: theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: Math.min(notesText.implicitHeight + 20, 190)
                            radius: theme.radiusMd
                            color: theme.well
                            clip: true
                            Flickable {
                                anchors { fill: parent; margins: 10 }
                                contentHeight: notesText.implicitHeight
                                boundsBehavior: Flickable.StopAtBounds
                                Text { id: notesText; width: parent.width; text: updates.notes.replace(/\[([^\]]+)\]\([^)]*\)/g, "$1").replace(/^#+\s*/gm, "").replace(/^[ \t]*[-*][ \t]+/gm, "• ").replace(/[*`]/g, ""); color: theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                            }
                        }
                        // one-click update: download, check the file, install, start the new version
                        ColumnLayout {
                            visible: updates.canInstall || updates.installing || updates.installError !== ""
                            Layout.fillWidth: true
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                PillButton {
                                    visible: !updates.installing
                                    text: "Update now"
                                    iconName: "download"
                                    checked: true
                                    enabled: updates.canInstall
                                    onClicked: updates.installNow()
                                }
                                PillButton { visible: updates.installing && updates.progress < 1; text: "Cancel"; onClicked: updates.cancelInstall() }
                                Caption { visible: updates.installing; Layout.fillWidth: true; text: updates.stage }
                            }
                            Rectangle {
                                visible: updates.installing
                                Layout.fillWidth: true
                                height: 4
                                radius: 2
                                color: theme.hairline
                                Rectangle { width: parent.width * updates.progress; height: parent.height; radius: 2; color: theme.accent }
                            }
                            Text { visible: updates.installError !== ""; Layout.fillWidth: true; text: updates.installError; color: theme.danger; font.pixelSize: 12; wrapMode: Text.Wrap }
                            Caption {
                                visible: !updates.installing && updates.canInstall
                                Layout.fillWidth: true
                                text: Qt.platform.os === "windows" ? "Downloads the installer, checks it and installs it, then Notch starts again."
                                                                   : "Downloads the package, checks it, and asks for your password to install it, then Notch starts again."
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            PillButton { visible: !updates.canInstall; text: "Download"; iconName: "download"; checked: true; onClicked: updates.download() }
                            PillButton { visible: updates.canInstall; text: "Download only"; iconName: "download"; onClicked: updates.download() }
                            PillButton { text: "Release page"; iconName: "external"; onClicked: Qt.openUrlExternally(updates.page) }
                            PillButton { visible: !updates.skipped; text: "Remind me in a week"; onClicked: updates.remindLater() }
                            PillButton { text: updates.skipped ? "Don't skip it" : "Skip this version"; onClicked: updates.skipped ? updates.unskip() : updates.skip() }
                        }
                        Caption { Layout.fillWidth: true; text: "Download opens the file for this system in your browser. Notch does not install anything by itself: close it, install, and open it again." }
                    }
                    SettingToggle {
                        title: "Check for updates automatically"
                        caption: "Once a day, Notch asks GitHub whether a newer release is out. It is an anonymous request: GitHub sees your IP address and the version of Notch, nothing else. Turn it off and nothing is ever sent."
                        checked: updates.enabled
                        onToggled: updates.enabled = !updates.enabled
                    }
                }

                SettingCard {
                    title: "Author"
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Made by"; color: theme.textDim; font.pixelSize: 12 }
                        Text { text: "Kevien.OJ (Kepin17)"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                        Item { Layout.fillWidth: true }
                        Text { text: "MIT License"; color: theme.textDim; font.pixelSize: 12 }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        PillButton { text: "Web demo"; iconName: "external"; checked: true; onClicked: Qt.openUrlExternally("https://kepin17.github.io/olyzano-notch/") }
                        PillButton { text: "Source code"; iconName: "external"; onClicked: Qt.openUrlExternally("https://github.com/Kepin17/olyzano-notch") }
                        PillButton { text: "Releases"; iconName: "external"; onClicked: Qt.openUrlExternally("https://github.com/Kepin17/olyzano-notch/releases") }
                        PillButton { text: "Report a problem"; iconName: "external"; onClicked: Qt.openUrlExternally("https://github.com/Kepin17/olyzano-notch/issues") }
                    }
                }

                SettingCard {
                    title: "Contribute"
                    Caption { Layout.fillWidth: true; text: "Olyzano Notch is open source and welcomes help: code, translations, ideas, bug reports. Fork the project, make your change on a branch and send a pull request." }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        PillButton { text: "How to contribute"; iconName: "external"; checked: true; onClicked: Qt.openUrlExternally("https://github.com/Kepin17/olyzano-notch#contributing") }
                        PillButton { text: "Developer guide"; iconName: "external"; onClicked: Qt.openUrlExternally("https://github.com/Kepin17/olyzano-notch/blob/main/docs/developer-guide.md") }
                        PillButton { text: "Open issues"; iconName: "external"; onClicked: Qt.openUrlExternally("https://github.com/Kepin17/olyzano-notch/issues") }
                        PillButton { text: "Send a pull request"; iconName: "external"; onClicked: Qt.openUrlExternally("https://github.com/Kepin17/olyzano-notch/pulls") }
                    }
                }

                SettingCard {
                    title: "Thanks to the contributors"
                    visible: updates.contributors.length > 0
                    Caption { Layout.fillWidth: true; text: "People who sent code to Olyzano Notch on GitHub. Thank you! Click a name to open their profile." }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: updates.contributors
                            PillButton { text: modelData; iconName: "external"; onClicked: Qt.openUrlExternally("https://github.com/" + modelData) }
                        }
                    }
                }

                SettingCard {
                    title: "Buy me a coffee"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        Rectangle { // a white mat around the code, so it scans in dark themes too
                            Layout.preferredWidth: 156; Layout.preferredHeight: 156
                            radius: 10
                            color: "white"
                            Image {
                                anchors.centerIn: parent
                                width: 144; height: 144
                                source: "images/traktir.jpeg"
                                sourceSize: Qt.size(288, 288)
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Enjoying Olyzano Notch?"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                            Text {
                                Layout.fillWidth: true
                                text: "It is free and open source. If it helps you, you can buy me a coffee: scan the QR code with your banking or e-wallet app. Thank you!"
                                color: theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                SettingCard {
                    id: compatCard
                    visible: Qt.platform.os === "windows"
                    title: "RivaTuner / MSI Afterburner"
                    property bool hooked: false
                    Component.onCompleted: hooked = notch.rtssHooked()
                    Connections { target: settingsScroll; function onCategoryChanged() { compatCard.hooked = notch.rtssHooked() } }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: compatCard.hooked ? theme.warn : theme.textDim
                        font.pixelSize: 12
                        text: compatCard.hooked ? "RivaTuner (RTSS) is hooked into Olyzano Notch right now, so its overlay can show on the notch as if it were a game."
                                                : "RivaTuner (RTSS, also inside MSI Afterburner) treats any app that draws with the GPU as a game. If its overlay shows on Olyzano Notch, tell it to ignore Notch once:"
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: theme.text
                        font.pixelSize: 12
                        text: "1. Open RivaTuner Statistics Server.\n2. Click the + button (bottom left) and pick notch.exe (copy its path below).\n3. Set Application detection level to None for it."
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        PillButton { text: "Copy path of notch.exe"; iconName: "copy"; onClicked: notch.copyText(notch.exePath()) }
                        PillButton { text: "Open the guide"; iconName: "external"; onClicked: Qt.openUrlExternally("https://github.com/Kepin17/olyzano-notch#rivatuner--msi-afterburner-shows-its-overlay-on-notch") }
                    }
                }

                SettingCard {
                    title: "Problems?"
                    RowLayout {
                        Layout.fillWidth: true
                        Caption { Layout.fillWidth: true; text: "Copy the version and system details to paste into a bug report." }
                        PillButton {
                            text: "Copy details"
                            iconName: "copy"
                            onClicked: notch.copyText("Olyzano Notch " + Qt.application.version + " · " + Qt.platform.os + " · " + Qt.platform.pluginName)
                        }
                    }
                }
            }
        }

        Loader {
            active: settingsScroll.category === "wellbeing"
            visible: active // an idle Loader is still a layout item: its spacing would leave a gap
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                Layout.fillWidth: true
                spacing: 16

                SettingCard {
                    title: "Stretch reminder"
                    SettingToggle {
                        title: "Remind me to take breaks"
                        caption: wellbeing.idleAvailable
                                 ? "Counts the time you actually work (mouse and keyboard). Stepping away resets it. You decide each time: break, later or skip."
                                 : "This desktop doesn't report idle time, so the reminder goes by the clock. Stepping away isn't noticed."
                        checked: wellbeing.enabled
                        onToggled: wellbeing.enabled = !wellbeing.enabled
                    }
                    MinutePicker {
                        visible: wellbeing.enabled
                        label: "Remind after working for"
                        choices: [30, 45, 50, 60, 90]
                        value: wellbeing.workMinutes
                        onPicked: (m) => wellbeing.workMinutes = m
                    }
                    MinutePicker {
                        visible: wellbeing.enabled
                        label: "Break length"
                        choices: [2, 5, 10]
                        value: wellbeing.breakMinutes
                        onPicked: (m) => wellbeing.breakMinutes = m
                    }
                    MinutePicker {
                        visible: wellbeing.enabled && wellbeing.idleAvailable
                        label: "Away this long counts as a break"
                        choices: [2, 5, 10]
                        value: wellbeing.idleMinutes
                        onPicked: (m) => wellbeing.idleMinutes = m
                    }
                    MinutePicker {
                        visible: wellbeing.enabled
                        label: "\"Later\" asks again after"
                        choices: [5, 10, 15]
                        value: wellbeing.snoozeMinutes
                        onPicked: (m) => wellbeing.snoozeMinutes = m
                    }
                }

                SettingCard {
                    visible: wellbeing.enabled
                    title: "Don't interrupt"
                    SettingToggle {
                        title: "While a video or music plays"
                        checked: wellbeing.pauseForMedia
                        onToggled: wellbeing.pauseForMedia = !wellbeing.pauseForMedia
                    }
                    SettingToggle {
                        title: "During a call"
                        caption: "While the microphone or a camera is in use, or the phone rings."
                        checked: wellbeing.pauseForCalls
                        onToggled: wellbeing.pauseForCalls = !wellbeing.pauseForCalls
                    }
                    SettingToggle {
                        title: "While a Pomodoro runs"
                        caption: "It has its own breaks."
                        checked: wellbeing.pauseForPomodoro
                        onToggled: wellbeing.pauseForPomodoro = !wellbeing.pauseForPomodoro
                    }
                }

                SettingCard {
                    title: "Eyes"
                    SettingToggle {
                        title: "20-20-20 reminder"
                        caption: "Every 20 minutes of work, a small note to look at something far away for 20 seconds."
                        checked: wellbeing.eyeReminder
                        onToggled: wellbeing.eyeReminder = !wellbeing.eyeReminder
                    }
                }

                SettingCard {
                    title: "Doomscrolling"
                    SettingToggle {
                        title: "Warn me about endless scrolling"
                        caption: "Notices when Instagram, TikTok or YouTube Shorts is in front while you use the computer, and speaks up when it adds up. Everything stays here: only the kind of site is kept, never what you look at."
                        checked: doomscroll.enabled
                        onToggled: doomscroll.enabled = !doomscroll.enabled
                    }
                    SectionLabel { visible: doomscroll.enabled; text: "Count" }
                    Flow {
                        visible: doomscroll.enabled
                        Layout.fillWidth: true
                        spacing: 6
                        PillButton { text: "Instagram"; checked: doomscroll.instagram; onClicked: doomscroll.instagram = !doomscroll.instagram }
                        PillButton { text: "TikTok"; checked: doomscroll.tiktok; onClicked: doomscroll.tiktok = !doomscroll.tiktok }
                        PillButton { text: "YouTube Shorts"; checked: doomscroll.shorts; onClicked: doomscroll.shorts = !doomscroll.shorts }
                        PillButton { text: "Facebook Reels"; checked: doomscroll.facebook; onClicked: doomscroll.facebook = !doomscroll.facebook }
                    }
                    DockSlider {
                        visible: doomscroll.enabled
                        title: "Warn after"
                        valueText: doomscroll.limitMinutes + " min"
                        from: 5; to: 60; stepSize: 5
                        value: doomscroll.limitMinutes
                        onMoved: doomscroll.limitMinutes = value
                    }
                    DockSlider {
                        visible: doomscroll.enabled
                        title: "Or after this many videos"
                        valueText: doomscroll.swipeLimit
                        from: 10; to: 100; stepSize: 5
                        value: doomscroll.swipeLimit
                        onMoved: doomscroll.swipeLimit = value
                    }
                    DockSlider {
                        visible: doomscroll.enabled
                        title: "Ask again every"
                        valueText: doomscroll.repeatMinutes + " min"
                        from: 3; to: 30; stepSize: 1
                        value: doomscroll.repeatMinutes
                        onMoved: doomscroll.repeatMinutes = value
                    }
                    TextField {
                        visible: doomscroll.enabled
                        Layout.fillWidth: true
                        implicitHeight: 32
                        leftPadding: 12
                        placeholderText: "Other words in a window's title, separated by commas"
                        placeholderTextColor: theme.placeholder
                        text: doomscroll.custom
                        color: theme.text
                        selectionColor: theme.accent
                        font.pixelSize: 12
                        background: Rectangle { radius: 16; color: theme.well; border.width: 1; border.color: parent.activeFocus ? theme.accent : theme.outline }
                        onEditingFinished: doomscroll.custom = text
                    }
                    Caption {
                        visible: doomscroll.enabled
                        Layout.fillWidth: true
                        text: "Notch cannot see your scrolling itself: it goes by the window in front, and Shorts are told apart by their length. Instagram counts as a whole."
                    }
                    RowLayout {
                        visible: doomscroll.enabled
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: {
                                const t = doomscroll.today
                                const parts = []
                                for (const k of [["instagram", "Instagram"], ["tiktok", "TikTok"], ["shorts", "Shorts"], ["facebook", "Facebook"], ["custom", "Other"]])
                                    if (t[k[0]] > 0) parts.push(k[1] + " " + t[k[0]])
                                return "Today: " + t.total + " min" + (parts.length ? " · " + parts.join(" · ") : "")
                            }
                            color: theme.text
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                        PillButton { text: "Reset today"; onClicked: doomscroll.resetToday() }
                    }
                }

                SettingCard {
                    title: "Today"
                    Text {
                        text: {
                            const minutes = Math.round(wellbeing.todayWorkedMs / 60000)
                            const worked = minutes >= 60 ? Math.floor(minutes / 60) + " h " + (minutes % 60) + " min" : minutes + " min"
                            return "Worked " + worked + " · " + wellbeing.todayBreaks + (wellbeing.todayBreaks === 1 ? " break" : " breaks")
                        }
                        color: theme.text
                        font.pixelSize: 13
                    }
                }
            }
        }

        Loader {
            active: settingsScroll.category === "sharing"
            visible: active // an idle Loader is still a layout item: its spacing would leave a gap
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                Layout.fillWidth: true
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    SectionLabel { text: "Nearby" }
                    Caption {
                        Layout.fillWidth: true
                        text: "The name other Olyzano Notch computers see while you have Receive switched on in the Share window."
                    }
                    TextField {
                        id: deviceField
                        Layout.fillWidth: true
                        implicitHeight: 30
                        leftPadding: 12
                        text: nearby.deviceName
                        placeholderTextColor: theme.placeholder
                        color: theme.text
                        selectionColor: theme.accent
                        font.pixelSize: 13
                        background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: deviceField.activeFocus ? theme.accent : theme.outline }
                        onEditingFinished: nearby.deviceName = text
                    }
                }

                ColumnLayout {
                    id: sharing
                    Layout.fillWidth: true
                    spacing: 8
                    property bool manual: false
                    function expired(m) { return m.expiresAt > 0 && m.expiresAt * 1000 < Date.now() }
                    function access(m) { // what the host should know about a member's access
                        if (!m.joined) return "Waiting to join" + (m.accessHours > 0 ? " · " + sharing.hours(m.accessHours) + " once joined" : "")
                        if (m.expiresAt <= 0) return "Joined · no end"
                        if (expired(m)) return "Access ended"
                        const left = Math.ceil((m.expiresAt * 1000 - Date.now()) / 3600000)
                        return "Joined · " + (left >= 48 ? Math.ceil(left / 24) + " days left" : left + " h left")
                    }
                    function hours(h) { return h % 24 === 0 ? (h / 24) + (h === 24 ? " day" : " days") : h + " h" }
                    function mb(bytes) { return bytes >= 10 * 1048576 ? Math.round(bytes / 1048576) + " MB" : (bytes / 1048576).toFixed(1) + " MB" }

                    RowLayout {
                        Layout.fillWidth: true
                        SectionLabel { Layout.fillWidth: true; text: "Sharing servers" }
                        PillButton { text: "How to set up a server"; iconName: "info"; onClicked: serverGuide.open() }
                    }
                    Caption {
                        Layout.fillWidth: true
                        text: "Share via QR can use a server you or your team run. Files are encrypted on this computer first. Ask the host for a key, or set up your own: the guide shows how, step by step."
                    }
                    Caption { Layout.fillWidth: true; visible: share.servers.length === 0; text: "No server yet." }

                    Repeater {
                        model: share.servers
                        delegate: Rectangle {
                            id: srv
                            required property var modelData
                            readonly property color tone: !modelData.tested ? theme.textDim : modelData.ok ? theme.success : theme.danger
                            Layout.fillWidth: true
                            implicitHeight: srvBody.implicitHeight + 28
                            radius: theme.radiusMd + 2
                            color: theme.card
                            border.width: 1
                            border.color: theme.hairline

                            ColumnLayout {
                                id: srvBody
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
                                spacing: 12
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Rectangle {
                                        Layout.preferredWidth: 36
                                        Layout.preferredHeight: 36
                                        radius: 10
                                        color: Qt.alpha(theme.accent, 0.14)
                                        LineIcon { anchors.centerIn: parent; glyph: "server"; sourceSize: Qt.size(18, 18); color: theme.accentText }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 1
                                        RowLayout {
                                            spacing: 6
                                            Text { Layout.fillWidth: true; text: srv.modelData.name; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                            Text { visible: srv.modelData.isDefault && share.servers.length > 1; text: "Default"; color: theme.accentText; font.pixelSize: 11 }
                                        }
                                        Text { Layout.fillWidth: true; text: srv.modelData.url.replace(/^https?:\/\//, ""); color: theme.textDim; font.pixelSize: 11; elide: Text.ElideMiddle }
                                    }
                                    Rectangle { // how the last test went
                                        implicitWidth: statusRow.implicitWidth + 16
                                        implicitHeight: 22
                                        radius: 11
                                        color: Qt.alpha(srv.tone, 0.12)
                                        Row {
                                            id: statusRow
                                            anchors.centerIn: parent
                                            spacing: 5
                                            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 6; height: 6; radius: 3; color: srv.tone }
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                font.pixelSize: 11
                                                color: srv.tone
                                                text: !srv.modelData.tested ? "Checking…" : !srv.modelData.ok ? "Problem" : srv.modelData.owner ? "Host" : "Connected"
                                            }
                                        }
                                    }
                                    IconButton {
                                        iconName: "undo"
                                        Accessible.name: "Test connection"
                                        ToolTip.visible: hovered
                                        ToolTip.delay: 400
                                        ToolTip.text: "Test connection"
                                        onClicked: share.testServer(srv.modelData.id)
                                    }
                                    IconButton {
                                        iconName: "trash"
                                        Accessible.name: "Remove this server"
                                        ToolTip.visible: hovered
                                        ToolTip.delay: 400
                                        ToolTip.text: "Remove this server"
                                        onClicked: share.removeServer(srv.modelData.id)
                                    }
                                }
                                // Space used on the server, as a thin bar
                                ColumnLayout {
                                    visible: srv.modelData.ok === true
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Rectangle {
                                        visible: srv.modelData.quota > 0
                                        Layout.fillWidth: true
                                        implicitHeight: 4
                                        radius: 2
                                        color: theme.well
                                        Rectangle {
                                            width: parent.width * Math.min(1, srv.modelData.used / Math.max(1, srv.modelData.quota))
                                            height: parent.height
                                            radius: 2
                                            color: srv.modelData.used > srv.modelData.quota * 0.9 ? theme.danger : theme.accent
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Text {
                                            Layout.fillWidth: true
                                            color: theme.textDim
                                            font.pixelSize: 11
                                            wrapMode: Text.Wrap
                                            text: sharing.mb(srv.modelData.used) + (srv.modelData.quota ? " of " + sharing.mb(srv.modelData.quota) : "") + " used"
                                                  + (srv.modelData.maxActive ? " · " + srv.modelData.active + " of " + srv.modelData.maxActive + " shares" : "")
                                                  + " · files up to " + sharing.mb(srv.modelData.maxFile)
                                        }
                                        PillButton {
                                            visible: share.servers.length > 1 && !srv.modelData.isDefault
                                            text: "Make default"
                                            onClicked: share.defaultServerId = srv.modelData.id
                                        }
                                    }
                                }
                                Text {
                                    visible: srv.modelData.tested && !srv.modelData.ok
                                    Layout.fillWidth: true
                                    text: srv.modelData.message
                                    color: theme.danger
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }
                                Rectangle { visible: srv.modelData.owner === true; Layout.fillWidth: true; implicitHeight: 1; color: theme.hairline }

                                // The host's part: members and their keys, all in one window.
                                ColumnLayout {
                                    id: team
                                    visible: srv.modelData.owner === true
                                    Layout.fillWidth: true
                                    spacing: 4
                                    property var list: []
                                    property var links: ({})  // member name -> the key made in this session (a key is shown once)
                                    property int space: 1024  // MB of server space for the new member (5 GB at most)
                                    property int hours: 720   // how long a new member's access lasts after they join; 0 = never ends
                                    property string note: ""
                                    property int total: 0
                                    property bool loading: false
                                    onVisibleChanged: if (visible) share.loadMembers(srv.modelData.id)
                                    Component.onCompleted: if (visible) share.loadMembers(srv.modelData.id)
                                    Connections {
                                        target: share
                                        function onMembersLoaded(id, list, offset, total) { // pages arrive newest first; the next one is asked for when the list is scrolled to its end
                                            if (id !== srv.modelData.id) return
                                            team.list = offset === 0 ? list : team.list.concat(list)
                                            team.total = total
                                            team.loading = false
                                        }
                                        function onMemberLink(id, name, ok, text) {
                                            if (id !== srv.modelData.id) return
                                            if (!ok) { team.note = text; return }
                                            const l = Object.assign({}, team.links)
                                            l[name] = text
                                            team.links = l
                                            notch.copyText(text) // the newest key is already on the clipboard
                                            memberName.clear()
                                            if (!keysPopup.opened) keysPopup.open()
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.topMargin: 4
                                        spacing: 6
                                        Text { text: "Members"; color: theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                                        Text { visible: team.total > 0; text: team.total; color: theme.textDim; font.pixelSize: 11 }
                                        Item { Layout.fillWidth: true }
                                        PillButton { text: team.total > 0 ? "Manage" : "Add member"; iconName: team.total > 0 ? "" : "plus"; checked: true; onClicked: keysPopup.open() }
                                    }

                                    Popup {
                                        id: keysPopup
                                        parent: settingsScroll // inside the panel, not across the whole screen
                                        x: (settingsScroll.width - width) / 2
                                        y: Math.max(0, (settingsScroll.height - height) / 2)
                                        modal: true
                                        dim: false
                                        focus: true
                                        padding: 16
                                        width: Math.min(440, settingsScroll.width - 24)
                                        onOpened: { settingsScroll.keysOpen = true; memberName.forceActiveFocus() }
                                        onClosed: { settingsScroll.keysOpen = false; team.note = "" }
                                        Overlay.modal: Item {} // no dark layer behind it
                                        background: Rectangle { radius: 16; color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 1); border.color: theme.outline }
                                        contentItem: ColumnLayout {
                                            spacing: 10
                                            RowLayout {
                                                Layout.fillWidth: true
                                                SectionLabel { Layout.fillWidth: true; text: "Members of " + srv.modelData.name }
                                                IconButton { iconName: "close"; Accessible.name: "Close"; onClicked: keysPopup.close() }
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 6
                                                TextField {
                                                    id: memberName
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 0
                                                    implicitHeight: 30
                                                    leftPadding: 12
                                                    placeholderText: "New username, e.g. alice"
                                                    placeholderTextColor: theme.placeholder
                                                    color: theme.text
                                                    selectionColor: theme.accent
                                                    font.pixelSize: 13
                                                    maximumLength: 40
                                                    background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: memberName.activeFocus ? theme.accent : theme.outline }
                                                    onAccepted: createKey.clicked()
                                                    Keys.onEscapePressed: keysPopup.close()
                                                }
                                                PillButton {
                                                    id: createKey
                                                    text: "Create key"
                                                    iconName: "plus"
                                                    checked: true
                                                    enabled: memberName.text.trim() !== ""
                                                    onClicked: { team.note = ""; share.createMember(srv.modelData.id, memberName.text, team.hours, team.space) }
                                                }
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8
                                                Caption { Layout.preferredWidth: 48; text: "Space" }
                                                Segmented {
                                                    Layout.fillWidth: true
                                                    model: [[512, "500 MB"], [1024, "1 GB"], [2048, "2 GB"], [5120, "5 GB"]]
                                                    value: team.space
                                                    onPicked: (v) => team.space = v
                                                }
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8
                                                Caption { Layout.preferredWidth: 48; text: "Access" }
                                                Segmented {
                                                    Layout.fillWidth: true
                                                    model: [[24, "1 day"], [168, "7 days"], [720, "30 days"], [0, "No end"]]
                                                    value: team.hours
                                                    onPicked: (v) => team.hours = v
                                                }
                                            }
                                            Caption { Layout.fillWidth: true; text: "Access lasts that long after they join. Send each key to its person: it works once, for 7 days." }
                                            Text { visible: team.note !== ""; Layout.fillWidth: true; text: team.note; color: theme.danger; font.pixelSize: 12; wrapMode: Text.Wrap }
                                            ListView {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: Math.min(contentHeight, 5 * 44) // five rows, then it scrolls
                                                visible: team.list.length > 0
                                                clip: true
                                                spacing: 0
                                                boundsBehavior: Flickable.StopAtBounds
                                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                                // near the end: fetch the next page by itself, no button
                                                onContentYChanged: if (!team.loading && team.list.length < team.total && contentY + height > contentHeight - 88) {
                                                    team.loading = true
                                                    share.loadMembers(srv.modelData.id, team.list.length)
                                                }
                                                model: team.list
                                                delegate: Item {
                                                    id: row
                                                    required property var modelData
                                                    property bool copied: false
                                                    readonly property string key: team.links[modelData.name] || ""
                                                    readonly property bool ended: modelData.joined && sharing.expired(modelData)
                                                    width: ListView.view.width
                                                    height: 44
                                                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.hairline }
                                                    RowLayout {
                                                        anchors.fill: parent
                                                        spacing: 10
                                                        Rectangle { // joined / waiting / ended
                                                            Layout.preferredWidth: 7
                                                            Layout.preferredHeight: 7
                                                            radius: 4
                                                            color: row.ended ? theme.danger : row.modelData.joined ? theme.success : theme.textFaint
                                                        }
                                                        ColumnLayout {
                                                            Layout.fillWidth: true
                                                            Layout.minimumWidth: 0
                                                            spacing: 0
                                                            Text { Layout.fillWidth: true; text: row.modelData.name; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight }
                                                            Text {
                                                                Layout.fillWidth: true
                                                                color: theme.textDim
                                                                font.pixelSize: 11
                                                                elide: Text.ElideRight
                                                                text: sharing.access(row.modelData) + " · " + sharing.mb(row.modelData.used) + " / " + sharing.mb(row.modelData.quota)
                                                            }
                                                        }
                                                        IconButton {
                                                            visible: row.key !== ""
                                                            iconName: row.copied ? "check" : "copy"
                                                            checked: row.copied
                                                            Accessible.name: "Copy key"
                                                            ToolTip.visible: hovered
                                                            ToolTip.delay: 400
                                                            ToolTip.text: row.copied ? "Copied" : "Copy key"
                                                            onClicked: { notch.copyText(row.key); row.copied = true }
                                                        }
                                                        IconButton {
                                                            iconName: "undo"
                                                            Accessible.name: "New key"
                                                            ToolTip.visible: hovered
                                                            ToolTip.delay: 400
                                                            ToolTip.text: "New key (the old one stops working)"
                                                            onClicked: { row.copied = false; share.reinviteMember(srv.modelData.id, row.modelData.name) }
                                                        }
                                                        IconButton {
                                                            iconName: "trash"
                                                            Accessible.name: "Revoke " + row.modelData.name
                                                            ToolTip.visible: hovered
                                                            ToolTip.delay: 400
                                                            ToolTip.text: "Revoke access"
                                                            onClicked: share.revokeMember(srv.modelData.id, row.modelData.name)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Someone gave you a key: paste it here and this computer joins their server under the name they chose.
                    SectionLabel { text: "Join a server" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        TextField {
                            id: joinField
                            Layout.fillWidth: true
                            implicitHeight: 30
                            leftPadding: 12
                            placeholderText: "Paste the key from the host (https://…/join#…)"
                            placeholderTextColor: theme.placeholder
                            color: theme.text
                            selectionColor: theme.accent
                            font.pixelSize: 13
                            background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: joinField.activeFocus ? theme.accent : theme.outline }
                            onAccepted: joinButton.clicked()
                        }
                        PillButton {
                            id: joinButton
                            text: "Join"
                            checked: true
                            enabled: joinField.text.trim() !== ""
                            onClicked: { joinResult.text = ""; share.joinServer(joinField.text) }
                        }
                    }
                    Caption {
                        id: joinResult
                        Layout.fillWidth: true
                        visible: text !== ""
                        Connections {
                            target: share
                            function onJoined(ok, message) {
                                joinResult.text = message
                                joinResult.color = ok ? theme.success : theme.danger
                                if (ok) joinField.clear()
                            }
                        }
                    }

                    // A one-page guide: what the server is, and the steps to run it. Commands copy with a click.
                    Popup {
                        id: serverGuide
                        parent: settingsScroll
                        x: (settingsScroll.width - width) / 2
                        y: Math.max(0, (settingsScroll.height - height) / 2)
                        width: Math.min(560, settingsScroll.width - 24)
                        height: Math.min(520, settingsScroll.height - 16)
                        modal: true
                        dim: false
                        focus: true
                        padding: 16
                        onOpened: settingsScroll.guideOpen = true
                        onClosed: settingsScroll.guideOpen = false
                        Overlay.modal: Item {}
                        background: Rectangle { radius: 16; color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 1); border.color: theme.outline }
                        component Step: RowLayout {
                            property int n
                            property string title
                            property string body
                            Layout.fillWidth: true
                            spacing: 10
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 22; Layout.preferredHeight: 22
                                radius: 11
                                color: Qt.alpha(theme.accent, 0.2)
                                Text { anchors.centerIn: parent; text: n; color: theme.accentText; font.pixelSize: 12; font.weight: Font.DemiBold }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { Layout.fillWidth: true; text: title; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
                                Text { Layout.fillWidth: true; visible: body !== ""; text: body; color: theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap }
                            }
                        }
                        component Cmd: Rectangle {
                            property string code
                            Layout.fillWidth: true
                            Layout.leftMargin: 32
                            implicitHeight: cmdText.implicitHeight + 16
                            radius: 8
                            color: theme.well
                            border.width: 1
                            border.color: theme.hairline
                            Text {
                                id: cmdText
                                anchors { left: parent.left; right: copyBtn.left; verticalCenter: parent.verticalCenter; leftMargin: 10; rightMargin: 6 }
                                text: code
                                color: theme.text
                                font.family: "monospace"
                                font.pixelSize: 11
                                wrapMode: Text.WrapAnywhere
                            }
                            IconButton {
                                id: copyBtn
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 4 }
                                iconName: "copy"
                                Accessible.name: "Copy"
                                ToolTip.visible: hovered
                                ToolTip.text: "Copy"
                                onClicked: notch.copyText(code)
                            }
                        }
                        contentItem: ColumnLayout {
                            spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                SectionLabel { Layout.fillWidth: true; text: "Run your own share server" }
                                IconButton { iconName: "close"; Accessible.name: "Close"; onClicked: serverGuide.close() }
                            }
                            ScrollView {
                                id: guideScroll
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: guideScroll.availableWidth - 12
                                    spacing: 10
                                    Caption { Layout.fillWidth: true; text: "The server only keeps files that were already encrypted on your computer, and deletes them when they expire; it can never read them. You need a small always-on computer with a domain name: a cheap VPS (Linux) is the usual choice, and Docker." }
                                    Step { n: 1; title: "Point a domain at the server"; body: "Make an A record (for example share.example.com) that points to the server's IP address, and open ports 80 and 443." }
                                    Step { n: 2; title: "Get the server files"; body: "On the server:" }
                                    Cmd { code: "git clone https://github.com/Kepin17/olyzano-notch.git\ncd olyzano-notch/server" }
                                    Step { n: 3; title: "Set the domain and a secret key"; body: "Copy the settings file, make a long random key, then put the key and your domain into .env (RELAY_API_KEY and DOMAIN). Keep that key private: it is the host's key." }
                                    Cmd { code: "cp .env.example .env\nopenssl rand -hex 32\nnano .env" }
                                    Step { n: 4; title: "Start it"; body: "HTTPS is set up by itself the first time the domain is visited. A file may be up to 5 GB and the server holds 20 GB by default (MAX_FILE_MB and MAX_TOTAL_MB in .env)." }
                                    Cmd { code: "docker compose up -d --build" }
                                    Step { n: 5; title: "Add it to Notch"; body: "Back in Notch press Add a server by hand, type the address (https://share.example.com) and the key from step 3, then Add. The server card should say Connected and Host." }
                                    Step { n: 6; title: "Let other people use it"; body: "On the server's card press Manage, then Add member: type a name, choose how much space and for how long, and send the key it makes to that person. They paste it under Join a server." }
                                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.hairline }
                                    Text { text: "Later"; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                                    Caption { Layout.fillWidth: true; text: "Restart it:" }
                                    Cmd { code: "docker compose restart relay" }
                                    Caption { Layout.fillWidth: true; text: "Update it after copying new files (or git pull) and see what it is doing:" }
                                    Cmd { code: "docker compose up -d --build\ndocker compose logs -f relay" }
                                    Caption { Layout.fillWidth: true; text: "Without Docker (Python 3.9 or newer, behind any HTTPS proxy):" }
                                    Cmd { code: "RELAY_API_KEY=your-key DATA_DIR=./data PORT=8080 python3 relay.py" }
                                    Caption { Layout.fillWidth: true; text: "Lost the host key? Make a new one in .env and run docker compose up -d; Notch needs the new key once. Starting over completely deletes the data volume (all members and shares): see the full guide." }
                                    Item { Layout.preferredHeight: 4 }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                PillButton { text: "Open the full guide"; iconName: "external"; onClicked: Qt.openUrlExternally("https://github.com/Kepin17/olyzano-notch/blob/main/server/README.md") }
                                Item { Layout.fillWidth: true }
                                PillButton { text: "Add a server"; iconName: "plus"; checked: true; onClicked: { serverGuide.close(); sharing.manual = true } }
                            }
                        }
                    }

                    PillButton {
                        text: sharing.manual ? "Hide" : "Add a server by hand"
                        onClicked: sharing.manual = !sharing.manual
                    }
                    ColumnLayout {
                        visible: sharing.manual
                        Layout.fillWidth: true
                        spacing: 6
                        TextField {
                            id: nameField
                            Layout.fillWidth: true
                            implicitHeight: 30
                            leftPadding: 12
                            placeholderText: "Name (optional)"
                            placeholderTextColor: theme.placeholder
                            color: theme.text
                            selectionColor: theme.accent
                            font.pixelSize: 13
                            background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: nameField.activeFocus ? theme.accent : theme.outline }
                        }
                        TextField {
                            id: urlField
                            Layout.fillWidth: true
                            implicitHeight: 30
                            leftPadding: 12
                            placeholderText: "https://share.example.com"
                            placeholderTextColor: theme.placeholder
                            color: theme.text
                            selectionColor: theme.accent
                            font.pixelSize: 13
                            background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: urlField.activeFocus ? theme.accent : theme.outline }
                        }
                        TextField {
                            id: keyField
                            Layout.fillWidth: true
                            implicitHeight: 30
                            leftPadding: 12
                            echoMode: TextInput.Password
                            placeholderText: "API key"
                            placeholderTextColor: theme.placeholder
                            color: theme.text
                            selectionColor: theme.accent
                            font.pixelSize: 13
                            background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: keyField.activeFocus ? theme.accent : theme.outline }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Caption { id: addResult; Layout.fillWidth: true; color: theme.danger }
                            PillButton {
                                text: "Add"
                                checked: true
                                onClicked: {
                                    addResult.text = share.addServer(nameField.text, urlField.text, keyField.text)
                                    if (addResult.text === "") {
                                        nameField.clear()
                                        urlField.clear()
                                        keyField.clear()
                                        sharing.manual = false
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Loader {
            active: settingsScroll.category === "appearance"
            visible: active // an idle Loader is still a layout item: its spacing would leave a gap
            Layout.fillWidth: true
            sourceComponent: ColumnLayout {
                Layout.fillWidth: true
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    SectionLabel { text: "Theme" }
                    // A swatch card per style: its island, surface and accent, as they look in dark mode.
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 8
                        rowSpacing: 8
                        Repeater {
                            model: theme.styles
                            AbstractButton {
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                id: styleCard
                                required property var modelData
                                readonly property bool current: notch.uiStyle === modelData.id
                                Layout.fillWidth: true
                                implicitHeight: 64
                                Accessible.name: modelData.name + " theme"
                                onClicked: notch.uiStyle = modelData.id
                                background: Rectangle {
                                    radius: theme.radiusMd
                                    color: styleCard.modelData.island
                                    border.width: styleCard.current ? 2 : 1
                                    border.color: styleCard.current ? theme.accent : styleCard.hovered ? theme.textFaint : theme.outline
                                }
                                contentItem: Item {
                                    Rectangle {
                                        x: 8; y: 8
                                        width: parent.width - 16
                                        height: 22
                                        radius: 7
                                        color: styleCard.modelData.surface
                                        Rectangle {
                                            anchors.verticalCenter: parent.verticalCenter
                                            x: 6
                                            width: 10; height: 10; radius: 5
                                            color: styleCard.modelData.accent
                                        }
                                        Rectangle {
                                            anchors.verticalCenter: parent.verticalCenter
                                            x: 22
                                            width: parent.width - 30; height: 4; radius: 2
                                            color: Qt.rgba(1, 1, 1, 0.25)
                                        }
                                    }
                                    Text {
                                        x: 8
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 7
                                        width: parent.width - 16
                                        text: styleCard.modelData.name
                                        color: "#f5f5f7"
                                        font.pixelSize: 11
                                        font.weight: styleCard.current ? Font.DemiBold : Font.Normal
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                    Caption {
                        Layout.fillWidth: true
                        text: {
                            for (const s of theme.styles)
                                if (s.id === notch.uiStyle)
                                    return s.caption
                            return ""
                        }
                    }
                    SectionLabel { text: "Brightness"; Layout.topMargin: 6 }
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
                    SectionLabel { text: "Capsule shape"; Layout.topMargin: 6 }
                    Row {
                        spacing: 6
                        enabled: activities.capsuleMode === "capsule"
                        opacity: enabled ? 1 : 0.5
                        Repeater {
                            model: [["edge", "Notch"], ["pill", "Pill"]]
                            PillButton {
                                required property var modelData
                                text: modelData[1]
                                checked: activities.capsuleLook === modelData[0]
                                onClicked: activities.capsuleLook = modelData[0]
                            }
                        }
                    }
                    Caption {
                        Layout.fillWidth: true
                        text: activities.capsuleMode !== "capsule" ? "Used in Capsule mode (Settings → General). The Widget card keeps its own look."
                            : activities.capsuleLook === "pill" ? "A fully rounded capsule on the top edge, the same size and place as the notch, so the windows below it are not in its way."
                            : "The capsule grows out of the top edge like a notch, with curved shoulders."
                    }
                    SettingToggle {
                        Layout.topMargin: 6
                        title: "Border matches my system accent"
                        caption: "The thin edge of the capsule and the Widget card uses your " + (Qt.platform.os === "windows" ? "Windows" : "desktop") + " accent colour instead of the look's own outline."
                        checked: activities.borderMatchesSystem
                        onToggled: activities.borderMatchesSystem = !activities.borderMatchesSystem
                    }
                    SettingToggle {
                        Layout.topMargin: 6
                        title: "Clock follows theme colour"
                        caption: "The idle clock and the status bar clock use the look's accent colour instead of plain text. Off keeps them neutral."
                        checked: activities.clockAccent
                        onToggled: activities.clockAccent = !activities.clockAccent
                    }
                    SettingToggle {
                        Layout.topMargin: 6
                        title: "Animations and transitions"
                        caption: "Turn it off and everything moves at once: the capsule and panels change size without easing, songs and notifications swap without a roll, and nothing glides, pulses or bounces. Easier on slow computers and for people who prefer a still screen."
                        checked: activities.animations
                        onToggled: activities.animations = !activities.animations
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        enabled: activities.animations
                        opacity: enabled ? 1 : 0.45
                        SectionLabel { text: "Bounce" }
                        Segmented {
                            Layout.fillWidth: true
                            model: [["off", "Off"], ["soft", "Soft"], ["normal", "Normal"], ["bouncy", "Bouncy"]]
                            value: activities.animBounce
                            onPicked: (v) => activities.animBounce = v
                        }
                        Caption { Layout.fillWidth: true; text: "How far the capsule and panel overshoot their size before they settle. Off: they glide straight to it." }
                    }
                }
                SettingCard {
                    title: "Size & shape"
                    SectionLabel { text: "Interface size" }
                    Segmented {
                        Layout.fillWidth: true
                        model: [[0.9, "Small"], [1.0, "Normal"], [1.12, "Large"], [1.25, "Extra large"]]
                        value: activities.uiScale
                        onPicked: (v) => activities.uiScale = v
                    }
                    Caption { Layout.fillWidth: true; text: "Scales the open panel as a whole: text, icons and spacing together. Use Large on a high-resolution screen, Small where space is tight." }
                    SectionLabel { text: "Corners"; Layout.topMargin: 4 }
                    Segmented {
                        Layout.fillWidth: true
                        model: [["sharp", "Sharp"], ["normal", "Normal"], ["round", "Round"]]
                        value: activities.cornerStyle
                        onPicked: (v) => activities.cornerStyle = v
                    }
                    Caption { Layout.fillWidth: true; text: "How round cards, buttons and the capsule are." }
                }

                SettingCard {
                    title: "Glass"
                    SettingToggle {
                        title: "See-through notch"
                        caption: desktop.canBlur ? "The notch and the cards on it turn translucent, and what is behind them is blurred by the desktop."
                                                 : "The notch and the cards on it turn translucent. This desktop has no blur to offer, so they stay mostly solid to keep the text readable."
                        checked: activities.glass
                        onToggled: activities.glass = !activities.glass
                    }
                    DockSlider {
                        visible: activities.glass
                        title: "Strength"
                        valueText: activities.glassStrength + "%"
                        from: 0; to: 100; stepSize: 5
                        value: activities.glassStrength
                        onMoved: activities.glassStrength = value
                    }
                }
            }
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


    FolderDialog {
        id: folderDialog
        title: "Choose where Olyzano Notch stores files"
        currentFolder: "file://" + notch.storageDir
        onAccepted: notch.setStorageDir(selectedFolder)
    }

    // A titled group of settings on a card.
    component SettingCard: Rectangle {
        id: settingCard
        property string title
        default property alias content: cardBody.data
        Layout.fillWidth: true
        implicitHeight: cardBody.implicitHeight + 24
        radius: theme.radiusMd
        color: theme.card
        border.width: 1
        border.color: theme.outline
        GlassLayer { anchors.fill: parent; radius: theme.radiusMd }
        ColumnLayout {
            id: cardBody
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
            spacing: 10
            Text {
                text: settingCard.title.toUpperCase()
                color: theme.textDim
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }
        }
    }

    // A row of minute choices for the wellbeing settings.
    component MinutePicker: ColumnLayout {
        id: picker
        property string label
        property var choices: []
        property int value
        signal picked(int minutes)
        Layout.fillWidth: true
        spacing: 6
        SectionLabel { text: picker.label }
        Row {
            spacing: 6
            Repeater {
                model: picker.choices
                PillButton {
                    required property int modelData
                    text: modelData + " min"
                    checked: picker.value === modelData
                    onClicked: picker.picked(modelData)
                }
            }
        }
    }

    // A labelled slider for the dock settings.
    component DockSlider: ColumnLayout {
        id: dockSlider
        property alias title: sliderTitle.text
        property string valueText
        property alias from: slider.from
        property alias to: slider.to
        property alias stepSize: slider.stepSize
        property alias value: slider.value
        signal moved()
        Layout.fillWidth: true
        Layout.topMargin: 6
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            SectionLabel { id: sliderTitle; Layout.fillWidth: true }
            Caption { text: dockSlider.valueText }
        }
        Slider {
            id: slider
            Layout.fillWidth: true
            snapMode: Slider.SnapAlways
            onMoved: dockSlider.moved()
            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + (slider.availableHeight - height) / 2
                width: slider.availableWidth
                height: 4
                radius: 2
                color: theme.outline // the unfilled part, visible on a card
                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    radius: 2
                    color: theme.accent
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + (slider.availableHeight - height) / 2
                width: 16
                height: 16
                radius: theme.radiusSm
                color: slider.pressed ? theme.accent : theme.text
                border.color: theme.outline
            }
        }
    }
}
