import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Share via QR: choose how (this WiFi or your own server), then show the QR code while it lasts.
// Reads and drives the `share` object (ShareController).
Popup {
    id: pop
    required property var shell
    property var ids: []
    property bool hasVault: false
    property string mode: "local"
    property int ttl: 3600
    property bool single: true
    property string address: ""
    property var addresses: []
    readonly property string st: share.status
    readonly property bool running: st === "preparing" || st === "uploading" || st === "ready"
    property int now: Math.floor(Date.now() / 1000)

    x: (shell.width - width) / 2
    y: 64
    width: 340
    padding: 16
    focus: true
    // While a share runs the popup stays until Hide / Stop, so a stray click can't lose the QR.
    closePolicy: running ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function openFor(newIds) {
        if (!running) {
            share.stop() // clears an ended / failed share
            ids = newIds
            hasVault = newIds.some(id => notch.itemInfo(id).vault)
            mode = share.lastMode === "relay" && share.relayConfigured ? "relay" : "local"
            ttl = share.defaultTtl
            addresses = share.lanAddresses()
            address = addresses.length > 0 ? addresses[0] : ""
            pin.clear()
        }
        open()
    }
    function begin() {
        share.start(ids, mode, ttl, pin.text, single, address)
    }
    function expiryText() {
        const left = Math.max(0, share.expiresAt - now)
        const m = Math.floor(left / 60), s = left % 60
        return m >= 60 ? Math.floor(m / 60) + " h " + (m % 60) + " min" : m + ":" + (s < 10 ? "0" : "") + s
    }
    function qrSource(text) {
        return "image://qr/" + Qt.btoa(text).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "")
    }
    onOpened: now = Math.floor(Date.now() / 1000)
    onClosed: if (!running) share.stop()

    Timer {
        interval: 1000
        repeat: true
        running: pop.visible && pop.st === "ready"
        onTriggered: pop.now = Math.floor(Date.now() / 1000)
    }

    background: Rectangle { radius: theme.washi ? 8 : 16; color: theme.surface; border.color: theme.outline }

    contentItem: ColumnLayout {
        spacing: 10

        SectionLabel { text: "Share via QR" }

        // ---- setup ----
        ColumnLayout {
            visible: pop.st === "idle"
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
                PillButton { text: "This WiFi"; checked: pop.mode === "local"; onClicked: pop.mode = "local" }
                PillButton {
                    text: "My server"
                    checked: pop.mode === "relay"
                    onClicked: pop.mode = "relay"
                }
            }
            Caption {
                Layout.fillWidth: true
                visible: pop.ids.length > 1
                text: "Whoever scans sees the list and can take one file, a few, or all as a zip."
            }
            Caption {
                Layout.fillWidth: true
                text: pop.mode === "local"
                      ? "Your computer serves the file to phones on the same WiFi. Not encrypted, and it works only while Notch runs."
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
            TextField {
                id: pin
                visible: pop.mode === "relay"
                Layout.fillWidth: true
                implicitHeight: 30
                leftPadding: 12
                placeholderText: "PIN (optional)"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 13
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhDigitsOnly
                background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: theme.accent }
                Keys.onEscapePressed: pop.close()
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

        // ---- working ----
        ColumnLayout {
            visible: pop.st === "preparing" || pop.st === "uploading"
            Layout.fillWidth: true
            spacing: 10
            Caption { text: pop.st === "uploading" ? "Uploading encrypted file…" : "Preparing…" }
            ProgressBar {
                Layout.fillWidth: true
                indeterminate: pop.st === "preparing" || share.progress < 0
                value: Math.max(0, share.progress)
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: { share.stop(); pop.close() } }
            }
        }

        // ---- ready ----
        ColumnLayout {
            visible: pop.st === "ready"
            Layout.fillWidth: true
            spacing: 8
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
                    source: pop.st === "ready" ? pop.qrSource(share.url) : ""
                }
            }
            Text {
                Layout.fillWidth: true
                text: share.name
                color: theme.text
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
            }
            Caption {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: "Scan with your phone's camera · expires in " + pop.expiryText()
            }
            Caption {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: share.mode === "local" ? "Phone and computer must be on the same WiFi."
                      : "Encrypted: only people with this exact link can open it."
            }
            Caption {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                visible: share.mode === "local" && share.fileCount > 1
                text: share.downloads === 0 ? "Stays open until it expires or you press Stop."
                      : "Downloaded " + share.downloads + (share.downloads === 1 ? " time" : " times") + " so far."
            }
            TextField {
                Layout.fillWidth: true
                implicitHeight: 28
                leftPadding: 10
                readOnly: true
                text: share.url
                color: theme.textDim
                font.pixelSize: 11
                selectByMouse: true
                background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: theme.outline }
            }
            RowLayout {
                Layout.fillWidth: true
                PillButton { text: "Copy link"; onClicked: { urlCopy.text = share.url; urlCopy.selectAll(); urlCopy.copy() } }
                Item { Layout.fillWidth: true }
                PillButton { text: "Hide"; onClicked: pop.close() }
                PillButton { text: "Stop"; checked: true; onClicked: { share.stop(); pop.close() } }
            }
            TextInput { id: urlCopy; visible: false }
        }

        // ---- finished / failed ----
        ColumnLayout {
            visible: pop.st === "ended" || pop.st === "error"
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: (share.endReason === "downloaded" ? "✓ " : "") + share.message
                color: pop.st === "error" ? theme.danger : theme.text
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { visible: pop.st === "error"; text: "Back"; onClicked: share.stop() }
                PillButton { text: "Close"; checked: true; onClicked: { share.stop(); pop.close() } }
            }
        }
    }
}
