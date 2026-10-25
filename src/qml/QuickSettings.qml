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
    onOpenChanged: if (open) quick.refresh()

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
                LineIcon { anchors.centerIn: parent; glyph: tile.icon; sourceSize: Qt.size(17, 17); color: tile.on ? theme.onColor(theme.accent) : theme.text }
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
        Layout.fillWidth: true
        implicitHeight: 30
        background: Rectangle { radius: 8; color: row.hovered ? card.tileOff : "transparent" }
        contentItem: RowLayout {
            spacing: 8
            Text { Layout.fillWidth: true; text: row.label; color: theme.text; font.pixelSize: 12; elide: Text.ElideRight; leftPadding: 6 }
            Text { text: row.note; color: theme.textDim; font.pixelSize: 11 }
            Rectangle { visible: row.mark; Layout.preferredWidth: 7; Layout.preferredHeight: 7; radius: 3.5; color: theme.accent; Layout.rightMargin: 6 }
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
                title: "Wi-Fi"
                subtitle: !quick.wifiEnabled ? "Off" : quick.wifiName !== "" ? quick.wifiName : "Not connected"
                on: quick.wifiEnabled
                expanded: card.wifiOpen
                onToggled: quick.setWifiEnabled(!quick.wifiEnabled)
                onExpand: { card.wifiOpen = !card.wifiOpen; card.btOpen = false }
            }
            Tile {
                visible: quick.btAvailable
                icon: "bluetooth"
                title: "Bluetooth"
                subtitle: !quick.btEnabled ? "Off" : quick.btConnected !== "" ? quick.btConnected : "On"
                on: quick.btEnabled
                expanded: card.btOpen
                onToggled: quick.setBtEnabled(!quick.btEnabled)
                onExpand: { card.btOpen = !card.btOpen; card.wifiOpen = false }
            }
        }
        ColumnLayout {
            visible: card.wifiOpen && quick.wifiAvailable
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: quick.wifiNetworks
                Row2 {
                    required property var modelData
                    label: modelData.name
                    mark: modelData.active
                    onClicked: quick.connectWifi(modelData.id)
                }
            }
            Text { visible: quick.wifiNetworks.length === 0; text: "No saved networks."; color: theme.textDim; font.pixelSize: 11; leftPadding: 6 }
            Text { text: "Join a new network in the system settings."; color: theme.textFaint; font.pixelSize: 10; leftPadding: 6; topPadding: 4 }
        }
        ColumnLayout {
            visible: card.btOpen && quick.btAvailable
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: quick.btDevices
                Row2 {
                    required property var modelData
                    label: modelData.name
                    note: modelData.connected ? "Connected" : ""
                    mark: modelData.connected
                    onClicked: quick.toggleBtDevice(modelData.path)
                }
            }
            Text { visible: quick.btDevices.length === 0; text: "No paired devices."; color: theme.textDim; font.pixelSize: 11; leftPadding: 6 }
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
