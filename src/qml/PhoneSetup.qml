import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Camera → Phone: choose an Android phone to show in a window of its own. The window (PhoneWindow.qml) is a normal window you can move and resize
// anywhere on the screen; this page only finds the phone. Everything goes through adb, so USB debugging (or wireless debugging) must be on.
ColumnLayout {
    id: setup
    property var shell: null
    readonly property bool modalOpen: false
    spacing: 10
    Component.onCompleted: androidMirror.refresh()

    Text { text: "Show an Android phone"; color: theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
    Caption { Layout.fillWidth: true; text: "The phone's screen opens in a window you can move and resize. Click to tap, drag to swipe, and use the buttons for Back, Home and the volume. Nothing is installed on the phone." }

    FormCard {
        visible: !androidMirror.available
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: androidMirror.problem; wrapMode: Text.Wrap; color: theme.warn; font.pixelSize: 12 }
        PillButton { text: "Look again"; iconName: "undo"; onClicked: androidMirror.refresh() }
    }

    FormCard {
        visible: androidMirror.available
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "Phones"; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
            PillButton { text: androidMirror.busy ? "Looking…" : "Look again"; iconName: "undo"; enabled: !androidMirror.busy; onClicked: androidMirror.refresh() }
        }
        Caption { visible: androidMirror.devices.length === 0; Layout.fillWidth: true; text: "No phone found. Plug it in, turn on USB debugging (Settings → Developer options) and press Look again, or connect without a cable below." }
        Repeater {
            model: androidMirror.devices
            delegate: RowLayout {
                id: row
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { Layout.fillWidth: true; text: row.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 13 }
                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: row.modelData.usable ? theme.textDim : theme.warn
                        font.pixelSize: 11
                        text: (row.modelData.wifi ? "Wireless" : "USB") + " · " + (row.modelData.state === "device" ? "ready" : row.modelData.state === "unauthorized" ? "tap Allow on the phone's screen"
                                                                                  : row.modelData.state === "offline" ? "offline: unplug and plug it again" : "not allowed (check the cable and the udev rules)")
                    }
                }
                PillButton { visible: row.modelData.usable; primary: true; text: androidMirror.serial === row.modelData.serial && androidMirror.windowOpen ? "Open again" : "Show"; iconName: "pen"; onClicked: androidMirror.mirror(row.modelData.serial) }
            }
        }
    }

    FormCard {
        visible: androidMirror.available
        Layout.fillWidth: true
        Text { text: "Without a cable (Android 11 or newer)"; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
        Caption { Layout.fillWidth: true; text: "On the phone: Settings → Developer options → Wireless debugging, on the same WiFi. Tap Pair device with pairing code, type what it shows here, and press Pair. Then type the address from the main Wireless debugging screen and press Connect." }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Field { id: pairAddress; label: "Pairing address"; placeholder: "192.168.1.20:37099" }
            Field { id: pairCode; label: "Code"; placeholder: "123456"; Layout.maximumWidth: 90 }
            PillButton { Layout.alignment: Qt.AlignBottom; text: "Pair"; enabled: !androidMirror.busy && pairAddress.text.trim() !== "" && pairCode.text.trim() !== ""; onClicked: { androidMirror.pair(pairAddress.text, pairCode.text); pairCode.clear() } }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Field { id: connectAddress; label: "Address"; placeholder: "192.168.1.20:5555" }
            PillButton { Layout.alignment: Qt.AlignBottom; text: "Connect"; enabled: !androidMirror.busy && connectAddress.text.trim() !== ""; onClicked: androidMirror.connectTo(connectAddress.text) }
        }
    }
    Text { visible: androidMirror.error !== ""; Layout.fillWidth: true; text: androidMirror.error; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 12 }
    Caption { visible: androidMirror.status !== "" && androidMirror.error === ""; Layout.fillWidth: true; text: androidMirror.status }
    Caption { Layout.fillWidth: true; text: "Needs adb (the Android platform tools) and ffmpeg on this computer. The picture is sent over adb, not over the internet; some apps (banking, video) show a black screen on purpose." }
}
