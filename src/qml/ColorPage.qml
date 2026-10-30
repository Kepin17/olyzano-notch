import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Color: pick one from the screen; the last one is shown big with its HEX / RGB / HSL, and the ones before sit below.
// A click on any color copies it in the chosen format; a right-click removes it.
ColumnLayout {
    id: page
    required property var shell
    visible: shell.page === "color"
    Layout.fillWidth: true
    Layout.maximumWidth: 100000
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 14

    property string kind: "hex"
    property string copied: "" // the color just copied, for the check mark
    readonly property var colors: notch.recentColors
    readonly property string current: colors.length > 0 ? colors[0] : ""

    function copy(hex) {
        notch.copyText(colorPicker.format(hex, kind))
        copied = hex
        copiedTimer.restart()
        shell.infoText = "Copied " + colorPicker.format(hex, kind)
        infoResetter.restart()
    }
    Timer { id: copiedTimer; interval: 1400; onTriggered: page.copied = "" }
    Timer { id: infoResetter; interval: 1400; onTriggered: { if (shell.infoText.indexOf("Copied") === 0) shell.infoText = "" } }

    // ---- the last one picked: a big swatch, and its three formats as rows (a click copies that one) ----
    Rectangle {
        visible: page.current !== ""
        Layout.fillWidth: true
        implicitHeight: 128
        radius: theme.radiusLg
        color: theme.well
        border.width: 1
        border.color: theme.hairline
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 150
                radius: theme.radiusMd
                color: page.current !== "" ? page.current : "transparent"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.14)
                HoverHandler { id: heroHover }
                TapHandler { onTapped: page.copy(page.current) }
                Row {
                    anchors { right: parent.right; bottom: parent.bottom; margins: 10 }
                    spacing: 5
                    opacity: heroHover.hovered || page.copied === page.current ? 1 : 0
                    Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 120 } }
                    LineIcon { glyph: page.copied === page.current ? "check" : "copy"; sourceSize: Qt.size(14, 14); color: colorPicker.readableOn(page.current) }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: page.copied === page.current ? "Copied" : "Copy"; color: colorPicker.readableOn(page.current); font.pixelSize: 11; font.weight: Font.DemiBold }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4
                Repeater {
                    model: [["hex", "HEX"], ["rgb", "RGB"], ["hsl", "HSL"]]
                    Rectangle {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: theme.radiusSm
                        readonly property bool chosen: page.kind === modelData[0]
                        color: chosen ? theme.accentSoft : rowHover.hovered ? theme.surfaceHover : "transparent"
                        HoverHandler { id: rowHover }
                        TapHandler { onTapped: { page.kind = row.modelData[0]; page.copy(page.current) } }
                        Text {
                            x: 12; anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData[1]
                            color: row.chosen ? theme.accent : theme.textDim
                            font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8
                        }
                        Text {
                            x: 52; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 52 - 30
                            text: page.current !== "" ? colorPicker.format(page.current, row.modelData[0]) : ""
                            color: theme.text
                            font.pixelSize: 13; font.family: "monospace"
                            elide: Text.ElideRight
                        }
                        LineIcon {
                            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                            glyph: page.copied === page.current && row.chosen ? "check" : "copy"
                            sourceSize: Qt.size(13, 13)
                            color: row.chosen ? theme.accent : theme.textFaint
                            opacity: rowHover.hovered || row.chosen ? 1 : 0
                        }
                    }
                }
            }
        }
    }

    // ---- the ones before, and the picker ----
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        SectionLabel { visible: page.colors.length > 1; text: "Recent" }
        Caption {
            Layout.fillWidth: true
            visible: page.colors.length > 1 || !colorPicker.supported
            text: !colorPicker.supported ? "Picking a color from the screen isn't available here." : page.colors.length - 1 + (page.colors.length === 2 ? " color" : " colors")
        }
        Item { Layout.fillWidth: true; visible: page.colors.length <= 1 && colorPicker.supported }
        PillButton {
            visible: colorPicker.supported
            text: "Pick from screen"
            iconName: "eyedropper"
            checked: true
            implicitHeight: 34
            implicitWidth: 160
            onClicked: {
                // Get out of the way so anything on screen (even under Notch) can be picked
                // (not in "Always open": it would not come back).
                if (shell.capsuleMode !== "open") {
                    shell.pinned = false
                    shell.open = false
                }
                colorPicker.pick()
            }
        }
    }
    Caption {
        visible: page.colors.length === 0
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: "No colors yet. Pick one from the screen (the panel hides, click a pixel anywhere, Esc cancels): it is kept here, and a click copies it."
    }
    Flickable { // two rows show; more scroll
        id: history
        visible: page.colors.length > 1
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(grid.implicitHeight, 84)
        contentWidth: width
        contentHeight: grid.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: history.contentHeight > history.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        Flow {
            id: grid
            width: history.width - 12
            spacing: 8
        Repeater {
            model: page.colors.slice(1)
            Rectangle {
                id: sw
                required property string modelData
                width: 38; height: 38
                radius: theme.radiusMd
                color: modelData
                border.width: swHover.hovered ? 2 : 1
                border.color: swHover.hovered ? theme.text : theme.hairline
                scale: swHover.hovered ? 1.06 : 1
                Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 120 } }
                HoverHandler { id: swHover }
                ToolTip.visible: swHover.hovered
                ToolTip.delay: 300
                ToolTip.text: colorPicker.format(sw.modelData, page.kind) + " · click to copy · right-click to remove"
                TapHandler { onTapped: page.copy(sw.modelData) }
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: notch.removeColor(sw.modelData) }
                LineIcon {
                    anchors.centerIn: parent
                    visible: page.copied === sw.modelData
                    glyph: "check"
                    color: colorPicker.readableOn(sw.modelData)
                    sourceSize: Qt.size(18, 18)
                }
            }
        }
        }
    }
    Item { Layout.preferredHeight: 2 }
}
