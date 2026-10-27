import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The app launcher: a glass card under the bar's grid button. A search field on top (typing ranks the apps, Enter opens
// the first or the highlighted one, arrows move, Esc closes); without a query: the pinned apps, the recent ones, and all
// of them A-Z with the groups (Internet, Media...) as filters. Grid or list, icon size and the rest: Settings → Panel.
Rectangle {
    id: card
    property bool open: false
    property string query: ""
    property string category: ""
    property int current: 0
    property bool moved: false // the arrows were used: show the highlight
    signal closeRequested()

    readonly property bool grid: topbar.launcherView === "grid"
    readonly property int cell: topbar.launcherIcon + 34
    readonly property int columns: grid ? Math.max(3, Math.floor((width - 28) / cell)) : 1
    property var results: []
    property var pinnedList: []
    property var recentList: []
    property var groups: []
    function reload() {
        results = launcher.search(query, category)
        pinnedList = query === "" ? launcher.pinnedApps() : []
        recentList = query === "" && topbar.launcherRecent ? launcher.recentApps(8) : []
        groups = launcher.categories
        current = 0
        moved = false
    }
    onQueryChanged: reload()
    onCategoryChanged: reload()
    Connections { target: launcher; function onChanged() { card.reload() } }
    Connections { target: topbar; function onChanged() { card.reload() } }
    onOpenChanged: {
        if (open) { query = ""; category = ""; field.text = ""; reload(); field.forceActiveFocus() }
    }
    function launchApp(id) {
        launcher.launch(id)
        closeRequested()
    }

    width: 480
    readonly property bool hint: launcher.superKeyAvailable && !launcher.superKey
    implicitHeight: Math.min(560, 62 + scroller.contentHeight + 14 + (hint ? 40 : 0))
    height: implicitHeight
    radius: theme.radiusLg
    color: theme.dark ? Qt.rgba(0.09, 0.09, 0.10, Math.min(1, topbar.opacity + 0.25)) : Qt.rgba(1, 1, 1, Math.min(1, topbar.opacity + 0.25))
    border.width: 1
    border.color: theme.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.12)
    opacity: open ? 1 : 0
    visible: opacity > 0
    scale: open ? 1 : 0.97
    transformOrigin: Item.TopLeft
    Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 130 } }
    Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

    // one app: an icon over its name (grid), or the icon beside it (list); the pin shows on hover
    component AppTile: Item {
        id: tile
        required property var app
        property bool highlighted: false
        width: card.grid ? card.cell : parent ? parent.width : card.width
        height: card.grid ? topbar.launcherIcon + 38 : 40
        HoverHandler { id: tileHover }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: theme.radiusMd
            color: tile.highlighted ? theme.accentSoft : tileHover.hovered ? theme.surfaceHover : "transparent"
            border.width: tile.highlighted ? 1 : 0
            border.color: theme.accent
        }
        Image {
            id: ic
            x: card.grid ? (tile.width - width) / 2 : 12
            y: card.grid ? 6 : (tile.height - height) / 2
            width: card.grid ? topbar.launcherIcon : 28
            height: width
            source: tile.app.iconSource
            sourceSize: Qt.size(width * 2, height * 2)
            fillMode: Image.PreserveAspectFit
            smooth: true
            scale: tileHover.hovered ? 1.06 : 1
            Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 120 } }
        }
        Text {
            x: card.grid ? 4 : 52
            y: card.grid ? ic.y + ic.height + 3 : 0
            width: card.grid ? tile.width - 8 : tile.width - 52 - 40
            height: card.grid ? 28 : tile.height
            text: tile.app.name
            color: theme.text
            font.pixelSize: card.grid ? 11 : 13
            horizontalAlignment: card.grid ? Text.AlignHCenter : Text.AlignLeft
            verticalAlignment: card.grid ? Text.AlignTop : Text.AlignVCenter
            wrapMode: card.grid ? Text.Wrap : Text.NoWrap
            maximumLineCount: card.grid ? 2 : 1
            elide: Text.ElideRight
        }
        AbstractButton { // pin / unpin
            visible: tileHover.hovered || tile.app.pinned
            opacity: tileHover.hovered ? 1 : 0.5
            x: tile.width - width - 6
            y: card.grid ? 4 : (tile.height - height) / 2
            width: 22; height: 22
            Accessible.name: tile.app.pinned ? "Unpin" : "Pin"
            onClicked: launcher.setPinned(tile.app.id, !tile.app.pinned)
            background: Rectangle { radius: 11; color: parent.hovered ? theme.surfacePressed : "transparent" }
            contentItem: Item { LineIcon { anchors.centerIn: parent; glyph: "pin"; sourceSize: Qt.size(13, 13); color: tile.app.pinned ? theme.accent : theme.textDim } }
        }
        TapHandler { onTapped: card.launchApp(tile.app.id) }
    }
    component Heading: Text {
        Layout.fillWidth: true
        Layout.topMargin: 6
        color: theme.textDim
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.6
        leftPadding: 4
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        TextField {
            id: field
            Layout.fillWidth: true
            implicitHeight: 38
            leftPadding: 38
            rightPadding: 14
            placeholderText: "Search apps"
            placeholderTextColor: theme.placeholder
            color: theme.text
            selectionColor: theme.accent
            font.pixelSize: 14
            background: Rectangle {
                radius: 19
                color: theme.well
                border.width: 1
                border.color: field.activeFocus ? theme.accent : theme.outline
                LineIcon { x: 13; anchors.verticalCenter: parent.verticalCenter; glyph: "search"; sourceSize: Qt.size(16, 16); color: theme.textDim }
            }
            onTextChanged: card.query = text
            Keys.onEscapePressed: card.closeRequested()
            Keys.onReturnPressed: if (card.results.length > 0) card.launchApp(card.results[Math.min(card.current, card.results.length - 1)].id)
            Keys.onEnterPressed: Keys.onReturnPressed(event)
            Keys.onDownPressed: { card.moved = true; card.current = Math.min(card.results.length - 1, card.current + card.columns) }
            Keys.onUpPressed: { card.moved = true; card.current = Math.max(0, card.current - card.columns) }
            Keys.onRightPressed: (e) => { if (cursorPosition === text.length) { card.moved = true; card.current = Math.min(card.results.length - 1, card.current + 1) } else e.accepted = false }
            Keys.onLeftPressed: (e) => { if (card.moved && card.current > 0) card.current -= 1; else e.accepted = false }
        }

        RowLayout { // the Windows key, until it is switched on
            visible: card.hint
            Layout.fillWidth: true
            spacing: 8
            Text { Layout.fillWidth: true; text: "Open this with the Windows key"; color: theme.textDim; font.pixelSize: 11; elide: Text.ElideRight }
            PillButton { text: "Turn on"; onClicked: launcher.superKey = true }
        }
        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: content
                width: scroller.width
                spacing: 4

                Heading { visible: card.pinnedList.length > 0; text: "Pinned" }
                Flow {
                    visible: card.pinnedList.length > 0
                    Layout.fillWidth: true
                    Repeater { model: card.pinnedList; AppTile { required property var modelData; app: modelData } }
                }
                Heading { visible: card.recentList.length > 0; text: "Recent" }
                Flow {
                    visible: card.recentList.length > 0
                    Layout.fillWidth: true
                    Repeater { model: card.recentList; AppTile { required property var modelData; app: modelData } }
                }

                Heading { text: card.query !== "" ? (card.results.length + (card.results.length === 1 ? " result" : " results")) : "All apps" }
                Flow { // the groups, as filters
                    visible: topbar.launcherCategories && card.query === "" && card.groups.length > 1
                    Layout.fillWidth: true
                    Layout.bottomMargin: 4
                    spacing: 6
                    Repeater {
                        model: [""].concat(card.groups)
                        PillButton {
                            required property string modelData
                            text: modelData === "" ? "All" : modelData
                            checked: card.category === modelData
                            onClicked: card.category = modelData
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    Repeater {
                        model: card.results
                        AppTile {
                            required property var modelData
                            required property int index
                            app: modelData
                            highlighted: index === card.current && (card.query !== "" || card.moved)
                        }
                    }
                }
                Text {
                    visible: card.results.length === 0
                    Layout.fillWidth: true
                    Layout.topMargin: 14
                    Layout.bottomMargin: 14
                    horizontalAlignment: Text.AlignHCenter
                    text: "No app matches “" + card.query + "”."
                    color: theme.textDim
                    font.pixelSize: 12
                }
            }
        }
    }
}
