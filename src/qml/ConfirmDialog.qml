import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Themed confirm dialog — Popup + PillButton, no system Dialog chrome.
// Default: modal, centered on the Overlay. Callers set title / body / confirmText
// and connect to confirmed; the action itself stays at the call site.
Popup {
    id: dlg
    property string title: ""
    property string body: ""
    property string confirmText: "Delete"
    signal confirmed

    parent: Overlay.overlay
    x: Math.max(12, (Overlay.overlay.width - width) / 2)
    y: Math.max(48, (Overlay.overlay.height - height) / 2)
    width: 320
    padding: 16
    modal: true
    dim: false
    Overlay.modal: Item {}
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: theme.radiusLg
        color: theme.surface
        border.width: 1
        border.color: theme.outline
    }
    contentItem: ColumnLayout {
        spacing: 12
        Text {
            text: dlg.title
            color: theme.text
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: dlg.body
            wrapMode: Text.Wrap
            color: theme.textDim
            font.pixelSize: 12
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PillButton {
                text: "Cancel"
                onClicked: dlg.close()
            }
            PillButton {
                text: dlg.confirmText
                checked: true
                onClicked: {
                    dlg.confirmed()
                    dlg.close()
                }
            }
        }
    }
}
