import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ComboBox {
    id: choice
    implicitHeight: 28
    leftPadding: 9
    rightPadding: 24
    font.pixelSize: 12
    contentItem: Text {
        text: choice.displayText
        color: choice.enabled ? theme.text : theme.textFaint
        font: choice.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Text {
        x: choice.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        text: "⌄"
        color: theme.textDim
    }
    background: Rectangle {
        radius: theme.radiusSm
        color: theme.well
        border.width: 1
        border.color: choice.activeFocus ? theme.accent : theme.hairline
    }
    delegate: ItemDelegate {
        required property var modelData
        required property int index
        width: choice.width
        implicitHeight: 30
        highlighted: choice.highlightedIndex === index
        contentItem: Text {
            text: choice.textRole ? modelData[choice.textRole] : modelData
            color: theme.text
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: parent.highlighted ? Qt.alpha(theme.accent, 0.16) : "transparent"
            radius: theme.radiusSm
        }
    }
    popup: Popup {
        y: choice.height + 4
        width: choice.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 200)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: choice.popup.visible ? choice.delegateModel : null
            currentIndex: choice.highlightedIndex
            ScrollBar.vertical: ScrollBar {}
        }
        background: Rectangle {
            color: theme.surface
            radius: theme.radiusSm
            border.width: 1
            border.color: theme.outline
        }
    }
}
