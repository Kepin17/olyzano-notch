import QtQuick

// Wi-Fi signal as four rising bars: `bars` (0-4) of them lit, the rest faint. `off` dims all of them and strikes the
// icon through (Wi-Fi switched off or not connected). Drawn, so it stays sharp at any size.
Item {
    id: icon
    property int bars: 0
    property bool off: false
    property color color: theme.text
    property real size: 16
    implicitWidth: size
    implicitHeight: size
    Row {
        anchors.fill: parent
        spacing: icon.size * 0.08
        Repeater {
            model: 4
            Item {
                required property int index
                width: (icon.size - 3 * icon.size * 0.08) / 4
                height: icon.size
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: icon.size * (0.3 + 0.7 * (parent.index + 1) / 4)
                    radius: width / 2
                    color: icon.color
                    opacity: icon.off ? 0.22 : parent.index < icon.bars ? 1 : 0.25
                    Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 140 } }
                }
            }
        }
    }
    Rectangle { // the strike when there is no connection
        visible: icon.off
        anchors.centerIn: parent
        width: icon.size * 1.2
        height: 1.5
        radius: 1
        rotation: -45
        color: icon.color
        opacity: 0.8
    }
}
