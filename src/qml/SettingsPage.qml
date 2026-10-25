import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Settings page (scrolls when taller than the window).
ScrollView {
    id: settingsScroll
    required property var shell
    // Its menu / folder dialog are open: Notch keeps the whole window clickable meanwhile.
    readonly property bool overlayOpen: ruleMenu.visible || folderDialog.visible
    property string category: "general" // the tab shown; kept while Notch runs
    visible: shell.page === "settings"
    Layout.fillWidth: true
    Layout.preferredHeight: Math.min(settingsColumn.implicitHeight, 470)
    contentWidth: availableWidth
    clip: true
    ColumnLayout {
        id: settingsColumn
        width: settingsScroll.availableWidth - 24
        x: 12
        spacing: 16

        // Categories: only the chosen one is shown.
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [["general", "General"], ["files", "Files"], ["dock", "Dock"], ["sharing", "Sharing"], ["appearance", "Appearance"]]
                PillButton {
                    required property var modelData
                    text: modelData[1]
                    checked: settingsScroll.category === modelData[0]
                    onClicked: settingsScroll.category = modelData[0]
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: -6
            implicitHeight: 1
            color: theme.outline
        }

        ColumnLayout {
            visible: settingsScroll.category === "general"
            Layout.fillWidth: true
            spacing: 16

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
                title: "Downloads in the notch"
                caption: "Files your browser is still downloading show in the closed notch, with their size and speed."
                checked: activities.showDownloads
                onToggled: activities.showDownloads = !activities.showDownloads
            }

            SettingToggle {
                visible: notch.linuxDesktop
                title: "Phone calls & notifications"
                caption: "Through KDE Connect: incoming calls, your phone's notifications and a low phone battery show in the notch."
                checked: activities.phoneAlerts
                onToggled: activities.phoneAlerts = !activities.phoneAlerts
            }

            SettingToggle {
                visible: notch.linuxDesktop
                title: "Microphone & camera indicators"
                caption: "A green dot beside the notch while a camera is on, an orange one while an app records the microphone."
                checked: activities.privacyIndicators
                onToggled: activities.privacyIndicators = !activities.privacyIndicators
            }

            RowLayout {
                Layout.fillWidth: true
                Caption { Layout.fillWidth: true; text: "Notch " + Qt.application.version }
                PillButton { text: "Quit Notch"; onClicked: Qt.quit() }
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
                        color: theme.warn
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
                                             : "Needs KDE Plasma with its blur effect on (and Notch built with KF6WindowSystem)."
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
            visible: settingsScroll.category === "sharing"
            Layout.fillWidth: true
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                SectionLabel { text: "Nearby" }
                Caption {
                    Layout.fillWidth: true
                    text: "The name other Notch computers see while you have Receive switched on in the Share window."
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
                        radius: theme.washi ? 8 : 12
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
                                radius: 12
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
        title: "Choose where Notch stores files"
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
        radius: theme.washi ? 8 : 14
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
                radius: 8
                color: slider.pressed ? theme.accent : theme.text
                border.color: theme.outline
            }
        }
    }
}
