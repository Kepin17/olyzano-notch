import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Mac-style dock: pinned apps, a divider, then the other running apps (dot underneath).
// Click opens/raises, right-click pins or unpins, + picks from the installed apps.
Item {
    id: bar
    readonly property bool overlayOpen: appMenu.visible || picker.visible
    implicitHeight: 54

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width, row.width + 20)
        height: 50
        radius: theme.washi ? theme.chipRadius : 18
        color: theme.surface
        border.width: theme.washi ? 1 : 0
        border.color: theme.outline

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 4

            Repeater {
                model: dock.items
                delegate: Item {
                    id: slot
                    required property var modelData
                    required property int index
                    readonly property bool divider: index > 0 && !modelData.pinned && dock.items[index - 1].pinned
                    width: 38 + (divider ? 9 : 0)
                    height: 46

                    Rectangle {
                        visible: slot.divider
                        x: 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: 1
                        height: 30
                        color: theme.outline
                    }

                    Item {
                        id: tile
                        x: slot.divider ? 9 : 0
                        width: 38
                        height: parent.height

                        Image {
                            id: icon
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 4
                            width: 32
                            height: 32
                            source: slot.modelData.iconSource
                            sourceSize: Qt.size(64, 64)
                            transformOrigin: Item.Bottom
                            scale: tap.containsMouse ? 1.25 : 1
                            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                        }
                        Rectangle {
                            visible: slot.modelData.running
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 40
                            width: 4
                            height: 4
                            radius: 2
                            color: slot.modelData.active ? theme.accent : theme.textDim
                        }
                        MouseArea {
                            id: tap
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: (mouse) => {
                                if (mouse.button === Qt.RightButton) {
                                    appMenu.appId = slot.modelData.id
                                    appMenu.appPinned = slot.modelData.pinned
                                    appMenu.popup()
                                } else {
                                    dock.launch(slot.modelData.id)
                                }
                            }
                        }
                        ToolTip {
                            visible: tap.containsMouse
                            delay: 400
                            text: slot.modelData.name
                            font.pixelSize: 11
                            contentItem: Text { text: parent.text; font: parent.font; color: theme.text }
                            background: Rectangle { radius: 8; color: theme.surface; border.color: theme.outline }
                        }
                    }
                }
            }

            Item {
                width: 30
                height: 46
                IconButton {
                    anchors.centerIn: parent
                    iconName: "plus"
                    onClicked: {
                        picker.filter = ""
                        picker.open()
                    }
                }
            }
        }
    }

    DarkMenu {
        id: appMenu
        property string appId
        property bool appPinned
        MenuItem { text: "Open"; onTriggered: dock.launch(appMenu.appId) }
        MenuItem {
            text: appMenu.appPinned ? "Unpin from dock" : "Pin to dock"
            onTriggered: appMenu.appPinned ? dock.unpin(appMenu.appId) : dock.pin(appMenu.appId)
        }
    }

    Popup {
        id: picker
        property string filter
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: 90
        width: 320
        height: 360
        padding: 12
        focus: true
        onOpened: search.forceActiveFocus()
        background: Rectangle { radius: 16; color: theme.surface; border.color: theme.outline }
        contentItem: ColumnLayout {
            spacing: 8
            SectionLabel { text: "Pin an app" }
            TextField {
                id: search
                Layout.fillWidth: true
                implicitHeight: 30
                leftPadding: 12
                placeholderText: "Search apps"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 13
                background: Rectangle { radius: 15; color: theme.well; border.width: 1; border.color: theme.accent }
                onTextChanged: picker.filter = text
                Keys.onEscapePressed: picker.close()
            }
            ListView {
                id: apps
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // Re-queried on every keystroke and after each pin so the check marks stay right.
                property int revision: 0
                model: { revision; return dock.installedApps(picker.filter) }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: ItemDelegate {
                    id: entry
                    required property var modelData
                    width: apps.width
                    height: 36
                    padding: 6
                    onClicked: {
                        modelData.pinned ? dock.unpin(modelData.id) : dock.pin(modelData.id)
                        apps.revision++
                    }
                    background: Rectangle { radius: 8; color: entry.hovered ? theme.surfaceHover : "transparent" }
                    contentItem: RowLayout {
                        spacing: 10
                        Image {
                            source: entry.modelData.iconSource
                            sourceSize: Qt.size(48, 48)
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                        }
                        Text {
                            Layout.fillWidth: true
                            text: entry.modelData.name
                            color: theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: entry.modelData.pinned
                            text: "✓"
                            color: theme.accent
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }
    }
}
