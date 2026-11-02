import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

TextArea {
    Layout.fillWidth: true
    implicitHeight: contentHeight + topPadding + bottomPadding
    readOnly: true
    selectByMouse: true
    wrapMode: TextEdit.Wrap
    textFormat: TextEdit.PlainText
    color: theme.text
    font.pixelSize: 14 // the conversation is what you read most: roomy
    font.family: Qt.application.font.family
    padding: 0
    background: null
}
