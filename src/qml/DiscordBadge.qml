import QtQuick

// Discord on Windows, shown while in a voice call (Activities.discordState):
//   a green ring    in the call
//   a filled green  your microphone hears a voice
//   a red slashed microphone   you are muted
//   red slashed headphones     you are deafened
// Hidden when there is no call.
Item {
    id: badge
    readonly property string call: activities.discordState

    visible: call !== "off"
    implicitWidth: 14
    implicitHeight: 14

    Rectangle {
        visible: badge.call === "call" || badge.call === "talking"
        anchors.fill: parent
        radius: width / 2
        color: badge.call === "talking" ? theme.success : "transparent"
        border.width: 1.6
        border.color: theme.success
        Behavior on color { enabled: activities.animations; ColorAnimation { duration: 90 } }
    }

    LineIcon {
        visible: badge.call === "muted"
        anchors.centerIn: parent
        glyph: "mic-off"
        sourceSize: Qt.size(14, 14)
        color: theme.danger
    }

    Item {
        visible: badge.call === "deafened"
        anchors.fill: parent
        LineIcon {
            anchors.centerIn: parent
            glyph: "headphones"
            sourceSize: Qt.size(14, 14)
            color: theme.danger
        }
        Rectangle { // the slash
            anchors.centerIn: parent
            width: 17
            height: 1.6
            radius: 1
            rotation: -45
            color: theme.danger
        }
    }
}
