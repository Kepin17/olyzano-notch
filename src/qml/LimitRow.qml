import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A limit you set: drag the slider, or type the number (empty or 0 = no limit). The slider reaches 30; the box takes more.
RowLayout {
    id: row
    property string title
    property int value: 0
    signal edited(int value)
    Layout.fillWidth: true
    spacing: 12

    Text { text: row.title; color: theme.text; font.pixelSize: 13; Layout.preferredWidth: 84; elide: Text.ElideRight }
    Slider {
        id: slider
        Layout.fillWidth: true
        implicitHeight: 28
        from: 0
        to: 30
        stepSize: 1
        onMoved: row.edited(Math.round(value))
        Accessible.name: row.title
        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 6
            radius: 3
            color: theme.well
            border.width: 1
            border.color: theme.hairline
            Rectangle { width: Math.max(0, slider.visualPosition * parent.width); height: parent.height; radius: 3; color: row.value > 0 ? theme.accent : "transparent" }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 18
            height: 18
            radius: 9
            color: "#ffffff"
            border.width: 2
            border.color: row.value > 0 ? theme.accent : theme.hairline
        }
    }
    Binding { target: slider; property: "value"; value: Math.min(30, row.value) }
    TextField {
        id: field
        Layout.preferredWidth: 78
        Layout.preferredHeight: 30
        horizontalAlignment: TextInput.AlignHCenter
        placeholderText: "No limit"
        placeholderTextColor: theme.placeholder
        color: theme.text
        selectionColor: theme.accent
        font.pixelSize: 12
        inputMethodHints: Qt.ImhDigitsOnly
        validator: IntValidator { bottom: 0; top: 10000 }
        background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: field.activeFocus ? theme.accent : theme.hairline }
        onTextEdited: row.edited(text === "" ? 0 : parseInt(text))
        Accessible.name: row.title + " (a number)"
    }
    Binding { target: field; property: "text"; value: row.value > 0 ? String(row.value) : ""; when: !field.activeFocus }
}
