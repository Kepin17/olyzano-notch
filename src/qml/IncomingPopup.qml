import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Someone nearby wants to send files. Shows the number to compare and asks Accept / Decline.
Popup {
    id: pop
    required property var shell
    property string transferId: ""
    property string from: ""
    property var files: []
    property double total: 0
    property string code: ""
    property bool lowDisk: false

    x: (shell.width - width) / 2
    y: 64
    width: 340
    padding: 16
    focus: true
    closePolicy: Popup.NoAutoClose

    function ask(id, who, list, size, sas, low) {
        transferId = id
        from = who
        files = list
        total = size
        code = sas
        lowDisk = low
        open()
    }
    function mb(bytes) { return bytes >= 10 * 1048576 ? Math.round(bytes / 1048576) + " MB" : (bytes / 1048576).toFixed(1) + " MB" }

    // Answered from the Share window, timed out or cancelled by the sender: nothing left to ask.
    Connections {
        target: nearby
        function onTransferUpdated(id, state) {
            if (id === pop.transferId && state !== "asking")
                pop.close()
        }
    }

    background: Rectangle { radius: theme.radiusLg; color: theme.surface; border.color: theme.outline }

    contentItem: ColumnLayout {
        spacing: 10
        SectionLabel { text: "Incoming files" }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.text
            font.pixelSize: 13
            text: pop.from + " wants to send you " + pop.files.length + (pop.files.length === 1 ? " file" : " files") + " (" + pop.mb(pop.total) + ")."
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: pop.code
            color: theme.text
            font.pixelSize: 40
            font.letterSpacing: 10
        }
        Caption { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "Accept only if the same number is on their screen." }
        Repeater {
            model: pop.files.slice(0, 4)
            delegate: Text {
                required property var modelData
                Layout.fillWidth: true
                text: modelData.name
                color: theme.textDim
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
        }
        Caption { Layout.fillWidth: true; visible: pop.files.length > 4; text: "and " + (pop.files.length - 4) + " more" }
        Caption { Layout.fillWidth: true; visible: pop.lowDisk; color: theme.danger; text: "There may not be enough free space for this." }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PillButton { text: "Decline"; onClicked: { nearby.decline(pop.transferId); pop.close() } }
            PillButton { text: "Accept"; checked: true; onClicked: { nearby.accept(pop.transferId); pop.close() } }
        }
    }
}
