import QtQuick

// A round badge with the first letter of a name, in the colour of that speaker.
Rectangle {
    id: av
    property string name
    property color tint: theme.accent
    property int size: 24
    implicitWidth: size
    implicitHeight: size
    radius: size / 2
    color: Qt.alpha(tint, 0.9)
    Text {
        anchors.centerIn: parent
        text: av.name.replace(/^Speaker\s*/, "").charAt(0).toUpperCase() || "?"
        color: theme.onColor(av.tint)
        font.pixelSize: av.size * 0.46
        font.weight: Font.DemiBold
    }
}
