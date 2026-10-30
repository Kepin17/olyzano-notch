import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Share via QR. "setup": choose how and with what protection. "ready": the QR code on the left and the link on the
// right, in this same dialog right after Share. "sessions": everything being shared right now (several at once). Drives the `share` object (ShareController).
Popup {
    id: pop
    required property var shell
    property var ids: []
    property bool hasVault: false
    property string view: "setup" // setup | ready | sessions
    property string readyId: ""   // the share whose QR and link the "ready" view shows
    property string mode: "local" // local | relay | nearby
    property string nearbyId: ""
    property string receiveError: ""
    property bool optionsOpen: false
    // A chosen protection, or a rejected secret, keeps the options in view.
    readonly property bool optionsShown: optionsOpen || protection !== "none" || (tried && secretError !== "")
    property int ttl: 3600
    property string protection: "none" // none | pin | password
    property bool single: true
    property string address: ""
    property var addresses: []
    property bool tried: false
    property string serverId: ""
    property real totalBytes: 0
    // The saved server this share would go to, with what its last test found (limits, usage).
    readonly property var server: {
        const list = share.servers
        return list.find(s => s.id === serverId) || list.find(s => s.isDefault) || null
    }
    readonly property bool limited: mode === "relay" && server !== null && server.ok === true
    readonly property string limitError: {
        if (!limited)
            return ""
        if (server.maxFile > 0 && totalBytes > server.maxFile)
            return "Too big for " + server.name + ": it takes up to " + mb(server.maxFile) + " per share."
        if (server.quota > 0 && server.used + totalBytes > server.quota)
            return "Not enough space left on " + server.name + " (" + mb(Math.max(0, server.quota - server.used)) + " free)."
        if (server.maxActive > 0 && server.active >= server.maxActive)
            return "You already have " + server.active + " shares on " + server.name + ", its limit."
        return ""
    }
    property int now: Math.floor(Date.now() / 1000)
    readonly property string secretError: protection === "none" ? "" : share.checkSecret(protection, secret.text)

    // Responsive: as wide as the panel allows (never off the window), and as tall as the window leaves room for;
    // what does not fit scrolls inside instead of running off the screen.
    // Inside the open panel, centred there: nothing hangs out of it, and the panel (kept tall enough by Main) never hides behind.
    x: shell.panelRect.x + (shell.panelRect.width - width) / 2
    y: shell.panelRect.y + Math.max(16, (shell.panelRect.height - height) / 2)
    width: Math.min(view === "ready" ? 520 : view === "sessions" ? 460 : 440, shell.panelRect.width - 32)
    height: Math.min(implicitHeight, Math.max(240, shell.height - 48))
    // The panel behind steps back instead of competing with the dialog (only the panel is dimmed, not the desktop around it).
    modal: true
    dim: true
    Overlay.modal: Item {
        Rectangle {
            x: shell.panelRect.x
            y: shell.panelRect.y
            width: shell.panelRect.width
            height: shell.panelRect.height
            radius: 22
            color: Qt.rgba(0, 0, 0, 0.5)
        }
        Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 120 } }
    }
    padding: 16
    focus: true
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 130 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 160; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: 90 }
    }

    function openFor(newIds) {
        ids = newIds
        hasVault = newIds.some(id => notch.itemInfo(id).vault)
        totalBytes = newIds.reduce((sum, id) => sum + notch.itemInfo(id).size, 0)
        serverId = share.lastServerId
        if (share.relayConfigured)
            share.testAllServers() // limits and usage up to date (not worth a network round trip without a server)
        mode = share.lastMode === "relay" && share.relayConfigured ? "relay" : "local"
        ttl = share.defaultTtl
        protection = "none"
        tried = false
        nearbyId = ""
        optionsOpen = false
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
    function debugBegin(pin) { // pictures only: share on "This WiFi" with an optional PIN
        mode = "local"
        protection = pin !== "" ? "pin" : "none"
        secret.text = pin
        single = false
        begin()
    }
    function begin() {
        if (mode === "nearby") {
            if (nearby.send(ids, nearbyId) !== "")
                view = "sessions"
            return
        }
        tried = true
        if (secretError !== "")
            return
        readyId = share.start(ids, mode, ttl, protection, secret.text, single, address, serverId)
        secret.clear()
        view = readyId !== "" ? "ready" : "sessions"
    }
    function mb(bytes) { return bytes >= 10 * 1048576 ? Math.round(bytes / 1048576) + " MB" : (bytes / 1048576).toFixed(1) + " MB" }
    // The longest expiry this server's rules allow (or the longest we offer).
    function ttlAllowed(seconds) { return !limited || server.maxTtl <= 0 || seconds <= server.maxTtl }
    onServerChanged: if (server && limited && server.maxTtl > 0 && ttl > server.maxTtl) ttl = 600
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
    Connections {
        target: share
        function onCountChanged() { if (pop.view === "ready" && share.count === 0) pop.view = "sessions" } // nothing left to show
    }

    Timer {
        interval: 1000
        repeat: true
        running: pop.visible && share.activeCount > 0
        onTriggered: pop.now = Math.floor(Date.now() / 1000)
    }

    background: Rectangle { radius: theme.radiusLg; color: theme.surface; border.color: theme.outline }

    contentItem: Flickable {
        id: scroller
        implicitHeight: column.implicitHeight
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ColumnLayout {
        id: column
        width: scroller.width
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            IconButton {
                visible: pop.view === "ready" || (pop.view === "sessions" && pop.ids.length > 0)
                iconName: "back"
                Accessible.name: pop.view === "ready" ? "Back to the list" : "Back to the new share"
                onClicked: pop.view = pop.view === "ready" ? "sessions" : "setup"
            }
            SectionLabel { text: pop.view === "setup" ? "Share via QR" : pop.view === "ready" ? "Ready to scan" : "Sharing now" }
            Item { Layout.fillWidth: true }
            PillButton {
                iconName: "laptop"
                text: nearby.receiving ? "Receiving · " + pop.countdown(nearby.receiveUntil) : "Receive"
                checked: nearby.receiving
                onClicked: pop.receiveError = nearby.receiving ? (nearby.stopReceiving(), "") : nearby.startReceiving()
            }
            PillButton {
                visible: pop.view === "setup" && share.count + nearby.count > 0
                text: "Active (" + (share.count + nearby.count) + ")"
                onClicked: pop.view = "sessions"
            }
            PillButton {
                visible: pop.view === "sessions" && pop.ids.length > 0
                text: "New share"
                onClicked: pop.view = "setup"
            }
            IconButton { iconName: "close"; Accessible.name: "Close"; onClicked: pop.close() }
        }

        Caption {
            Layout.fillWidth: true
            visible: nearby.receiving
            text: "Visible to Olyzano Notch computers on this network as “" + nearby.deviceName + "”. Whoever sends to you needs your OK."
        }
        Caption {
            Layout.fillWidth: true
            visible: pop.receiveError !== ""
            color: theme.danger
            text: pop.receiveError
        }

        // ---- setup ----
        ColumnLayout {
            visible: pop.view === "setup"
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText // a name can look like markup
                text: pop.ids.length === 1 ? notch.itemInfo(pop.ids[0]).name : pop.ids.length + " files"
                color: theme.text
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }
            Segmented {
                Layout.fillWidth: true
                model: [["local", "This WiFi"], ["relay", "My server"], ["nearby", "Nearby"]]
                value: pop.mode
                onPicked: v => { pop.mode = v; if (v === "nearby") { pop.protection = "none"; nearby.scan() } }
            }
            Caption {
                Layout.fillWidth: true
                visible: pop.ids.length > 1
                text: "Whoever scans sees the list and can take one file, a few, or all as a zip."
            }
            Caption {
                Layout.fillWidth: true
                text: pop.mode === "nearby"
                      ? "Send to a computer running Olyzano Notch on this network. They press Receive, you both compare a 4-digit code, and they press Accept. Files are encrypted on the way."
                      : pop.mode === "local"
                      ? "Your computer serves the files to phones on the same WiFi. Not encrypted, and it works only while Olyzano Notch runs."
                        + (notch.platform === "windows" ? " The first time, Windows asks to let Olyzano Notch through its firewall: allow it on private networks." : "")
                      : share.relayConfigured
                        ? "Encrypted here, then stored on your server. The server cannot read it, and your computer can be switched off."
                        : "Set up your server in Settings → Sharing first."
            }
            // nearby computers
            ColumnLayout {
                visible: pop.mode === "nearby"
                Layout.fillWidth: true
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Caption {
                        Layout.fillWidth: true
                        text: nearby.scanning ? "Looking around…" : nearby.devices.length === 0 ? "No computer found." : "Computers nearby"
                    }
                    PillButton { text: "Look again"; onClicked: nearby.scan() }
                }
                ProgressBar { Layout.fillWidth: true; visible: nearby.scanning; indeterminate: true }
                Repeater {
                    model: nearby.devices
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 40
                        radius: theme.radiusMd
                        color: theme.card
                        border.width: 1
                        border.color: pop.nearbyId === modelData.id ? theme.accent : theme.outline
                        RowLayout {
                            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                            spacing: 8
                            LineIcon { glyph: "laptop"; sourceSize: Qt.size(16, 16); color: pop.nearbyId === modelData.id ? theme.accent : theme.textDim }
                            Text { Layout.fillWidth: true; textFormat: Text.PlainText; text: modelData.name; color: theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                            Text { text: modelData.os === "windows" ? "Windows" : modelData.os === "linux" ? "Linux" : ""; color: theme.textDim; font.pixelSize: 11 }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: pop.nearbyId = modelData.id }
                    }
                }
                Caption {
                    Layout.fillWidth: true
                    visible: !nearby.scanning && nearby.devices.length === 0
                    text: "On the other computer, open Olyzano Notch and press Receive. Both computers must be on the same network (guest WiFi often blocks this)."
                }
            }
            // which server, when there is a choice
            RowLayout {
                visible: pop.mode === "relay" && share.servers.length > 1
                Layout.fillWidth: true
                spacing: 6
                Caption { text: "Server" }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: share.servers
                        PillButton {
                            required property var modelData
                            text: modelData.name
                            iconName: "server"
                            checked: pop.server !== null && pop.server.id === modelData.id
                            onClicked: pop.serverId = modelData.id
                        }
                    }
                }
            }
            Caption {
                Layout.fillWidth: true
                visible: pop.mode === "relay" && pop.server !== null && pop.server.tested && !pop.server.ok
                color: theme.danger
                textFormat: Text.PlainText // a name can look like markup
                text: pop.server ? pop.server.name + ": " + pop.server.message : ""
            }
            Caption {
                Layout.fillWidth: true
                visible: pop.limitError !== ""
                color: theme.danger
                text: pop.limitError
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
            // Expiry and protection: closed by default, the summary says what they are set to.
            RowLayout {
                visible: pop.mode !== "nearby"
                Layout.fillWidth: true
                Caption {
                    Layout.fillWidth: true
                    text: "Expires in " + (({ 600: "10 min", 3600: "1 hour", 86400: "24 hours" })[pop.ttl] || Math.round(pop.ttl / 60) + " min")
                          + " · " + (pop.protection === "none" ? "no PIN or password" : pop.protection === "pin" ? "PIN" : "password")
                }
                PillButton { text: pop.optionsShown ? "Fewer options" : "Options"; checked: pop.optionsShown; onClicked: pop.optionsOpen = !pop.optionsOpen }
            }
            ColumnLayout {
                visible: pop.mode !== "nearby" && pop.optionsShown
                Layout.fillWidth: true
                spacing: 10
                Segmented {
                    visible: pop.mode !== "nearby"
                    Layout.fillWidth: true
                    model: [[600, "10 min"], [3600, "1 hour"], [86400, "24 hours"]].filter(m => pop.ttlAllowed(m[0]))
                    value: pop.ttl
                    onPicked: v => { pop.ttl = v; share.defaultTtl = v }
                }

                SectionLabel { text: "Protect with"; visible: pop.mode !== "nearby" }
                Segmented {
                    visible: pop.mode !== "nearby"
                    Layout.fillWidth: true
                    model: [["none", "Nothing"], ["pin", "PIN"], ["password", "Password"]]
                    value: pop.protection
                    onPicked: v => pop.protection = v
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
                                                        : "Checked by Olyzano Notch: 5 wrong tries lock that phone out for 10 minutes.")
                                : (pop.mode === "relay" ? "It also locks the key inside the link, so the link alone opens nothing. Tell it by another route."
                                                        : "Checked by Olyzano Notch: 5 wrong tries lock that phone out for 10 minutes.")
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
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: pop.close() }
                PillButton {
                    text: pop.mode === "nearby" ? "Send" : pop.hasVault && pop.mode === "local" ? "Share anyway" : "Share"
                    checked: true
                    enabled: pop.mode === "nearby" ? pop.nearbyId !== ""
                           : pop.mode === "local" ? pop.addresses.length > 0 : (share.relayConfigured && pop.limitError === "")
                    onClicked: pop.begin()
                }
            }
        }

        // ---- ready: the QR on the left, the link on the right ----
        ColumnLayout {
            visible: pop.view === "ready"
            Layout.fillWidth: true
            spacing: 10
            Repeater {
                model: share
                delegate: GridLayout {
                    id: rd
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
                    required property string serverName
                    required property bool active
                    readonly property var snapshot: ({ status: status, progress: progress, expiresAt: expiresAt, downloads: downloads, message: message })
                    visible: sessionId === pop.readyId
                    Layout.fillWidth: true
                    columns: pop.width >= 480 ? 2 : 1
                    columnSpacing: 20
                    rowSpacing: 14

                    Rectangle { // the code (a calm placeholder with the progress while it is being made)
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        implicitWidth: 188
                        implicitHeight: 188
                        radius: 12
                        color: rd.status === "ready" ? "white" : theme.well
                        border.width: rd.status === "ready" ? 0 : 1
                        border.color: theme.outline
                        Image {
                            visible: rd.status === "ready"
                            anchors.centerIn: parent
                            width: 172
                            height: 172
                            sourceSize: Qt.size(172, 172)
                            fillMode: Image.PreserveAspectFit
                            smooth: false
                            cache: false
                            source: rd.status === "ready" ? pop.qrSource(rd.url) : ""
                        }
                        ColumnLayout {
                            visible: rd.status !== "ready"
                            anchors.centerIn: parent
                            width: parent.width - 32
                            spacing: 10
                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                text: pop.statusText(rd.snapshot)
                                color: rd.status === "error" ? theme.danger : theme.textDim
                                font.pixelSize: 12
                            }
                            ProgressBar {
                                Layout.fillWidth: true
                                visible: rd.status === "preparing" || rd.status === "uploading"
                                indeterminate: rd.status === "preparing" || rd.progress < 0
                                value: Math.max(0, rd.progress)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            LineIcon {
                                glyph: rd.mode === "relay" ? "server" : "wifi"
                                sourceSize: Qt.size(16, 16)
                                color: rd.active ? theme.accent : theme.textDim
                            }
                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText // a name can look like markup
                                text: rd.name
                                color: theme.text
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                elide: Text.ElideMiddle
                            }
                            LineIcon {
                                visible: rd.protection !== ""
                                glyph: "lock"
                                sourceSize: Qt.size(13, 13)
                                color: theme.textDim
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (rd.fileCount > 1 ? rd.fileCount + " files" : "1 file")
                                  + (rd.serverName !== "" && share.servers.length > 1 ? " · " + rd.serverName : "")
                                  + (rd.protection === "pin" ? " · PIN required" : rd.protection === "password" ? " · password required" : "")
                            color: theme.textDim
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: pop.statusText(rd.snapshot)
                            color: rd.status === "error" ? theme.danger : rd.active ? theme.text : theme.textDim
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                        Caption {
                            Layout.fillWidth: true
                            visible: rd.status === "ready"
                            text: (rd.mode === "local" ? "Phone and computer must be on the same WiFi." : "Encrypted: only people with this exact link can open it.")
                                  + (rd.protection === "pin" ? " They also need the PIN." : rd.protection === "password" ? " They also need the password." : "")
                                  + (rd.mode === "local" && rd.fileCount > 1 ? " Stays open until it expires or you press Stop." : "")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: rd.status === "ready"
                            spacing: 6
                            TextField {
                                id: readyLink
                                Layout.fillWidth: true
                                implicitHeight: 30
                                leftPadding: 10
                                readOnly: true
                                text: rd.url
                                color: theme.textDim
                                font.pixelSize: 11
                                selectByMouse: true
                                background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: theme.outline }
                            }
                            PillButton { text: "Copy"; onClicked: { readyLink.selectAll(); readyLink.copy(); readyLink.deselect() } }
                        }
                        Item { Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 4 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            PillButton { visible: rd.active; text: "Stop sharing"; onClicked: share.stop(rd.sessionId) }
                            PillButton { visible: !rd.active; text: "Back to the list"; onClicked: pop.view = "sessions" }
                            Item { Layout.fillWidth: true }
                            PillButton { text: "Done"; checked: true; onClicked: pop.close() }
                        }
                    }
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
                visible: share.count + nearby.count === 0
                text: "Nothing is being shared. Right-click a file → Share via QR."
            }

            Flickable {
                Layout.fillWidth: true
                visible: share.count > 0
                implicitHeight: list.implicitHeight // the whole popup scrolls
                interactive: false

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
                            required property string serverName
                            required property bool active
                            readonly property var snapshot: ({ status: status, progress: progress, expiresAt: expiresAt, downloads: downloads, message: message })
                            Layout.fillWidth: true
                            implicitHeight: body.implicitHeight + 20
                            radius: theme.radiusMd
                            color: theme.card
                            border.width: 1
                            border.color: theme.outline

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
                                                textFormat: Text.PlainText // a name can look like markup
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
                                            text: pop.statusText(card.snapshot) + (card.serverName !== "" && share.servers.length > 1 ? " · " + card.serverName : "")
                                            color: card.status === "error" ? theme.danger : theme.textDim
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                    PillButton {
                                        visible: card.status === "ready"
                                        text: "QR"
                                        onClicked: { pop.readyId = card.sessionId; pop.view = "ready" }
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

                            }
                        }
                    }
                }
            }

            // transfers with other computers (Nearby)
            ColumnLayout {
                Layout.fillWidth: true
                visible: nearby.count > 0
                spacing: 8
                Repeater {
                    model: nearby
                    delegate: Rectangle {
                        id: nb
                        required property string transferId
                        required property string direction
                        required property string peer
                        required property string state
                        required property string code
                        required property double progress
                        required property string message
                        required property bool active
                        readonly property bool moving: state === "sending" || state === "receiving"
                        Layout.fillWidth: true
                        implicitHeight: nbBody.implicitHeight + 20
                        radius: theme.radiusMd
                        color: theme.card
                        border.width: 1
                        border.color: state === "asking" ? theme.accent : theme.outline
                        ColumnLayout {
                            id: nbBody
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                LineIcon { glyph: "laptop"; sourceSize: Qt.size(16, 16); color: nb.active ? theme.accent : theme.textDim }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        Layout.fillWidth: true
                                        text: (nb.direction === "out" ? "To " : "From ") + nb.peer
                                        color: theme.text
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        color: nb.state === "failed" ? theme.danger : theme.textDim
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        text: nb.state === "connecting" ? "Connecting…"
                                              : nb.state === "waiting" ? "Waiting for them to accept"
                                              : nb.state === "asking" ? "Wants to send you files"
                                              : nb.moving ? (nb.direction === "out" ? "Sending " : "Receiving ") + Math.round(nb.progress * 100) + "%"
                                              : nb.state === "done" ? "✓ " + nb.message : nb.message
                                    }
                                }
                                PillButton { visible: nb.state === "asking"; text: "Accept"; checked: true; onClicked: nearby.accept(nb.transferId) }
                                PillButton { visible: nb.state === "asking"; text: "Decline"; onClicked: nearby.decline(nb.transferId) }
                                PillButton { visible: nb.active && nb.state !== "asking"; text: "Stop"; onClicked: nearby.cancel(nb.transferId) }
                                IconButton { visible: !nb.active; iconName: "close"; Accessible.name: "Remove from the list"; onClicked: nearby.dismiss(nb.transferId) }
                            }
                            // the number both people compare
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                visible: nb.code !== "" && (nb.state === "waiting" || nb.state === "asking")
                                text: nb.code
                                color: theme.text
                                font.family: theme.displayFont !== "" ? theme.displayFont : Qt.application.font.family
                                font.pixelSize: 34
                                font.letterSpacing: 8
                            }
                            Caption {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                visible: nb.code !== "" && (nb.state === "waiting" || nb.state === "asking")
                                text: "It must be the same number on both screens."
                            }
                            ProgressBar { Layout.fillWidth: true; visible: nb.moving; value: nb.progress }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: share.count + nearby.count > 0
                PillButton {
                    visible: share.activeCount + nearby.activeCount > 1
                    text: "Stop all"
                    onClicked: { share.stopAll(); nearby.cancelAll() }
                }
                PillButton {
                    visible: share.count > share.activeCount || nearby.count > nearby.activeCount
                    text: "Clear finished"
                    onClicked: { share.clearFinished(); nearby.clearFinished() }
                }
                Item { Layout.fillWidth: true }
                PillButton { text: "Close"; checked: true; onClicked: pop.close() }
            }
        }
        }
    }
}
