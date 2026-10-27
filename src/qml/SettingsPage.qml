import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Settings page: a list of sections on the left (with a search for a setting), the chosen section on the right.
RowLayout {
    id: settingsScroll
    required property var shell
    // Its menu / folder dialog are open: Notch keeps the whole window clickable meanwhile.
    readonly property bool overlayOpen: ruleMenu.visible || folderDialog.visible
    property string category: "general" // the section shown; kept while Notch runs
    property string query: ""
    visible: shell.page === "settings"
    Layout.fillWidth: true
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    Layout.preferredHeight: 480
    spacing: 14

    // id, name, icon, what it is for
    readonly property var sections: [
        ["general", "General", "gear", "Start-up, how the notch sits on the screen, gaming mode and sounds."],
        ["appearance", "Appearance", "palette", "Themes and light or dark."],
        ["panel", "Panel", "panel", "A glass status bar along the top, with Quick Settings."],
        ["alerts", "Alerts & activities", "notifications", "What the notch shows you: chargers, downloads, calls, the microphone and more."],
        ["files", "Files", "folder", "How long files stay, where they go and what is added by itself."],
        ["dock", "Dock", "dock", "The app dock at the edge of the screen."],
        ["sharing", "Sharing & Nearby", "share", "Servers for Share via QR, and the name other computers see."],
        ["wellbeing", "Wellbeing", "heart", "Break and eye reminders."],
        ["about", "About", "info", "Version, author, links and support."]
    ]
    // What a search can find: section, setting, words that also describe it.
    readonly property var index: [
        ["about", "Updates", "update upgrade new version release check download latest github notify"], ["wellbeing", "Doomscrolling", "doomscroll scrolling instagram tiktok youtube shorts reels feed warn screen time"], ["general", "Start at login", "autostart boot startup"], ["general", "How the notch sits on the screen", "hidden capsule always open dynamic island pin"],
        ["general", "Gaming mode", "game fullscreen full screen maximised hide"], ["general", "Sounds", "sound effects volume quiet mute"],
        ["appearance", "Theme", "style colors classic washi frost ocean forest sunset lavender mocha sakura matcha ai indigo cheongja hanbok hanji japanese korean"], ["general", "Keep windows clear of the capsule", "reserve space taskbar work area maximise maximize windows below auto align strip"], ["general", "Widget (beta)", "widget floating card drag move position beta"], ["general", "Spectrum style", "spectrum equalizer bars wave dots pulse song capsule style visualizer"], ["general", "Voice assistant", "voice assistant commands speech microphone whisper offline say listen"], ["general", "Capsule size", "capsule size scale bigger smaller dynamic island"], ["general", "Floating pill", "pill floating capsule dynamic island edge look shape"], ["about", "About Olyzano Notch", "version author website demo source license coffee donate support traktir qr"], ["appearance", "Light or dark", "brightness dark light system mode"], ["appearance", "Glass", "glass glassmorphism translucent transparent blur frosted see-through"], ["appearance", "Border matches my system accent", "border outline edge accent windows color capsule widget"], ["appearance", "Animations and transitions", "animation animations transition motion reduce disable off smooth instant bounce spring"],
        ["panel", "Status bar", "panel top bar glass clock quick settings"], ["panel", "Always show or auto-hide", "bar hide slide reserve space"],
        ["panel", "Transparency and blur", "glass opacity bar"], ["panel", "What the bar shows", "app name date seconds battery percentage"],
        ["panel", "Quick Settings", "wifi bluetooth volume brightness power click hover"],
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
        ["sharing", "Nearby device name", "computer name receive"], ["sharing", "Sharing servers", "relay vps share qr server"],
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
            delegate: AbstractButton {
                id: side
                required property var modelData
                readonly property bool on: settingsScroll.category === modelData[0]
                Layout.fillWidth: true
                implicitHeight: 34
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
        // ... or what the search found
        Repeater {
            model: settingsScroll.results
            delegate: AbstractButton {
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

        ColumnLayout {
            visible: settingsScroll.category === "general"
            Layout.fillWidth: true
            spacing: 16

            SettingCard {
                title: "Notch"
            SettingToggle {
                title: "Start at login"
                checked: notch.autostart
                onToggled: notch.autostart = !notch.autostart
            }

            SettingToggle {
                title: "Dynamic island"
                caption: "A small black capsule stays at the top and shows what is going on: the song playing, the app using the microphone"
                    + (Qt.platform.os === "windows" || Qt.platform.os === "linux" ? ", a Discord call," : "") + " and notifications from this computer."
                checked: activities.capsuleMode !== "hidden"
                onToggled: activities.capsuleMode = activities.capsuleMode === "hidden" ? "capsule" : "hidden"
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

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "How the notch sits on the screen"
                    color: theme.text
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                Flow { // wraps: with "Widget · Beta" and "Reset position" the row is wider than the page
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [["hidden", "Hidden"], ["capsule", "Capsule"], ["widget", "Widget · Beta"], ["open", "Always open"]]
                        PillButton {
                            required property var modelData
                            text: modelData[1]
                            checked: activities.capsuleMode === modelData[0]
                            onClicked: activities.capsuleMode = modelData[0]
                        }
                    }
                    PillButton { visible: activities.capsuleMode === "widget"; text: "Reset position"; iconName: "undo"; onClicked: activities.resetWidgetPos() }
                }
                Caption {
                    Layout.fillWidth: true
                    text: activities.capsuleMode === "capsule" ? "A small black capsule stays at the top, like a Dynamic Island. Point at it to open Olyzano Notch."
                        : activities.capsuleMode === "widget" ? "Beta: a floating card you can drag anywhere. Click it to open Olyzano Notch beside it; pointing at the top edge opens nothing. It may still have rough edges."
                        : activities.capsuleMode === "open" ? "The panel stays open at the top of the screen."
                        : "Only a thin line shows at the top edge. Rest the pointer there to open Olyzano Notch."
                }
            }

            SettingToggle {
                visible: desktop.canReserve && activities.capsuleMode === "capsule"
                title: "Keep other windows clear of the capsule"
                caption: "Maximised windows start below the capsule, like under a taskbar, instead of behind it. A strip as tall as the capsule is kept free along the top"
                         + (topbar.enabled && topbar.reserve ? " (the status bar already keeps room, so only the difference is added)." : ".")
                         + " On Windows and KDE Plasma (Wayland)."
                checked: activities.reserveSpace
                onToggled: activities.reserveSpace = !activities.reserveSpace
            }

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
            DockSlider {
                title: "Capsule size"
                valueText: Math.round(activities.capsuleScale * 100) + "%"
                from: 80; to: 130; stepSize: 5
                value: Math.round(activities.capsuleScale * 100)
                onMoved: activities.capsuleScale = value / 100
            }
            SettingToggle {
                title: "Floating pill"
                caption: "The capsule floats a little below the top edge as a full pill with a thin outline and a soft shadow, instead of growing out of the edge. Not used in Widget mode."
                checked: activities.capsuleLook === "pill"
                onToggled: activities.capsuleLook = activities.capsuleLook === "pill" ? "edge" : "pill"
            }
            SettingToggle {
                title: "Next task in the capsule"
                caption: "While nothing is playing and no timer runs, the capsule shows your next task and how long is left."
                checked: activities.taskInCapsule
                onToggled: activities.taskInCapsule = !activities.taskInCapsule
            }
            }

            SettingCard {
                title: "Gaming mode"
                SettingToggle {
                    title: "Keep away from full-screen apps and games"
                    caption: "While an app or game that fills the screen (full screen or maximised) is in front, Olyzano Notch and the dock cannot be reached at all (no pointer, no clicks, nothing opens), and sounds and reminders stay quiet. Minimise it or make its window smaller and they work again."
                    checked: gaming.enabled
                    onToggled: gaming.enabled = !gaming.enabled
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
                    Row {
                        spacing: 6
                        Repeater {
                            model: [["auto", "Automatic"], ["id", "Indonesia"], ["en", "English"]]
                            PillButton {
                                required property var modelData
                                text: modelData[1]
                                checked: assistant.language === modelData[0]
                                onClicked: assistant.language = modelData[0]
                            }
                        }
                    }
                    SectionLabel { text: "Search with"; Layout.topMargin: 4 }
                    Row {
                        spacing: 6
                        Repeater {
                            model: [["duckduckgo", "DuckDuckGo"], ["google", "Google"], ["bing", "Bing"], ["youtube", "YouTube"]]
                            PillButton {
                                required property var modelData
                                text: modelData[1]
                                checked: assistant.searchEngine === modelData[0]
                                onClicked: assistant.searchEngine = modelData[0]
                            }
                        }
                    }
                    Caption {
                        Layout.fillWidth: true
                        text: "Try: “open the browser”, “open YouTube”, “search channel Nadia on YouTube”, “volume 40”, “timer 10 minutes”, “add task buy milk tomorrow 5”, “pause the music”. Nothing that deletes or shuts down is understood."
                    }
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

        ColumnLayout {
            visible: settingsScroll.category === "alerts"
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

        ColumnLayout {
            visible: settingsScroll.category === "files"
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

        ColumnLayout {
            visible: settingsScroll.category === "dock"
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

        ColumnLayout {
            visible: settingsScroll.category === "panel"
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
                Caption {
                    Layout.fillWidth: true
                    text: "Wi-Fi, Bluetooth, volume, brightness and power, from the icons on the right of the bar."
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

        ColumnLayout {
            visible: settingsScroll.category === "about"
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
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        PillButton { text: "Download"; iconName: "download"; checked: true; onClicked: updates.download() }
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

        ColumnLayout {
            visible: settingsScroll.category === "wellbeing"
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

        ColumnLayout {
            visible: settingsScroll.category === "sharing"
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
                function mb(bytes) { return bytes >= 10 * 1048576 ? Math.round(bytes / 1048576) + " MB" : (bytes / 1048576).toFixed(1) + " MB" }

                SectionLabel { text: "Sharing servers" }
                Caption {
                    Layout.fillWidth: true
                    text: "Share via QR can upload to a server you or your team run, encrypted on this computer first. Anyone can be the host: ask them for the address and key, or set one up with server/README.md."
                }
                Caption { Layout.fillWidth: true; visible: share.servers.length === 0; text: "No server yet." }

                Repeater {
                    model: share.servers
                    delegate: Rectangle {
                        id: srv
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: srvBody.implicitHeight + 16
                        radius: theme.radiusMd
                        color: theme.card
                        border.width: 1
                        border.color: modelData.isDefault ? theme.accent : theme.outline

                        ColumnLayout {
                            id: srvBody
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { Layout.fillWidth: true; text: srv.modelData.name; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight }
                                    Text { Layout.fillWidth: true; text: srv.modelData.url; color: theme.textDim; font.pixelSize: 11; elide: Text.ElideMiddle }
                                }
                                PillButton {
                                    text: srv.modelData.isDefault ? "Default" : "Make default"
                                    checked: srv.modelData.isDefault
                                    visible: share.servers.length > 1
                                    onClicked: share.defaultServerId = srv.modelData.id
                                }
                                IconButton { iconName: "close"; Accessible.name: "Remove this server"; onClicked: share.removeServer(srv.modelData.id) }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    font.pixelSize: 11
                                    color: !srv.modelData.tested ? theme.textDim : srv.modelData.ok ? theme.success : theme.danger
                                    text: !srv.modelData.tested ? "Checking…"
                                          : !srv.modelData.ok ? srv.modelData.message
                                          : srv.modelData.message + " · " + sharing.mb(srv.modelData.used) + (srv.modelData.quota ? " of " + sharing.mb(srv.modelData.quota) : "")
                                            + (srv.modelData.maxActive ? " · " + srv.modelData.active + " of " + srv.modelData.maxActive + " shares" : "")
                                            + " · files up to " + sharing.mb(srv.modelData.maxFile)
                                }
                                PillButton { text: "Test"; onClicked: share.testServer(srv.modelData.id) }
                            }
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

        ColumnLayout {
            visible: settingsScroll.category === "appearance"
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
                SettingToggle {
                    Layout.topMargin: 6
                    title: "Border matches my system accent"
                    caption: "The thin edge of the capsule and the Widget card uses your " + (Qt.platform.os === "windows" ? "Windows" : "desktop") + " accent colour instead of the look's own outline."
                    checked: activities.borderMatchesSystem
                    onToggled: activities.borderMatchesSystem = !activities.borderMatchesSystem
                }
                SettingToggle {
                    Layout.topMargin: 6
                    title: "Animations and transitions"
                    caption: "Turn it off and everything moves at once: the capsule and panels change size without easing, songs and notifications swap without a roll, and nothing glides, pulses or bounces. Easier on slow computers and for people who prefer a still screen."
                    checked: activities.animations
                    onToggled: activities.animations = !activities.animations
                }
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
