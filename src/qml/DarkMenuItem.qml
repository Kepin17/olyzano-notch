import QtQuick
import QtQuick.Controls.Basic

// One row of a DarkMenu. A row added by hand (Instantiator) uses this too, so it looks like the others.
// `heading` makes it a small caption for the rows below: dim, never highlighted.
MenuItem {
    id: mi
    property bool heading: false
    readonly property color fg: theme.dark ? "#f5f5f7" : "#111111"
    readonly property color fgDim: theme.dark ? "#8e8e93" : "#6e6e73"
    readonly property color bg: theme.dark ? "#2c2c2e" : "#ffffff"
    readonly property color hoverBg: theme.dark ? "#3a3a3c" : "#e4e4e9"
    implicitWidth: (mi.menu ? mi.menu.width : 180) - 12
    implicitHeight: heading ? 24 : 30
    leftPadding: 10
    rightPadding: 10
    topPadding: 4
    bottomPadding: 4
    spacing: 0
    indicator: null
    arrow: null
    hoverEnabled: !heading
    enabled: !heading && !disabledByCaller
    property bool disabledByCaller: false
    // keep Basic from applying its own white "sunken" chrome
    down: false

    contentItem: Text {
        text: mi.text
        color: mi.heading ? mi.fgDim : (mi.enabled ? mi.fg : mi.fgDim)
        font.pixelSize: mi.heading ? 10 : 12
        font.weight: mi.heading ? Font.DemiBold : Font.Medium
        font.letterSpacing: mi.heading ? 0.8 : 0
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        anchors.fill: parent
        anchors.leftMargin: mi.leftPadding
        anchors.rightMargin: mi.rightPadding
    }

    Text { // a row that opens another menu
        visible: !!mi.subMenu
        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
        text: "›"
        color: mi.fgDim
        font.pixelSize: 14
    }

    // Always an opaque row: no transparency for a style highlight to show through
    background: Rectangle {
        radius: theme.radiusSm
        color: mi.highlighted && mi.enabled ? mi.hoverBg : mi.bg
    }
}
