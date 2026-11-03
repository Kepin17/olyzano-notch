import QtQuick
import QtQuick.Controls.Basic

// Shared popup menu. Palette.highlight is forced to grey so the Basic style cannot
// paint a white system highlight on hover (that white block was covering the item).
Menu {
    id: menu
    padding: 6
    width: 180
    implicitWidth: 180

    readonly property color menuBg: theme.dark ? "#2c2c2e" : "#ffffff"
    readonly property color menuFg: theme.dark ? "#f5f5f7" : "#111111"
    readonly property color menuFgDim: theme.dark ? "#8e8e93" : "#6e6e73"
    readonly property color menuEdge: theme.dark ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(0, 0, 0, 0.2)
    readonly property color menuHoverBg: theme.dark ? "#3a3a3c" : "#e4e4e9"

    // Override every role the Basic style might use for a menu row
    palette.window: menuBg
    palette.windowText: menuFg
    palette.text: menuFg
    palette.buttonText: menuFg
    palette.button: menuBg
    palette.base: menuBg
    palette.alternateBase: menuBg
    palette.highlight: menuHoverBg          // NOT white / bright accent
    palette.highlightedText: menuFg
    palette.light: menuBg
    palette.dark: menuBg
    palette.mid: menuEdge
    palette.shadow: menuEdge
    palette.toolTipBase: menuBg
    palette.toolTipText: menuFg

    background: Rectangle {
        radius: theme.radiusMd
        color: menu.menuBg
        border.width: 1
        border.color: menu.menuEdge
    }

    delegate: DarkMenuItem {}
}
