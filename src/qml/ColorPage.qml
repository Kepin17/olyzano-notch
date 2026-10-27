import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Color: pick one from the screen; the last one is shown big with its HEX / RGB / HSL, and the ones before sit below.
// A click on any color copies it in the chosen format; a right-click removes it.
ColumnLayout {
    id: page
    required property var shell
    visible: shell.page === "color"
    Layout.fillWidth: true
    Layout.maximumWidth: 100000
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 14

    property string kind: "hex"
    property string copied: "" // the color just copied, for the check mark
    readonly property var colors: notch.recentColors
    readonly property string current: colors.length > 0 ? colors[0] : ""

    function copy(hex) {
        notch.copyText(colorPicker.format(hex, kind))
        copied = hex
        copiedTimer.restart()
        shell.infoText = "Copied " + colorPicker.format(hex, kind)
        infoResetter.restart()
    }
    Timer { id: copiedTimer; interval: 1400; onTriggered: page.copied = "" }
    Timer { id: infoResetter; interval: 1400; onTriggered: { if (shell.infoText.indexOf("Copied") === 0) shell.infoText = "" } }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        PillButton {
            visible: colorPicker.supported
            text: "Pick from screen"
            iconName: "eyedropper"
            checked: true
            implicitHeight: 34
            implicitWidth: 160
            onClicked: {
                // Get out of the way so anything on screen (even under Notch) can be picked
                // (not in "Always open": it would not come back).
                if (shell.capsuleMode !== "open") {
                    shell.pinned = false
                    shell.open = false
                }
                colorPicker.pick()
            }
        }
        Caption {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: !colorPicker.supported ? "Picking a color from the screen isn't available here."
                 : "The panel hides; click a pixel anywhere (Esc cancels)."
        }
        Segmented {
            visible: page.colors.length > 0
            implicitWidth: 176
            model: [["hex", "HEX"], ["rgb", "RGB"], ["hsl", "HSL"]]
            value: page.kind
            onPicked: v => page.kind = v
        }
    }

    // ---- the last one picked ----
    Rectangle {
        visible: page.current !== ""
        Layout.fillWidth: true
        implicitHeight: 92
        radius: theme.radiusLg
        color: page.current !== "" ? page.current : "transparent"
        border.width: 1
        border.color: theme.hairline
        readonly property color ink: page.current !== "" ? colorPicker.readableOn(page.current) : "white"
        HoverHandler { id: heroHover }
        TapHandler { onTapped: page.copy(page.current) }
        ColumnLayout {
            anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
            spacing: 2
            Text {
                text: page.current !== "" ? colorPicker.format(page.current, page.kind) : ""
                color: parent.parent.ink
                font.pixelSize: 24
                font.weight: Font.DemiBold
                font.family: "monospace"
            }
            Text {
                text: page.current !== "" ? (page.kind === "hex" ? colorPicker.format(page.current, "rgb") : colorPicker.format(page.current, "hex")) : ""
                color: parent.parent.ink
                opacity: 0.7
                font.pixelSize: 12
                font.family: "monospace"
            }
        }
        Row {
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            spacing: 6
            opacity: heroHover.hovered || page.copied === page.current ? 1 : 0.55
            Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 120 } }
            LineIcon {
                glyph: page.copied === page.current ? "check" : "copy"
                sourceSize: Qt.size(20, 20)
                color: parent.parent.ink
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: page.copied === page.current ? "Copied" : "Copy"
                color: parent.parent.ink
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }
    }

    // ---- the ones before ----
    Caption {
        visible: page.colors.length === 0
        Layout.fillWidth: true
        Layout.topMargin: 10
        horizontalAlignment: Text.AlignHCenter
        text: "No colors yet. Pick one from the screen: it is kept here, and a click copies it."
    }
    Flow {
        visible: page.colors.length > 1
        Layout.fillWidth: true
        spacing: 8
        Repeater {
            model: page.colors.slice(1)
            Rectangle {
                id: sw
                required property string modelData
                width: 44; height: 44
                radius: theme.radiusMd
                color: modelData
                border.width: swHover.hovered ? 2 : 1
                border.color: swHover.hovered ? theme.text : theme.hairline
                scale: swHover.hovered ? 1.06 : 1
                Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 120 } }
                HoverHandler { id: swHover }
                ToolTip.visible: swHover.hovered
                ToolTip.delay: 300
                ToolTip.text: colorPicker.format(sw.modelData, page.kind) + " · click to copy · right-click to remove"
                TapHandler { onTapped: page.copy(sw.modelData) }
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: notch.removeColor(sw.modelData) }
                LineIcon {
                    anchors.centerIn: parent
                    visible: page.copied === sw.modelData
                    glyph: "check"
                    color: colorPicker.readableOn(sw.modelData)
                    sourceSize: Qt.size(18, 18)
                }
            }
        }
    }
    Item { Layout.preferredHeight: 2 }
}
