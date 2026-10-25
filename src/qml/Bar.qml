import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The status bar: a glass strip along the top edge of each monitor with the app in front on the left, the status icons
// and the clock on the right, and the middle left clear for the notch. It stays (Always show, optionally keeping
// windows out from under it) or slides away until the pointer touches the top edge (Auto-hide). Its window is taller
// than the strip, with room for Quick Settings; only what is visible takes the pointer. Settings: Settings → Panel.
Window {
    id: root
    readonly property int stripH: topbar.height
    width: Screen.width > 0 ? Screen.width : 1920
    height: stripH + 480 // the strip, and room below it for the Quick Settings card
    visible: false
    color: "transparent"
    title: "Olyzano Notch Bar"

    property bool quickOpen: false // Quick Settings is out (a later step fills it in)
    property bool shown: topbar.behavior === "always"
    property bool edgeDwelled: false
    readonly property bool engaged: topbar.behavior === "always" || edgeDwelled || stripHover.hovered || quickOpen
    onEngagedChanged: {
        if (engaged) {
            hideTimer.stop()
            shown = true
        } else {
            hideTimer.restart()
        }
    }
    Connections {
        target: topbar
        function onChanged() { root.shown = topbar.behavior === "always" || root.engaged }
    }
    Timer { id: hideTimer; interval: 500; onTriggered: root.shown = false }
    Timer { id: dwellTimer; interval: 150; onTriggered: root.edgeDwelled = true }

    // ---- the pointer: only what is visible (and nothing at all over a full-screen game) ----
    readonly property rect maskRect: gaming.active ? Qt.rect(0, 0, 1, 1)
                                   : quickOpen ? Qt.rect(0, 0, width, height)
                                   : shown ? Qt.rect(0, 0, width, stripH)
                                   : Qt.rect(0, 0, width, 2)
    onMaskRectChanged: desktop.setInputRegion(root, maskRect)
    onVisibleChanged: if (visible) { desktop.setInputRegion(root, maskRect); desktop.setBlurRegion(root, blurRect, 0) }
    readonly property rect blurRect: shown && topbar.blur ? Qt.rect(0, 0, width, stripH) : Qt.rect(0, 0, 0, 0)
    onBlurRectChanged: desktop.setBlurRegion(root, blurRect, 0)

    // ---- the clock ----
    property date now: new Date()
    Timer {
        interval: topbar.showSeconds ? 1000 : 10000
        repeat: true
        running: root.visible
        triggeredOnStart: true
        onTriggered: root.now = new Date()
    }

    // ---- the strip ----
    Rectangle {
        id: strip
        x: 0
        y: root.shown ? 0 : -root.stripH
        width: root.width
        height: root.stripH
        color: theme.dark ? Qt.rgba(0.07, 0.07, 0.08, topbar.opacity) : Qt.rgba(1, 1, 1, topbar.opacity)
        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Rectangle { // the glass edge
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: theme.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)
        }
        HoverHandler { id: stripHover }

        // left: the app in front
        RowLayout {
            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
            width: Math.max(0, parent.width / 2 - 260)
            spacing: 10
            visible: topbar.showApp && dock.activeAppName !== ""
            Text {
                text: dock.activeAppName
                color: theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.maximumWidth: 220
            }
            Text {
                Layout.fillWidth: true
                text: dock.activeTitle
                color: theme.textDim
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        // right: status and the clock
        RowLayout {
            id: right
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            spacing: 12
            LineIcon { visible: activities.micApps.length > 0; glyph: "mic"; sourceSize: Qt.size(15, 15); color: theme.warn }
            LineIcon { visible: activities.cameraApps.length > 0; glyph: "camera"; sourceSize: Qt.size(15, 15); color: theme.success }
            // battery
            RowLayout {
                visible: systemEvents.batteryLevel >= 0
                spacing: 5
                Item {
                    implicitWidth: 22
                    implicitHeight: 11
                    Rectangle {
                        width: 20; height: 11; radius: 3
                        color: "transparent"
                        border.width: 1.2
                        border.color: theme.textDim
                        Rectangle {
                            x: 2; y: 2
                            height: parent.height - 4
                            radius: 1.5
                            width: Math.max(1.5, (parent.width - 4) * Math.max(0, systemEvents.batteryLevel) / 100)
                            color: systemEvents.batteryCharging ? theme.success : systemEvents.batteryLevel <= 20 ? theme.danger : theme.text
                        }
                    }
                    Rectangle { x: 20.5; y: 3.5; width: 1.5; height: 4; radius: 0.75; color: theme.textDim }
                }
                Text {
                    visible: topbar.showBattery
                    text: (systemEvents.batteryCharging ? "⚡" : "") + systemEvents.batteryLevel + "%"
                    color: theme.textDim
                    font.pixelSize: 12
                }
            }
            ColumnLayout {
                spacing: -1
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: topbar.showSeconds ? Qt.formatTime(root.now, "hh:mm:ss") : Qt.formatTime(root.now, Qt.locale().timeFormat(Locale.ShortFormat))
                    color: theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: topbar.showDate
                    Layout.alignment: Qt.AlignRight
                    text: Qt.formatDate(root.now, "ddd d MMM")
                    color: theme.textDim
                    font.pixelSize: 10
                }
            }
        }
    }

    // ---- the top edge, while the bar is away ----
    Item {
        x: 0
        width: root.width
        height: 2
        visible: !root.shown
        HoverHandler {
            onHoveredChanged: {
                if (hovered) {
                    dwellTimer.start()
                } else {
                    dwellTimer.stop()
                    root.edgeDwelled = false
                }
            }
        }
    }
}
