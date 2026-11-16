import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The phone, in a window of its own: a normal window, so it can be put anywhere on the screen, moved and resized freely (the picture keeps its shape).
// A click is a tap, a drag a swipe, holding a long press, the wheel scrolls. Opened by Camera → Phone → Show (AndroidMirror), closed with its own
// close button, which also stops the picture.
Window {
    id: win
    objectName: "phoneWindow"
    visible: androidMirror.windowOpen
    title: androidMirror.deviceName !== "" ? "Phone · " + androidMirror.deviceName : "Phone"
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
    minimumWidth: 300
    minimumHeight: 460
    width: 400
    height: 800
    color: theme.surface
    property bool typing: false
    property bool placed: false
    onVisibleChanged: if (visible && !placed) { // the first time, in the middle of the screen; after that it is wherever the person puts it
        placed = true
        const sh = Math.min(Screen.desktopAvailableHeight > 0 ? Screen.desktopAvailableHeight : 1000, 1000)
        height = Math.round(sh * 0.86)
        width = Math.max(minimumWidth, Math.round(height * 0.5))
        x = Math.round((Screen.width - width) / 2)
        y = Math.round((Screen.height - height) / 2)
    }
    onClosing: (close) => { androidMirror.close(); typing = false }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Flow {
            Layout.fillWidth: true
            spacing: 4
            PillButton { iconName: "back"; text: "Back"; onClicked: androidMirror.key("back") }
            PillButton { text: "Home"; onClicked: androidMirror.key("home") }
            PillButton { text: "Recents"; onClicked: androidMirror.key("recents") }
            PillButton { text: "Vol −"; onClicked: androidMirror.key("volume-down") }
            PillButton { text: "Vol +"; onClicked: androidMirror.key("volume-up") }
            PillButton { text: "Power"; Accessible.description: "Turns the phone's screen on or off"; onClicked: androidMirror.key("power") }
            PillButton { iconName: "pen"; text: "Type"; checked: win.typing; onClicked: { win.typing = !win.typing; if (win.typing) typeField.forceActiveFocus() } }
            PillButton { iconName: "camera"; text: "Screenshot"; enabled: androidMirror.state === "mirroring" && !androidMirror.busy; onClicked: androidMirror.screenshot() }
            PillButton {
                text: androidMirror.recording ? "Stop recording" : "Record"
                checked: androidMirror.recording
                enabled: androidMirror.state === "mirroring"
                onClicked: androidMirror.recording ? androidMirror.stopRecording() : androidMirror.startRecording()
            }
        }

        Rectangle {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: "#000"
            clip: true
            Image {
                id: pic
                anchors.fill: parent
                anchors.margins: 2
                cache: false
                asynchronous: false
                fillMode: Image.PreserveAspectFit
                smooth: true
                source: androidMirror.revision > 0 ? "image://phone/" + androidMirror.revision : ""
                visible: androidMirror.revision > 0
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: androidMirror.error !== "" ? theme.danger : "#aab"
                font.pixelSize: 13
                textFormat: Text.PlainText
                visible: androidMirror.state !== "mirroring" || androidMirror.revision === 0
                text: androidMirror.error !== "" ? androidMirror.error : androidMirror.state === "starting" ? (androidMirror.status !== "" ? androidMirror.status : "Starting…") : "Waiting for the phone's picture…"
            }
            // where the picture really is inside the item (it keeps its shape, so there are bars at the sides or above and below)
            readonly property real pw: pic.paintedWidth
            readonly property real ph: pic.paintedHeight
            readonly property real px: pic.x + (pic.width - pw) / 2
            readonly property real py: pic.y + (pic.height - ph) / 2
            MouseArea {
                id: touch
                anchors.fill: parent
                enabled: androidMirror.state === "mirroring"
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                property real sx: 0
                property real sy: 0
                property double t0: 0
                property bool inside: false
                function norm(v, origin, size) { return size > 0 ? (v - origin) / size : -1 }
                function onPicture(x, y) { const nx = norm(x, stage.px, stage.pw), ny = norm(y, stage.py, stage.ph); return nx >= 0 && nx <= 1 && ny >= 0 && ny <= 1 }
                onPressed: (m) => {
                    if (m.button === Qt.RightButton) { androidMirror.key("back"); return } // a right click is Back, like the back gesture
                    sx = m.x; sy = m.y; t0 = Date.now(); inside = onPicture(m.x, m.y)
                }
                onReleased: (m) => {
                    if (m.button !== Qt.LeftButton || !inside) return
                    const ms = Date.now() - t0, d = Math.hypot(m.x - sx, m.y - sy)
                    const ax = norm(sx, stage.px, stage.pw), ay = norm(sy, stage.py, stage.ph), bx = norm(m.x, stage.px, stage.pw), by = norm(m.y, stage.py, stage.ph)
                    if (d < 8 && ms < 450) androidMirror.tap(ax, ay)
                    else if (d < 8) androidMirror.swipe(ax, ay, ax, ay, Math.min(ms, 3000)) // held still: a long press
                    else androidMirror.swipe(ax, ay, bx, by, Math.max(80, Math.min(ms, 900)))
                }
                property double lastWheel: 0
                onWheel: (w) => {
                    if (!onPicture(w.x, w.y) || Date.now() - lastWheel < 160) return
                    lastWheel = Date.now()
                    const x = norm(w.x, stage.px, stage.pw), y = norm(w.y, stage.py, stage.ph), run = 0.22 // the page moves with the fingers: wheel down = finger up
                    androidMirror.swipe(x, y, x, Math.max(0, Math.min(1, y + (w.angleDelta.y < 0 ? -run : run))), 160)
                }
            }
        }

        RowLayout {
            visible: win.typing
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: typeField
                Layout.fillWidth: true
                placeholderText: "Type here, press Enter to send it to the phone"
                placeholderTextColor: theme.textFaint
                color: theme.text
                font.pixelSize: 12
                background: Rectangle { radius: theme.radiusSm; color: theme.well; border.width: 1; border.color: typeField.activeFocus ? Qt.alpha(theme.accent, 0.45) : theme.hairline }
                onAccepted: { if (text !== "") { androidMirror.typeText(text); text = "" } }
                Keys.onPressed: (e) => { if (e.key === Qt.Key_Backspace && text === "") { androidMirror.key("backspace"); e.accepted = true } }
            }
            PillButton { text: "Send"; enabled: typeField.text !== ""; onClicked: typeField.accepted() }
            PillButton { text: "Enter"; onClicked: androidMirror.key("enter") }
        }
        Text {
            Layout.fillWidth: true
            visible: androidMirror.error !== "" && androidMirror.state === "mirroring"
            text: androidMirror.error
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: theme.danger
            font.pixelSize: 11
            TapHandler { onTapped: androidMirror.clearError() }
        }
    }
}
