import QtQuick
import QtQuick.Controls.Basic

// One level of an app's menu as a glass card: rows with a check / radio mark, the label, the shortcut (or a chevron for a
// submenu, which opens as another card beside this one, and so on down). Items come from `appMenu` (see DbusMenuModel.h).
Rectangle {
    id: pop
    property var items: []
    property bool lead: false // the level under the bar: submenus beside it open to the right unless there is no room
    readonly property real maxH: 740
    property int openSub: -1
    signal closeAll()

    // how far this card and its open submenus reach, for the pointer mask of the bar window
    readonly property real reachW: width + (sub.item ? sub.item.reachW - 4 : 0)
    readonly property real reachH: Math.max(height, sub.item ? sub.y + sub.item.reachH : 0)
    readonly property bool flip: x !== undefined && mapToItem(null, 0, 0).x + width * 2 > Window.width

    width: 300
    height: Math.min(col.height + 12, maxH)
    radius: 12
    color: theme.dark ? Qt.rgba(0.09, 0.09, 0.10, Math.min(1, topbar.opacity + 0.25)) : Qt.rgba(1, 1, 1, Math.min(1, topbar.opacity + 0.25))
    border.width: 1
    border.color: theme.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.12)

    Timer { id: subTimer; interval: 140; property int want: -1; onTriggered: pop.showSub(want) }
    function showSub(i) {
        if (i === openSub) return
        openSub = i
        if (i >= 0) appMenu.aboutToShow(items[i].id)
    }

    Flickable {
        id: flick
        anchors { fill: parent; topMargin: 6; bottomMargin: 6 }
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: col
            width: flick.width
            Repeater {
                model: pop.items
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    width: col.width
                    height: modelData.separator ? 9 : 30
                    Rectangle {
                        visible: row.modelData.separator
                        anchors { left: parent.left; right: parent.right; leftMargin: 12; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        height: 1
                        color: theme.outline
                    }
                    Rectangle {
                        visible: !row.modelData.separator
                        anchors { fill: parent; leftMargin: 5; rightMargin: 5 }
                        radius: 8
                        color: rowHover.hovered && row.modelData.enabled || pop.openSub === row.index ? theme.accentSoft : "transparent"
                        Text { // check / radio
                            x: 9; anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.check ? "✓" : row.modelData.radio ? "•" : ""
                            color: theme.text
                            font.pixelSize: 13
                        }
                        Text {
                            anchors { left: parent.left; leftMargin: 28; right: tail.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            text: row.modelData.label
                            color: row.modelData.enabled ? theme.text : theme.textFaint
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        Text {
                            id: tail
                            visible: !row.modelData.submenu
                            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            text: row.modelData.shortcut
                            color: theme.textDim
                            font.pixelSize: 12
                        }
                        LineIcon {
                            visible: row.modelData.submenu
                            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                            glyph: "chevron"; sourceSize: Qt.size(14, 14); color: theme.textDim
                            rotation: -90
                        }
                        HoverHandler {
                            id: rowHover
                            onHoveredChanged: if (hovered) { subTimer.want = row.modelData.submenu && row.modelData.enabled ? row.index : -1; subTimer.restart() }
                        }
                        TapHandler {
                            enabled: row.modelData.enabled
                            onTapped: {
                                if (row.modelData.submenu) { pop.showSub(row.index); return }
                                appMenu.trigger(row.modelData.id)
                                pop.closeAll()
                            }
                        }
                    }
                }
            }
        }
    }

    Loader {
        id: sub
        active: pop.openSub >= 0
        x: pop.flip ? -item.width + 4 : pop.width - 4
        y: {
            if (pop.openSub < 0) return 0
            let top = 6 - flick.contentY
            for (let i = 0; i < pop.openSub; ++i) top += pop.items[i].separator ? 9 : 30
            return Math.max(0, top - 6)
        }
        source: "AppMenuPopup.qml" // by file: a component can't name itself
        onLoaded: {
            item.items = Qt.binding(() => pop.openSub >= 0 ? pop.items[pop.openSub].children : [])
            item.closeAll.connect(pop.closeAll)
        }
    }
}
