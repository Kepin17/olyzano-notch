import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ComboBox {
    id: choice
    implicitHeight: theme.controlHeight
    leftPadding: 12
    rightPadding: 28
    font.pixelSize: 13
    contentItem: Text {
        text: choice.displayText
        textFormat: Text.PlainText // a name can come from outside (a window's title) and look like markup, with pictures it would fetch
        color: choice.enabled ? theme.text : theme.textFaint
        font: choice.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Text {
        x: choice.width - width - 10
        anchors.verticalCenter: parent.verticalCenter
        text: "⌄"
        color: theme.textDim
    }
    background: Rectangle {
        radius: theme.radiusSm
        color: choice.hovered ? theme.surfaceHover : theme.surface
        border.width: choice.activeFocus || theme.washi ? 1 : 0
        border.color: choice.activeFocus ? theme.accent : theme.outline
    }
    delegate: ItemDelegate {
        required property var modelData
        required property int index
        width: choice.width
        implicitHeight: 32
        highlighted: choice.highlightedIndex === index
        contentItem: Text {
            text: choice.textRole ? modelData[choice.textRole] : modelData
            textFormat: Text.PlainText
            color: theme.text
            font.pixelSize: 13
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: parent.highlighted ? Qt.alpha(theme.accent, 0.16) : "transparent"
            radius: theme.radiusSm
        }
    }
    // The shell (Main's window) keeps the panel open and the whole window clickable while a list is open.
    property bool reported: false
    property var reportedTo: null
    function report(on) {
        if (on === reported)
            return
        const shell = on ? Window.window : reportedTo
        if (!shell || shell.choicesOpen === undefined)
            return
        reported = on
        reportedTo = on ? shell : null
        shell.choicesOpen += on ? 1 : -1
    }
    Component.onDestruction: report(false)
    popup: Popup {
        onVisibleChanged: choice.report(visible)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        y: choice.height + 6
        width: choice.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 220)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: choice.delegateModel
            currentIndex: choice.highlightedIndex
            ScrollBar.vertical: ScrollBar {}
        }
        background: Rectangle {
            color: theme.surface
            radius: theme.radiusMd
            border.width: 1
            border.color: theme.outline
        }
    }
}
