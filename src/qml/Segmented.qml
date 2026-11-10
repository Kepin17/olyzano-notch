import QtQuick
import QtQuick.Controls.Basic

// A row of exclusive choices on one rounded track with a highlight that slides to the chosen one.
// `model` is a list of [value, label]; `value` is the chosen value.
Item {
    id: seg
    property var model: []
    property var value
    signal picked(var value)
    readonly property int index: { for (let i = 0; i < model.length; ++i) if (model[i][0] === value) return i; return 0 }
    implicitHeight: theme.controlHeight + 2
    implicitWidth: Math.max(160, model.length * 78)

    Rectangle {
        anchors.fill: parent
        radius: theme.washi ? theme.chipRadius : theme.radiusSm + 2
        color: theme.surface
        border.width: theme.washi ? 1 : 0
        border.color: theme.outline
    }
    Rectangle { // the chosen one: the solid accent, with text that reads on it (the same as a switch that is on)
        readonly property real cell: (seg.width - 6) / Math.max(1, seg.model.length)
        x: 3 + seg.index * cell
        y: 3
        width: cell
        height: seg.height - 6
        radius: theme.washi ? Math.max(2, theme.chipRadius - 2) : theme.radiusSm
        color: theme.washi ? theme.accent : theme.surfacePressed // a raised tile, not the accent: that is kept for the main action
        border.width: 0
        Behavior on x { enabled: activities.animations; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    }
    Row {
        x: 3; y: 3
        width: seg.width - 6
        height: seg.height - 6
        Repeater {
            model: seg.model
            AbstractButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                required property var modelData
                required property int index
                width: parent.width / seg.model.length
                height: parent.height
                // solid empty bg — `null` lets Basic style paint a white pressed block
                background: Rectangle { color: "transparent" }
                onClicked: { seg.value = modelData[0]; seg.picked(modelData[0]) }
                contentItem: Text {
                    text: modelData[1]
                    color: index === seg.index ? (theme.washi ? theme.onColor(theme.accent) : theme.text) : hovered ? theme.text : theme.textDim
                    font.pixelSize: 13
                    font.weight: index === seg.index ? Font.DemiBold : Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
