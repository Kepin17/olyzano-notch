import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Shared popup menu. Uses solid high-contrast fills (not theme.surface alone —
// on light palettes surface ≈ well and the menu vanishes into the panel).
// Palette is forced so Basic-style MenuItems cannot paint system-grey text.
Menu {
    id: menu
    padding: 8

    // Solid, readable surfaces for both modes
    readonly property color menuBg: theme.dark ? "#232326" : "#ffffff"
    readonly property color menuFg: theme.dark ? "#f2f2f7" : "#111111"
    readonly property color menuFgDim: theme.dark ? "#8e8e93" : "#6e6e73"
    readonly property color menuEdge: theme.dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.16)
    readonly property color menuHover: theme.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06)

    // Force Control palette (Basic still reads these for some chrome)
    palette.window: menuBg
    palette.windowText: menuFg
    palette.base: menuBg
    palette.text: menuFg
    palette.buttonText: menuFg
    palette.button: menuBg
    palette.highlight: theme.accent
    palette.highlightedText: theme.dark ? "#ffffff" : "#ffffff"
    palette.toolTipBase: menuBg
    palette.toolTipText: menuFg

    background: Rectangle {
        radius: theme.radiusMd
        color: menu.menuBg
        border.width: 1
        border.color: menu.menuEdge
        // soft drop ring
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            z: -1
            radius: parent.radius + 2
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(0, 0, 0, theme.dark ? 0.35 : 0.08)
        }
    }

    delegate: MenuItem {
        id: mi
        implicitHeight: 32
        leftPadding: 10
        rightPadding: 10
        indicator: null
        hoverEnabled: true

        // Force every piece of text chrome
        contentItem: Text {
            text: mi.text
            color: mi.enabled ? menu.menuFg : menu.menuFgDim
            font.pixelSize: 12
            font.weight: Font.Medium
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            leftPadding: 2
        }
        background: Rectangle {
            radius: theme.radiusSm
            color: mi.highlighted && mi.enabled ? Qt.alpha(theme.accent, theme.dark ? 0.35 : 0.2) : "transparent"
            border.width: mi.highlighted && mi.enabled ? 1 : 0
            border.color: Qt.alpha(theme.accent, 0.55)
        }
    }
}
