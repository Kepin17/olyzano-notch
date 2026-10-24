import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Share via QR. "setup": choose how and with what protection. "sessions": everything being shared right
// now (several at once), each with its QR code. Drives the `share` object (ShareController).
Popup {
    id: pop
    required property var shell
    property var ids: []
    property bool hasVault: false
    property string view: "setup" // setup | sessions
    property string expandedId: ""
    property string mode: "local"
    property int ttl: 3600
    property string protection: "none" // none | pin | password
    property bool single: true
    property string address: ""
    property var addresses: []
    property bool tried: false
    property int now: Math.floor(Date.now() / 1000)
    readonly property string secretError: protection === "none" ? "" : share.checkSecret(protection, secret.text)

    x: (shell.width - width) / 2
    y: 64
    width: 340
    padding: 16
    focus: true

    function openFor(newIds) {
        ids = newIds
        hasVault = newIds.some(id => notch.itemInfo(id).vault)
        mode = share.lastMode === "relay" && share.relayConfigured ? "relay" : "local"
        ttl = share.defaultTtl
        protection = "none"
        tried = false
        addresses = share.lanAddresses()
        address = addresses.length > 0 ? addresses[0] : ""
        secret.clear()
        view = "setup"
        open()
    }
    function openSessions() {
        view = "sessions"
        open()
    }
    function begin() {
        tried = true
        if (secretError !== "")
            return
        expandedId = share.start(ids, mode, ttl, protection, secret.text, single, address)
        secret.clear()
        view = "sessions"
    }
    function countdown(expiresAt) {
        const left = Math.max(0, expiresAt - now)
        const m = Math.floor(left / 60), s = left % 60
        return m >= 60 ? Math.floor(m / 60) + " h " + (m % 60) + " min" : m + ":" + (s < 10 ? "0" : "") + s
    }
    function statusText(d) {
        if (d.status === "preparing") return "Preparing…"
        if (d.status === "uploading") return "Uploading " + Math.round(Math.max(0, d.progress) * 100) + "%"
        if (d.status === "ready")
            return "Expires in " + countdown(d.expiresAt) + (d.downloads > 0 ? " · downloaded " + d.downloads + "×" : "")
        return d.message
    }
    function qrSource(text) {
        return "image://qr/" + Qt.btoa(text).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "")
    }
    onOpened: now = Math.floor(Date.now() / 1000)
    onClosed: secret.clear()

    Timer {
        interval: 1000
        repeat: true
        running: pop.visible && share.activeCount > 0
        onTriggered: pop.now = Math.floor(Date.now() / 1000)
    }

    background: Rectangle { radius: theme.washi ? 8 : 16; color: theme.surface; border.color: theme.outline }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: pop.view === "setup" ? "Share via QR" : "Sharing now" }
            Item { Layout.fillWidth: true }
            PillButton {
                visible: pop.view === "setup" && share.count > 0
                text: "Active (" + share.count + ")"
                onClicked: pop.view = "sessions"
            }
            PillButton {
                visible: pop.view === "sessions" && pop.ids.length > 0
                text: "New share"
                onClicked: pop.view = "setup"
            }
        }

        // ---- setup ----
        ColumnLayout {
            visible: pop.view === "setup"
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: pop.ids.length === 1 ? notch.itemInfo(pop.ids[0]).name : pop.ids.length + " files"
                color: theme.text
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }
            Row {
                spacing: 6
                PillButton { text: "This WiFi"; iconName: "wifi"; checked: pop.mode === "local"; onClicked: pop.mode = "local" }
                PillButton { text: "My server"; iconName: "server"; checked: pop.mode === "relay"; onClicked: pop.mode = "relay" }
            }
            Caption {
                Layout.fillWidth: true
                visible: pop.ids.length > 1
                text: "Whoever scans sees the list and can take one file, a few, or all as a zip."
            }
            Caption {
                Layout.fillWidth: true
                text: pop.mode === "local"
                      ? "Your computer serves the files to phones on the same WiFi. Not encrypted, and it works only while Notch runs."
                      : share.relayConfigured
                        ? "Encrypted here, then stored on your server. The server cannot read it, and your computer can be switched off."
                        : "Set up your server in Settings → Sharing first."
            }
            Caption {
                Layout.fillWidth: true
                visible: pop.mode === "local" && pop.addresses.length === 0
                color: theme.danger
                text: "No network connection found."
            }
            Caption {
                Layout.fillWidth: true
                visible: pop.hasVault && pop.mode === "local"
                color: theme.danger
                text: "These include locked files. Over WiFi they are sent unencrypted."
            }
            Row {
                spacing: 6
                Repeater {
                    model: [[600, "10 min"], [3600, "1 hour"], [86400, "24 hours"]]
                    PillButton {
                        required property var modelData
                        text: modelData[1]
                        checked: pop.ttl === modelData[0]
                        onClicked: { pop.ttl = modelData[0]; share.defaultTtl = modelData[0] }
                    }
                }
            }

            SectionLabel { text: "Protect with" }
            Row {
                spacing: 6
                PillButton { text: "Nothing"; checked: pop.protection === "none"; onClicked: pop.protection = "none" }
                PillButton { iconName: "lock"; text: "PIN"; checked: pop.protection === "pin"; onClicked: pop.protection = "pin" }
                PillButton { iconName: "lock"; text: "Password"; checked: pop.protection === "password"; onClicked: pop.protection = "password" }
            }
            TextField {
                id: secret
                visible: pop.protection !== "none"
                Layout.fillWidth: true
                implicitHeight: 30
                leftPadding: 12
                placeholderText: pop.protection === "pin" ? "PIN (4–12 digits)" : "Password (8+ characters)"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 13
                echoMode: showSecret.checked ? TextInput.Normal : TextInput.Password
                inputMethodHints: pop.protection === "pin" ? Qt.ImhDigitsOnly : Qt.ImhNone
                background: Rectangle {
                    radius: theme.chipRadius + 2
                    color: theme.well
                    border.width: 1
                    border.color: pop.tried && pop.secretError !== "" ? theme.danger : theme.accent
                }
                onAccepted: pop.begin()
                Keys.onEscapePressed: pop.close()
            }
            RowLayout {
                visible: pop.protection !== "none"
                Layout.fillWidth: true
                Caption {
                    Layout.fillWidth: true
                    color: pop.tried && pop.secretError !== "" ? theme.danger : theme.textDim
                    text: pop.tried && pop.secretError !== "" ? pop.secretError
                          : pop.protection === "pin"
                            ? (pop.mode === "relay" ? "Checked by your server: 5 wrong tries lock it for 10 minutes."
                                                    : "Checked by Notch: 5 wrong tries lock that phone out for 10 minutes.")
                            : (pop.mode === "relay" ? "It also locks the key inside the link, so the link alone opens nothing. Tell it by another route."
                                                    : "Checked by Notch: 5 wrong tries lock that phone out for 10 minutes.")
                }
                PillButton { id: showSecret; text: "Show"; checkable: true }
            }
            SettingToggle {
                visible: pop.mode === "relay" && pop.ids.length === 1
                Layout.fillWidth: true
                title: "Single download"
                caption: "The link stops working after the first download."
                checked: pop.single
                onToggled: pop.single = !pop.single
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: pop.close() }
                PillButton {
                    text: pop.hasVault && pop.mode === "local" ? "Share anyway" : "Share"
                    checked: true
                    enabled: pop.mode === "local" ? pop.addresses.length > 0 : share.relayConfigured
                    onClicked: pop.begin()
                }
            }
        }

        // ---- sessions ----
        ColumnLayout {
            visible: pop.view === "sessions"
            Layout.fillWidth: true
            spacing: 8

            Caption {
                Layout.fillWidth: true
                visible: share.count === 0
                text: "Nothing is being shared. Right-click a file → Share via QR."
            }

            Flickable {
                Layout.fillWidth: true
                visible: share.count > 0
                implicitHeight: Math.min(list.implicitHeight, 380)
                contentHeight: list.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                ColumnLayout {
                    id: list
                    width: parent.width - 8
                    spacing: 8
                    Repeater {
                        model: share
                        delegate: Rectangle {
                            id: card
                            required property string sessionId
                            required property string name
                            required property string mode
                            required property string status
                            required property string url
                            required property double progress
                            required property double expiresAt
                            required property int fileCount
                            required property int downloads
                            required property string message
                            required property string endReason
                            required property string protection
                            required property bool active
                            readonly property bool open: pop.expandedId === sessionId && status === "ready"
                            readonly property var snapshot: ({ status: status, progress: progress, expiresAt: expiresAt, downloads: downloads, message: message })
                            Layout.fillWidth: true
                            implicitHeight: body.implicitHeight + 20
                            radius: theme.washi ? 8 : 12
                            color: theme.card
                            border.width: 1
                            border.color: open ? theme.accent : theme.outline

                            ColumnLayout {
                                id: body
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    LineIcon {
                                        glyph: card.mode === "relay" ? "server" : "wifi"
                                        sourceSize: Qt.size(16, 16)
                                        color: card.active ? theme.accent : theme.textDim
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Text {
                                                Layout.fillWidth: true
                                                text: card.name
                                                color: theme.text
                                                font.pixelSize: 13
                                                font.weight: Font.Medium
                                                elide: Text.ElideMiddle
                                            }
                                            LineIcon {
                                                visible: card.protection !== ""
                                                glyph: "lock"
                                                sourceSize: Qt.size(12, 12)
                                                color: theme.textDim
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: pop.statusText(card.snapshot)
                                            color: card.status === "error" ? theme.danger : theme.textDim
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                    PillButton {
                                        visible: card.status === "ready"
                                        text: card.open ? "Hide" : "QR"
                                        onClicked: pop.expandedId = card.open ? "" : card.sessionId
                                    }
                                    PillButton {
                                        visible: card.active
                                        text: "Stop"
                                        onClicked: share.stop(card.sessionId)
                                    }
                                    IconButton {
                                        visible: !card.active
                                        iconName: "close"
                                        Accessible.name: "Remove from the list"
                                        onClicked: share.dismiss(card.sessionId)
                                    }
                                }

                                ProgressBar {
                                    Layout.fillWidth: true
                                    visible: card.status === "preparing" || card.status === "uploading"
                                    indeterminate: card.status === "preparing" || card.progress < 0
                                    value: Math.max(0, card.progress)
                                }

                                ColumnLayout {
                                    visible: card.open
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Rectangle {
                                        Layout.alignment: Qt.AlignHCenter
                                        implicitWidth: 208
                                        implicitHeight: 208
                                        radius: 6
                                        color: "white"
                                        Image {
                                            anchors.centerIn: parent
                                            width: 200
                                            height: 200
                                            sourceSize: Qt.size(200, 200)
                                            fillMode: Image.PreserveAspectFit
                                            smooth: false
                                            cache: false
                                            source: card.open ? pop.qrSource(card.url) : ""
                                        }
                                    }
                                    Caption {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        text: (card.mode === "local" ? "Phone and computer must be on the same WiFi." : "Encrypted: only people with this exact link can open it.")
                                              + (card.protection === "pin" ? " They also need the PIN."
                                                 : card.protection === "password" ? " They also need the password." : "")
                                    }
                                    Caption {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        visible: card.mode === "local" && card.fileCount > 1
                                        text: "Stays open until it expires or you press Stop."
                                    }
                                    TextField {
                                        id: linkField
                                        Layout.fillWidth: true
                                        implicitHeight: 28
                                        leftPadding: 10
                                        readOnly: true
                                        text: card.url
                                        color: theme.textDim
                                        font.pixelSize: 11
                                        selectByMouse: true
                                        background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: theme.outline }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Item { Layout.fillWidth: true }
                                        PillButton {
                                            text: "Copy link"
                                            onClicked: { linkField.selectAll(); linkField.copy(); linkField.deselect() }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: share.count > 0
                PillButton {
                    visible: share.activeCount > 1
                    text: "Stop all"
                    onClicked: share.stopAll()
                }
                PillButton {
                    visible: share.count > share.activeCount
                    text: "Clear finished"
                    onClicked: share.clearFinished()
                }
                Item { Layout.fillWidth: true }
                PillButton { text: "Close"; checked: true; onClicked: pop.close() }
            }
        }
    }
}
