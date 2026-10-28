import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Search from the notch (like Spotlight): Shift + Space opens the notch as a search field with the apps that match under it. Typing
// ranks them (the launcher's own ranking and use counts); with nothing typed it lists the ones opened lately. Up and down move,
// Enter opens the highlighted one, Ctrl+1 to Ctrl+7 open a row at once, Esc closes.
ColumnLayout {
    id: panel
    property bool active: false
    property string query: ""
    property var results: []
    property int current: 0
    readonly property int rows: results.length
    // what the island should be as tall as: the field, the rows, a little air
    readonly property real wantedHeight: 12 + 46 + (rows > 0 ? 8 + (query === "" ? 20 : 0) + rows * 46 : query !== "" ? 8 + 44 : 0) + 12
    signal closeRequested()
    spacing: 0

    function setQuery(text) { field.text = text }
    function reload() {
        results = query === "" ? launcher.recentApps(6) : launcher.search(query, "").slice(0, 7)
        current = 0
    }
    function launchRow(i) {
        if (i < 0 || i >= results.length)
            return
        launcher.launch(results[i].id)
        closeRequested()
    }
    onQueryChanged: reload()
    onActiveChanged: if (active) { field.text = ""; query = ""; reload(); field.forceActiveFocus() }
    Connections { target: launcher; function onChanged() { if (panel.active) panel.reload() } }

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 46
        spacing: 12
        LineIcon { glyph: "search"; sourceSize: Qt.size(22, 22); color: theme.textDim }
        TextField {
            id: field
            Layout.fillWidth: true
            Layout.fillHeight: true
            placeholderText: "Search apps"
            placeholderTextColor: theme.placeholder
            color: theme.text
            selectionColor: theme.accent
            font.pixelSize: 22
            font.weight: Font.Light
            leftPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            background: null
            onTextChanged: panel.query = text
            Keys.onPressed: (e) => {
                if (e.key === Qt.Key_Down || (e.key === Qt.Key_N && (e.modifiers & Qt.ControlModifier))) {
                    panel.current = Math.min(panel.results.length - 1, panel.current + 1); e.accepted = true
                } else if (e.key === Qt.Key_Up || (e.key === Qt.Key_P && (e.modifiers & Qt.ControlModifier))) {
                    panel.current = Math.max(0, panel.current - 1); e.accepted = true
                } else if (e.key === Qt.Key_Escape) {
                    panel.closeRequested(); e.accepted = true
                } else if ((e.modifiers & Qt.ControlModifier) && e.key >= Qt.Key_1 && e.key <= Qt.Key_7) {
                    panel.launchRow(e.key - Qt.Key_1); e.accepted = true
                }
            }
            onAccepted: panel.launchRow(panel.current)
        }
    }
    Rectangle { visible: panel.rows > 0 || panel.query !== ""; Layout.fillWidth: true; Layout.topMargin: 0; implicitHeight: 1; color: theme.hairline }
    Text {
        visible: panel.query === "" && panel.rows > 0
        Layout.topMargin: 8
        Layout.preferredHeight: 20
        text: "RECENT"
        color: theme.textFaint
        font.pixelSize: 10
        font.weight: Font.DemiBold
        font.letterSpacing: 1
    }
    Text {
        visible: panel.query !== "" && panel.rows === 0
        Layout.fillWidth: true
        Layout.topMargin: 8
        Layout.preferredHeight: 44
        verticalAlignment: Text.AlignVCenter
        text: "No app called “" + panel.query + "”"
        color: theme.textDim
        font.pixelSize: 14
    }
    Repeater {
        model: panel.results
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            readonly property bool picked: index === panel.current
            Layout.fillWidth: true
            Layout.topMargin: index === 0 && panel.query !== "" ? 8 : 0
            Layout.preferredHeight: 46
            radius: theme.radiusMd
            color: picked ? theme.accentSoft : rowHover.hovered ? theme.hoverFill : "transparent"
            HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor; onHoveredChanged: if (hovered) panel.current = row.index }
            TapHandler { onTapped: panel.launchRow(row.index) }
            RowLayout {
                anchors { fill: parent; leftMargin: 10; rightMargin: 12 }
                spacing: 12
                Image {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    source: row.modelData.iconSource
                    sourceSize: Qt.size(60, 60)
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                }
                Text { Layout.fillWidth: true; text: row.modelData.name; color: theme.text; font.pixelSize: 15; font.weight: row.picked ? Font.DemiBold : Font.Medium; elide: Text.ElideRight }
                Text { text: row.modelData.category; color: theme.textDim; font.pixelSize: 11 }
                Text { visible: row.picked; text: "↵"; color: theme.accentText; font.pixelSize: 16 }
                Text { visible: !row.picked && row.index < 7; text: "Ctrl+" + (row.index + 1); color: theme.textFaint; font.pixelSize: 10 }
            }
        }
    }
}
