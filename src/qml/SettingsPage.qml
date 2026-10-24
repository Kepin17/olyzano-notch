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
                    model: [["classic", "Classic"], ["washi", "Washi 和紙"]]
                    PillButton {
                        required property var modelData
                        text: modelData[1]
                        checked: notch.uiStyle === modelData[0]
                        onClicked: notch.uiStyle = modelData[0]
                    }
                }
            }
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
            Caption {
                text: theme.washi ? "Sumi ink and washi paper, with a vermilion (shu) accent."
                                  : "Highlights use your KDE accent color."
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

        RowLayout {
            Layout.fillWidth: true
            Caption { Layout.fillWidth: true; text: "Notch " + Qt.application.version }
            PillButton { text: "Quit Notch"; onClicked: Qt.quit() }
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
}
