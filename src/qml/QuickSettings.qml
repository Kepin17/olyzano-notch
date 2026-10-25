import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Quick Settings: a glass card under the right end of the bar. Wi-Fi and Bluetooth tiles (tap to switch, the chevron
// lists saved networks / paired devices), sound with its output, brightness, the power mode, and who is listening.
Rectangle {
    id: card
    property bool open: false
    readonly property bool wifiList: wifiOpen
    property bool wifiOpen: false
    property bool btOpen: false
    property bool soundOpen: false
    property bool inputOpen: false
    property string askFor: "" // the network whose password is being typed
    property string detailFor: "" // the network whose details are shown
    property bool ethOpen: false
    function qrSource(text) { return "image://qr/" + Qt.btoa(text).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "") }
    function closeDetail() { detailFor = ""; quick.forgetWifiPassword() }
    function syncWatch() {
        quick.watchWifi(open && wifiOpen)
        quick.watchBluetooth(open && btOpen)
    }
    onWifiOpenChanged: { askFor = ""; if (!wifiOpen) closeDetail(); syncWatch() }
    onBtOpenChanged: syncWatch()
    width: 372
    implicitHeight: body.implicitHeight + 28
    height: implicitHeight
    radius: theme.radiusLg
    color: theme.dark ? Qt.rgba(0.09, 0.09, 0.10, Math.min(1, topbar.opacity + 0.25)) : Qt.rgba(1, 1, 1, Math.min(1, topbar.opacity + 0.25))
    border.width: 1
    border.color: theme.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.12)
    opacity: open ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 140 } }
    onOpenChanged: {
        if (open) quick.refresh()
        else { askFor = ""; closeDetail(); wifiOpen = false; btOpen = false; ethOpen = false; soundOpen = false; inputOpen = false } // closing the card folds its lists
        syncWatch()
    }

    readonly property color tileOff: theme.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.06)
    readonly property color tileOn: theme.accentSoft

    component Chev: LineIcon { glyph: "chevron"; sourceSize: Qt.size(14, 14); color: theme.textDim }

    component Tile: Rectangle {
        id: tile
        property string icon
        property string title
        property string subtitle
        property bool on: false
        property bool expanded: false
        property int signalBars: -1 // 0-4: draw Wi-Fi bars instead of the icon
        property bool signalOff: false
        signal toggled()
        signal expand()
        Layout.fillWidth: true
        implicitHeight: 58
        radius: 16
        color: on ? card.tileOn : card.tileOff
        border.width: on ? 1 : 0
        border.color: theme.accent
        Behavior on color { ColorAnimation { duration: 120 } }
        MouseArea { anchors.fill: parent; onClicked: tile.toggled() }
        RowLayout {
            anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
            spacing: 9
            Rectangle {
                Layout.preferredWidth: 34; Layout.preferredHeight: 34
                radius: 17
                color: tile.on ? theme.accent : Qt.rgba(1, 1, 1, theme.dark ? 0.10 : 0)
                border.width: tile.on || theme.dark ? 0 : 1
                border.color: theme.outline
                LineIcon { visible: tile.signalBars < 0; anchors.centerIn: parent; glyph: tile.icon; sourceSize: Qt.size(17, 17); color: tile.on ? theme.onColor(theme.accent) : theme.text }
                SignalIcon { visible: tile.signalBars >= 0; anchors.centerIn: parent; size: 17; bars: tile.signalBars; off: tile.signalOff; color: tile.on ? theme.onColor(theme.accent) : theme.text }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text { Layout.fillWidth: true; text: tile.title; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                Text { Layout.fillWidth: true; text: tile.subtitle; color: theme.textDim; font.pixelSize: 11; elide: Text.ElideRight }
            }
            AbstractButton {
                implicitWidth: 28; implicitHeight: 28
                onClicked: tile.expand()
                contentItem: Item { Chev { anchors.centerIn: parent; rotation: tile.expanded ? 180 : 0; Behavior on rotation { NumberAnimation { duration: 140 } } } }
            }
        }
    }

    component Row2: AbstractButton {
        id: row
        property string label
        property string note
        property bool mark: false
        property string glyph
        property real glyphOpacity: 1
        property int bars: -1 // Wi-Fi bars in front of the label
        property bool hasInfo: false
        signal infoClicked()
        Layout.fillWidth: true
        implicitHeight: 30
        background: Rectangle { radius: 8; color: row.hovered ? card.tileOff : "transparent" }
        contentItem: RowLayout {
            spacing: 8
            LineIcon { visible: row.glyph !== "" && row.bars < 0; Layout.leftMargin: 6; glyph: row.glyph; opacity: row.glyphOpacity; sourceSize: Qt.size(15, 15); color: theme.text }
            SignalIcon { visible: row.bars >= 0; Layout.leftMargin: 6; size: 15; bars: row.bars; color: theme.text }
            Text { Layout.fillWidth: true; text: row.label; color: theme.text; font.pixelSize: 12; elide: Text.ElideRight; leftPadding: row.glyph !== "" || row.bars >= 0 ? 0 : 6 }
            Text { text: row.note; color: theme.textDim; font.pixelSize: 11 }
            Rectangle { visible: row.mark; Layout.preferredWidth: 7; Layout.preferredHeight: 7; radius: 3.5; color: theme.accent; Layout.rightMargin: row.hasInfo ? 0 : 6 }
            AbstractButton {
                visible: row.hasInfo
                implicitWidth: 26; implicitHeight: 26
                Accessible.name: "Details"
                onClicked: row.infoClicked()
                background: Rectangle { radius: 13; color: parent.hovered ? card.tileOff : "transparent" }
                contentItem: Item { LineIcon { anchors.centerIn: parent; glyph: "info"; sourceSize: Qt.size(15, 15); color: theme.textDim } }
            }
        }
    }

    // A list of facts: the name on the left, its value on the right.
    component Facts: ColumnLayout {
        property var rows: [] // [[label, value], ...]; empty values are left out
        spacing: 3
        Layout.fillWidth: true
        Repeater {
            model: parent.rows.filter(r => r[1] !== "" && r[1] !== undefined && r[1] !== null)
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 10
                Text { Layout.preferredWidth: 104; text: modelData[0]; color: theme.textDim; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                Text { Layout.fillWidth: true; text: modelData[1]; color: theme.text; font.pixelSize: 11; elide: Text.ElideRight; font.family: /^[0-9a-f:.\/, ]+$/i.test(String(modelData[1])) ? "monospace" : Qt.application.font.family }
            }
        }
    }

    component Track: Slider {
        id: s
        Layout.fillWidth: true
        implicitHeight: 24
        background: Rectangle {
            x: s.leftPadding; y: s.topPadding + (s.availableHeight - height) / 2
            width: s.availableWidth; height: 6; radius: 3
            color: theme.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.12)
            Rectangle { width: s.visualPosition * parent.width; height: parent.height; radius: 3; color: theme.accent }
        }
        handle: Rectangle {
            x: s.leftPadding + s.visualPosition * (s.availableWidth - width); y: s.topPadding + (s.availableHeight - height) / 2
            width: 16; height: 16; radius: 8
            color: "white"
            border.width: 1; border.color: Qt.rgba(0, 0, 0, 0.25)
        }
    }

    ColumnLayout {
        id: body
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
        spacing: 12

        // Wi-Fi and Bluetooth
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: quick.wifiAvailable || quick.btAvailable
            Tile {
                visible: quick.wifiAvailable
                icon: "wifi"
                signalBars: quick.wifiEnabled ? quick.wifiBars : 0
                signalOff: !quick.wifiEnabled || quick.wifiName === ""
                title: "Wi-Fi"
                subtitle: !quick.wifiEnabled ? "Off" : quick.wifiName !== "" ? quick.wifiName : "Not connected"
                on: quick.wifiEnabled
                expanded: card.wifiOpen
                onToggled: quick.setWifiEnabled(!quick.wifiEnabled)
                onExpand: { card.wifiOpen = !card.wifiOpen; card.btOpen = false; card.ethOpen = false }
            }
            Tile {
                visible: quick.btAvailable
                icon: "bluetooth"
                title: "Bluetooth"
                subtitle: !quick.btEnabled ? "Off" : quick.btConnected !== "" ? quick.btConnected : "On"
                on: quick.btEnabled
                expanded: card.btOpen
                onToggled: quick.setBtEnabled(!quick.btEnabled)
                onExpand: { card.btOpen = !card.btOpen; card.wifiOpen = false; card.ethOpen = false }
            }
        }
        RowLayout { // Ethernet
            visible: quick.ethAvailable
            Layout.fillWidth: true
            spacing: 10
            Tile {
                icon: "ethernet"
                title: "Ethernet"
                subtitle: quick.ethState === "connected" ? (quick.ethInfo.speed > 0 ? quick.ethInfo.speed + " Mb/s" : "Connected") : quick.ethState === "unplugged" ? "Cable unplugged"
                         : quick.ethState === "connecting" ? "Connecting…" : "Off"
                on: quick.ethState === "connected" || quick.ethState === "connecting"
                expanded: card.ethOpen
                onToggled: quick.setEthernet(!(quick.ethState === "connected" || quick.ethState === "connecting"))
                onExpand: { card.ethOpen = !card.ethOpen; card.wifiOpen = false; card.btOpen = false }
            }
        }
        Facts {
            visible: card.ethOpen && quick.ethAvailable
            rows: [["Connection", quick.ethInfo.name], ["Interface", quick.ethInfo.device], ["Speed", quick.ethInfo.speed > 0 ? quick.ethInfo.speed + " Mb/s" : ""],
                   ["IP address", quick.ethInfo.ip], ["Gateway", quick.ethInfo.gateway], ["DNS", quick.ethInfo.dns], ["MAC address", quick.ethInfo.mac]]
        }
        Flickable {
            visible: card.wifiOpen && quick.wifiAvailable && card.detailFor === ""
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(wifiCol.implicitHeight, 200)
            contentHeight: wifiCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: wifiCol
                width: parent.width
                spacing: 0
                Repeater {
                    model: quick.wifiNetworks
                    ColumnLayout {
                        id: net
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 0
                        Row2 {
                            label: net.modelData.name
                            bars: net.modelData.bars
                            hasInfo: net.modelData.saved || net.modelData.active
                            onInfoClicked: card.detailFor = net.modelData.name
                            note: net.modelData.active ? "Connected" : net.modelData.saved ? "Saved" : net.modelData.enterprise ? "System settings" : net.modelData.secure ? "Password" : "Open"
                            mark: net.modelData.active
                            onClicked: {
                                if (net.modelData.active) quick.disconnectWifi()
                                else if (net.modelData.saved) quick.connectWifi(net.modelData.id)
                                else if (net.modelData.enterprise) quick.openSystemSettings()
                                else if (net.modelData.secure) card.askFor = card.askFor === net.modelData.name ? "" : net.modelData.name
                                else quick.joinWifi(net.modelData.ap, net.modelData.name, "")
                            }
                        }
                        RowLayout {
                            visible: card.askFor === net.modelData.name
                            Layout.fillWidth: true
                            Layout.leftMargin: 6
                            spacing: 6
                            TextField {
                                id: pass
                                Layout.fillWidth: true
                                placeholderText: "Password"
                                echoMode: TextInput.Password
                                font.pixelSize: 12
                                color: theme.text
                                onVisibleChanged: if (visible) forceActiveFocus(); else text = ""
                                onAccepted: join.clicked()
                                background: Rectangle { radius: 8; color: card.tileOff; border.width: pass.activeFocus ? 1 : 0; border.color: theme.accent }
                            }
                            Button {
                                id: join
                                text: "Join"
                                enabled: pass.text.length >= 8
                                onClicked: { quick.joinWifi(net.modelData.ap, net.modelData.name, pass.text); card.askFor = "" }
                            }
                        }
                    }
                }
                Text { visible: quick.wifiNetworks.length === 0; text: "Looking for networks..."; color: theme.textDim; font.pixelSize: 11; leftPadding: 6 }
            }
        }
        // Wi-Fi details: everything about the network, and its password as a QR code to scan
        ColumnLayout {
            id: wifiDetail
            visible: card.wifiOpen && quick.wifiAvailable && card.detailFor !== ""
            Layout.fillWidth: true
            spacing: 8
            readonly property var net: quick.wifiNetworks.find(n => n.name === card.detailFor) || null
            readonly property bool active: net !== null && net.active
            readonly property var info: active ? quick.wifiInfo : ({})
            readonly property bool mine: net !== null && net.id !== "" && quick.wifiSecretFor === net.id
            RowLayout {
                Layout.fillWidth: true
                AbstractButton {
                    implicitHeight: 26; implicitWidth: backRow.implicitWidth + 12
                    onClicked: card.closeDetail()
                    background: Rectangle { radius: 13; color: parent.hovered ? card.tileOff : "transparent" }
                    contentItem: Row {
                        id: backRow
                        spacing: 4
                        LineIcon { glyph: "chevron"; rotation: 90; sourceSize: Qt.size(14, 14); color: theme.textDim; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: "Networks"; color: theme.textDim; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                Item { Layout.fillWidth: true }
                SignalIcon { size: 15; bars: wifiDetail.net ? wifiDetail.net.bars : 0; color: theme.text }
                Text { text: card.detailFor; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.maximumWidth: 200 }
            }
            Facts {
                rows: wifiDetail.net === null ? [] : [
                    ["Status", wifiDetail.active ? "Connected" : "Saved"],
                    ["Signal strength", (wifiDetail.active ? wifiDetail.info.signal : wifiDetail.net.strength) + "%"],
                    ["Security", wifiDetail.active ? wifiDetail.info.security : wifiDetail.net.security],
                    ["Frequency", wifiDetail.active ? (wifiDetail.info.frequency / 1000).toFixed(3) + " GHz (Channel " + wifiDetail.info.channel + ")"
                                                     : (wifiDetail.net.frequency / 1000).toFixed(3) + " GHz"],
                    ["BSSID", wifiDetail.info.bssid],
                    ["Max bit rate", wifiDetail.info.bitrate > 0 ? wifiDetail.info.bitrate + " Mb/s" : ""],
                    ["IP address", wifiDetail.info.ip], ["Gateway", wifiDetail.info.gateway], ["DNS", wifiDetail.info.dns],
                    ["MAC address", wifiDetail.info.mac]]
            }
            // the password, only when asked for
            Rectangle {
                visible: wifiDetail.net !== null && wifiDetail.net.saved
                Layout.fillWidth: true
                implicitHeight: pass.implicitHeight + 20
                radius: 14
                color: card.tileOff
                ColumnLayout {
                    id: pass
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                    spacing: 8
                    PillButton {
                        visible: !wifiDetail.mine
                        Layout.alignment: Qt.AlignHCenter
                        text: "Show password and QR code"
                        iconName: "eye"
                        onClicked: quick.requestWifiPassword(wifiDetail.net.id)
                    }
                    Text {
                        visible: wifiDetail.mine && quick.wifiSecretBusy
                        Layout.alignment: Qt.AlignHCenter
                        text: "Waiting for the system's permission…"
                        color: theme.textDim; font.pixelSize: 11
                    }
                    Text {
                        visible: wifiDetail.mine && quick.wifiSecretError !== ""
                        Layout.fillWidth: true
                        text: quick.wifiSecretError
                        color: theme.danger; font.pixelSize: 11; wrapMode: Text.Wrap
                    }
                    ColumnLayout {
                        visible: wifiDetail.mine && quick.wifiSecret !== ""
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: "Scan this QR code with another device to connect to the \u201c" + card.detailFor + "\u201d network."
                            color: theme.textDim; font.pixelSize: 11
                        }
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: 196; implicitHeight: 196
                            radius: 10
                            color: "white"
                            Image {
                                anchors.centerIn: parent
                                width: 180; height: 180
                                sourceSize: Qt.size(180, 180)
                                smooth: false
                                cache: false
                                source: wifiDetail.mine && quick.wifiSecret !== "" ? card.qrSource(quick.wifiQr(card.detailFor, quick.wifiSecret, true)) : ""
                            }
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 8
                            Text { text: "Wi-Fi password:"; color: theme.textDim; font.pixelSize: 12 }
                            Text { text: quick.wifiSecret; color: theme.text; font.pixelSize: 13; font.family: "monospace"; font.weight: Font.DemiBold }
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 8
                            PillButton { text: "Copy"; iconName: "copy"; onClicked: notch.copyText(quick.wifiSecret) }
                            PillButton { text: "Hide"; iconName: "eye-off"; onClicked: quick.forgetWifiPassword() }
                        }
                    }
                }
            }
        }
        Flickable {
            visible: card.btOpen && quick.btAvailable
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(btCol.implicitHeight, 200)
            contentHeight: btCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: btCol
                width: parent.width
                spacing: 0
                Repeater {
                    model: quick.btDevices
                    Row2 {
                        required property var modelData
                        label: modelData.name
                        glyph: modelData.icon.indexOf("audio") === 0 ? "headphones" : "bluetooth"
                        note: modelData.connected ? "Connected" : modelData.paired ? "Paired" : "Pair"
                        mark: modelData.connected
                        onClicked: modelData.paired ? quick.toggleBtDevice(modelData.path) : quick.pairBtDevice(modelData.path)
                    }
                }
                Text { visible: quick.btDevices.length === 0; text: quick.btEnabled ? "Looking for devices..." : "Bluetooth is off."; color: theme.textDim; font.pixelSize: 11; leftPadding: 6 }
            }
        }

        // sound
        ColumnLayout {
            visible: quick.audioAvailable
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                AbstractButton {
                    implicitWidth: 28; implicitHeight: 28
                    onClicked: quick.toggleMute()
                    contentItem: Item { LineIcon { anchors.centerIn: parent; glyph: quick.muted || quick.volume === 0 ? "volume-off" : "volume"; sourceSize: Qt.size(19, 19); color: quick.muted ? theme.danger : theme.text } }
                }
                Track { from: 0; to: 100; stepSize: 1; value: quick.volume; onMoved: quick.setVolume(Math.round(value)) }
                Text { Layout.preferredWidth: 34; text: quick.volume + "%"; color: theme.textDim; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
            }
            AbstractButton {
                Layout.fillWidth: true
                implicitHeight: 28
                visible: quick.sinks.length > 1
                onClicked: card.soundOpen = !card.soundOpen
                contentItem: RowLayout {
                    spacing: 6
                    Text { Layout.fillWidth: true; text: "Output: " + quick.sinkName; color: theme.textDim; font.pixelSize: 11; elide: Text.ElideRight; leftPadding: 38 }
                    Chev { rotation: card.soundOpen ? 180 : 0 }
                }
            }
            ColumnLayout {
                visible: card.soundOpen
                Layout.fillWidth: true
                Layout.leftMargin: 32
                spacing: 0
                Repeater {
                    model: quick.sinks
                    Row2 { required property var modelData; label: modelData.description; mark: modelData.current; onClicked: quick.setSink(modelData.name) }
                }
            }
        }

        // the microphone: its level, mute, and which one
        ColumnLayout {
            visible: quick.inputAvailable
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                AbstractButton {
                    implicitWidth: 28; implicitHeight: 28
                    onClicked: quick.toggleInputMute()
                    contentItem: Item { LineIcon { anchors.centerIn: parent; glyph: quick.inputMuted ? "mic-off" : "mic"; sourceSize: Qt.size(19, 19); color: quick.inputMuted ? theme.danger : theme.text } }
                }
                Track { from: 0; to: 100; stepSize: 1; value: quick.inputVolume; onMoved: quick.setInputVolume(Math.round(value)) }
                Text { Layout.preferredWidth: 34; text: quick.inputVolume + "%"; color: theme.textDim; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
            }
            AbstractButton {
                Layout.fillWidth: true
                implicitHeight: 28
                visible: quick.sources.length > 1
                onClicked: card.inputOpen = !card.inputOpen
                contentItem: RowLayout {
                    spacing: 6
                    Text { Layout.fillWidth: true; text: "Input: " + quick.sourceName; color: theme.textDim; font.pixelSize: 11; elide: Text.ElideRight; leftPadding: 38 }
                    Chev { rotation: card.inputOpen ? 180 : 0 }
                }
            }
            ColumnLayout {
                visible: card.inputOpen
                Layout.fillWidth: true
                Layout.leftMargin: 32
                spacing: 0
                Repeater {
                    model: quick.sources
                    Row2 { required property var modelData; label: modelData.description; mark: modelData.current; onClicked: quick.setSource(modelData.name) }
                }
            }
        }

        // screen
        RowLayout {
            visible: quick.brightnessAvailable
            Layout.fillWidth: true
            spacing: 10
            Item { implicitWidth: 28; implicitHeight: 28; LineIcon { anchors.centerIn: parent; glyph: "sun"; sourceSize: Qt.size(19, 19); color: theme.text } }
            Track { from: 1; to: 100; stepSize: 1; value: quick.brightness; onMoved: quick.setBrightness(Math.round(value)) }
            Text { Layout.preferredWidth: 34; text: quick.brightness + "%"; color: theme.textDim; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
        }

        // power
        ColumnLayout {
            visible: systemEvents.batteryLevel >= 0 || quick.profilesAvailable
            Layout.fillWidth: true
            spacing: 8
            RowLayout {
                visible: systemEvents.batteryLevel >= 0
                Layout.fillWidth: true
                Text { text: (systemEvents.batteryCharging ? "⚡ " : "") + "Battery " + systemEvents.batteryLevel + "%"; color: theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                Item { Layout.fillWidth: true }
                Text {
                    visible: quick.batteryLeft !== ""
                    text: quick.batteryLeft + (systemEvents.batteryCharging ? " to full" : " left")
                    color: theme.textDim
                    font.pixelSize: 11
                }
            }
            Row {
                visible: quick.profilesAvailable
                spacing: 6
                Repeater {
                    model: quick.profiles
                    PillButton {
                        required property string modelData
                        text: modelData === "power-saver" ? "Power saver" : modelData === "performance" ? "Performance" : "Balanced"
                        checked: quick.profile === modelData
                        onClicked: quick.setProfile(modelData)
                    }
                }
            }
        }

        // who is listening
        ColumnLayout {
            visible: activities.micApps.length > 0 || activities.cameraApps.length > 0
            Layout.fillWidth: true
            spacing: 4
            Text { visible: activities.micApps.length > 0; text: "Microphone: " + activities.micApps.join(", "); color: theme.warn; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Text { visible: activities.cameraApps.length > 0; text: "Camera: " + activities.cameraApps.join(", "); color: theme.success; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PillButton { text: "System settings"; iconName: "gear"; onClicked: quick.openSystemSettings() }
        }
    }
}
