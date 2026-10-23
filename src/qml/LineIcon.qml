import QtQuick
import QtQuick.Controls.impl

// One of the line icons in icons/ (black strokes), tinted to `color`.
IconImage {
    property string glyph
    source: glyph !== "" ? "icons/" + glyph + ".svg" : ""
    sourceSize: Qt.size(16, 16)
    color: theme.text
}
