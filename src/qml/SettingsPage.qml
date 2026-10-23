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
