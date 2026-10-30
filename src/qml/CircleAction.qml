import QtQuick
import QtQuick.Controls.Basic

// A round button with a line icon: `primary` fills it with the accent. For the timer's controls.
AbstractButton {
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    id: rb
    property string iconName
    property bool primary: false
    property int diameter: primary ? 52 : 44
    implicitWidth: diameter
    implicitHeight: diameter
    opacity: enabled ? 1 : 0.4
    scale: down ? 0.94 : 1
    Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 90 } }
    background: Rectangle {
        radius: width / 2
        color: rb.primary ? (rb.hovered ? Qt.lighter(theme.accent, 1.08) : theme.accent) : rb.down ? theme.surfacePressed : rb.hovered ? theme.surfaceHover : theme.surface
        border.width: theme.washi && !rb.primary ? 1 : 0
        border.color: theme.outline
        Behavior on color { enabled: activities.animations; ColorAnimation { duration: 120 } }
    }
    contentItem: Item {
        LineIcon {
            anchors.centerIn: parent
            glyph: rb.iconName
            sourceSize: Qt.size(rb.primary ? 24 : 20, rb.primary ? 24 : 20)
            color: rb.primary ? theme.onColor(theme.accent) : theme.text
        }
    }
}
