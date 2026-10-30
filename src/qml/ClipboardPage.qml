import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Clipboard: what was copied lately (when Settings -> General -> Clipboard is on). A click copies it again; the icons pin it, keep
// it in Files or remove it.
ColumnLayout {
    id: page
    required property var shell
    visible: shell.page === "clipboard"
    Layout.fillWidth: true
    Layout.maximumWidth: 100000
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 10

    function ago(t) {
        const m = Math.floor((Date.now() - t.getTime()) / 60000)
        return m < 1 ? "now" : m < 60 ? m + " min" : m < 1440 ? Math.floor(m / 60) + " h" : Math.floor(m / 1440) + " d"
    }
    function say(text) {
        shell.say(text, 1400)
    }

    // Off: say what turning it on means.
    Rectangle {
        visible: !clipboardHistory.enabled
        Layout.fillWidth: true
        implicitHeight: offBody.implicitHeight + 28
        radius: theme.radiusMd
        color: theme.well
        border.width: 1
        border.color: theme.hairline
        ColumnLayout {
            id: offBody
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
            spacing: 8
            Text { text: "Keep what you copy"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.textDim
                font.pixelSize: 12
                text: "Olyzano Notch can remember the text and pictures you copy, so you can copy them again later. It stays on this computer. Anything you copy is kept, so a password could be too (apps that mark theirs as secret are skipped). Off by default."
            }
            PillButton { text: "Turn on"; iconName: "check"; checked: true; onClicked: clipboardHistory.enabled = true }
        }
    }

    RowLayout {
        visible: clipboardHistory.enabled
        Layout.fillWidth: true
        spacing: 8
        TextField {
            id: searchField
            Layout.fillWidth: true
            implicitHeight: 30
            leftPadding: 12
            placeholderText: "Search what you copied"
            placeholderTextColor: theme.placeholder
            color: theme.text
            selectionColor: theme.accent
            font.pixelSize: 13
            text: clipboardHistory.filter
            onTextChanged: clipboardHistory.filter = text
            background: Rectangle { radius: 15; color: theme.well; border.width: 1; border.color: searchField.activeFocus ? theme.accent : theme.outline }
        }
        Text { visible: clipboardHistory.total > 0; text: clipboardHistory.count === clipboardHistory.total ? clipboardHistory.total : clipboardHistory.count + " of " + clipboardHistory.total; color: theme.textDim; font.pixelSize: 11 }
        PillButton { visible: clipboardHistory.total > 0; text: "Clear"; iconName: "trash"; onClicked: clipboardHistory.clear() }
    }

    Text {
        visible: clipboardHistory.enabled && list.count === 0
        Layout.fillWidth: true
        Layout.preferredHeight: 100
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        color: theme.textDim
        font.pixelSize: 13
        text: clipboardHistory.total > 0 ? "Nothing matches." : "Copy something and it shows up here."
    }

    ListView {
        id: list
        visible: clipboardHistory.enabled && count > 0
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 6 * 54)
        clip: true
        spacing: 4
        boundsBehavior: Flickable.StopAtBounds
        model: clipboardHistory
        ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff; padding: 2 }
        delegate: Rectangle {
            id: row
            required property string id
            required property bool isImage
            required property string text
            required property string preview
            required property url url
            required property var time
            required property bool pinned
            width: ListView.view.width - 10
            height: 50
            radius: theme.radiusMd
            color: rowHover.hovered ? theme.hoverFill : theme.well
            border.width: 1
            border.color: row.pinned ? Qt.alpha(theme.accent, 0.5) : theme.hairline
            HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: { clipboardHistory.copy(row.id); page.say("Copied again") } }
            RowLayout {
                anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
                spacing: 10
                Rectangle { // a picture's thumbnail, or a text mark
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    radius: theme.radiusSm
                    color: Qt.alpha(theme.accent, 0.12)
                    clip: true
                    Image { visible: row.isImage; anchors.fill: parent; source: row.isImage ? row.url : ""; sourceSize: Qt.size(68, 68); fillMode: Image.PreserveAspectCrop; asynchronous: true }
                    Text { visible: !row.isImage; anchors.centerIn: parent; text: "Aa"; color: theme.accentText; font.pixelSize: 12; font.weight: Font.DemiBold }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 0
                    Text { Layout.fillWidth: true; text: row.preview; color: theme.text; font.pixelSize: 12; maximumLineCount: 2; wrapMode: Text.Wrap; elide: Text.ElideRight }
                    Text { text: page.ago(row.time) + (row.isImage ? "" : " · " + row.text.length + " characters"); color: theme.textDim; font.pixelSize: 10 }
                }
                IconButton {
                    iconName: "pin"
                    checked: row.pinned
                    Accessible.name: row.pinned ? "Unpin" : "Pin"
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: row.pinned ? "Unpin" : "Pin: keep it from expiring"
                    onClicked: clipboardHistory.pin(row.id, !row.pinned)
                }
                IconButton {
                    iconName: "folder"
                    Accessible.name: "Keep in Files"
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: "Keep in Files"
                    onClicked: { clipboardHistory.keep(row.id); page.say("Kept in Files") }
                }
                IconButton {
                    iconName: "trash"
                    Accessible.name: "Remove"
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: "Remove"
                    onClicked: clipboardHistory.remove(row.id)
                }
            }
        }
    }
}
