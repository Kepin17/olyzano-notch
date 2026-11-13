import QtQuick

// The little tile that says what kind of question it is: a glyph on a soft tint.
Rectangle {
    property string glyph
    implicitWidth: 30
    implicitHeight: 30
    radius: 9
    color: theme.accentSoft
    Text { anchors.centerIn: parent; text: parent.glyph; color: theme.accentText; font.pixelSize: parent.glyph.length > 2 ? 11 : 14; font.weight: Font.DemiBold }
}
