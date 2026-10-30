import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Labeled form field used across Dev tabs and Settings.
// Superset of the former SshTab/DbTab inline components: live `text` alias + onTextChanged
// sync (paste/autofill), not only textEdited.
ColumnLayout {
    id: fl
    property string label: ""
    property string placeholder: ""
    property string value: ""
    property bool mono: false
    property bool show: true
    property int echoMode: TextInput.Normal
    // live text always available for save, even if the draft binding lags
    readonly property alias text: tf.text
    signal edited(string text)
    visible: show
    Layout.fillWidth: true
    spacing: 4
    Text {
        text: fl.label
        color: theme.textDim
        font.pixelSize: 11
        font.weight: Font.Medium
    }
    TextField {
        id: tf
        Layout.fillWidth: true
        Layout.preferredHeight: 32
        text: fl.value
        placeholderText: fl.placeholder
        placeholderTextColor: theme.textFaint
        color: theme.text
        echoMode: fl.echoMode
        font.family: fl.mono ? "monospace" : ""
        font.pixelSize: 12
        leftPadding: 10
        rightPadding: 10
        background: Rectangle {
            radius: theme.radiusSm
            color: theme.well
            border.width: 1
            border.color: tf.activeFocus ? Qt.alpha(theme.accent, 0.45) : theme.hairline
        }
        onTextChanged: {
            // keep the draft in sync on every change (not only textEdited — paste/autofill too)
            if (text !== fl.value)
                fl.edited(text)
        }
    }
}
