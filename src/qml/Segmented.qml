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
    implicitHeight: 32
    implicitWidth: Math.max(160, model.length * 78)

    Rectangle {
        anchors.fill: parent
        radius: theme.washi ? theme.chipRadius : theme.radiusSm + 2
        color: theme.well
        border.width: 1
        border.color: theme.washi ? theme.outline : theme.hairline
    }
    Rectangle { // the highlight
        readonly property real cell: (seg.width - 6) / Math.max(1, seg.model.length)
        x: 3 + seg.index * cell
        y: 3
        width: cell
        height: seg.height - 6
        radius: theme.washi ? Math.max(2, theme.chipRadius - 2) : theme.radiusSm
        color: Qt.alpha(theme.accent, 0.18)
        border.width: 1
        border.color: Qt.alpha(theme.accent, 0.45)
        Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    }
    Row {
        x: 3; y: 3
        width: seg.width - 6
        height: seg.height - 6
        Repeater {
            model: seg.model
            AbstractButton {
                required property var modelData
                required property int index
                width: parent.width / seg.model.length
                height: parent.height
                onClicked: { seg.value = modelData[0]; seg.picked(modelData[0]) }
                contentItem: Text {
                    text: modelData[1]
                    color: index === seg.index ? theme.accent : hovered ? theme.text : theme.textSoft
                    font.pixelSize: 12
                    font.weight: index === seg.index ? Font.DemiBold : Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: null
            }
        }
    }
}
