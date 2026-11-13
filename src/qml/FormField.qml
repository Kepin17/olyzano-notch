import QtQuick
import QtQuick.Controls.Basic

// A one-line box for the Forms pages.
TextField {
    id: f
    implicitHeight: 34
    leftPadding: 12
    rightPadding: 12
    placeholderTextColor: theme.placeholder
    color: theme.text
    font.pixelSize: 13
    selectByMouse: true
    background: Rectangle { radius: theme.radiusSm; color: theme.surface; border.width: 1; border.color: f.activeFocus ? theme.accent : theme.hairline }
}
