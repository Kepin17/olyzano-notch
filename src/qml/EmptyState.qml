import QtQuick
import QtQuick.Layouts

// Centered empty-list placeholder ("No connections. Press + …").
// Anchors to the parent (the list container) and is shown/hidden by the caller's `visible`.
Text {
    property string message: ""
    anchors.centerIn: parent
    text: message
    color: theme.textFaint
    font.pixelSize: 11
    horizontalAlignment: Text.AlignHCenter
}
