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
    property var results: []            // the apps that match
    property var content: []            // what was found inside files, recordings and forms (knowledge.globalResults)
    property int current: 0
    signal closeRequested()
    signal openResult(var result)
    spacing: 0

    readonly property var tasksFound: {
        const q = query.trim().toLowerCase()
        if (q === "") return []
        return tasks.list.filter(t => !t.done && (t.title || "").toLowerCase().indexOf(q) >= 0).slice(0, 3).map(t => ({ type: "content", group: "Tasks", kind: "task", title: t.title, subtitle: "", snippet: "", ref: t.id, page: 0, startSec: -1 }))
    }
    // one list for the arrow keys: the apps first, then what was found inside things, each group under its own small title
    readonly property var items: {
        const inside = content.map(c => Object.assign({ type: "content" }, c)).concat(tasksFound)
        const apps = results.slice(0, query === "" ? 6 : inside.length > 0 ? 4 : 7).map(a => Object.assign({ type: "app", group: query === "" ? "RECENT" : "Apps" }, a))
        return apps.concat(inside)
    }
    readonly property int rows: items.length
    function titleAbove(i) { return i === 0 ? (items[0].group === "Apps" && items.every(x => x.type === "app") ? "" : items[0].group) : items[i - 1].group !== items[i].group ? items[i].group : "" }
    // what the island should be as tall as: the field, the rows (taller with a passage under them), the group titles, a little air
    readonly property real wantedHeight: {
        let h = 12 + 46 + 12
        if (rows > 0) {
            h += 8
            for (let i = 0; i < rows; ++i) h += (titleAbove(i) !== "" ? 22 : 0) + (items[i].type === "content" && items[i].snippet !== "" ? 58 : 46)
        } else if (query !== "") h += 8 + 44
        if (!knowledge.enabled && query !== "") h += 28
        return Math.min(h, 540)
    }

    function setQuery(text) { field.text = text }
    function reload() {
        results = query === "" ? launcher.recentApps(6) : launcher.search(query, "").slice(0, 7)
        if (query === "") content = []
        current = 0
    }
    function launchRow(i) {
        if (i < 0 || i >= items.length)
            return
        const it = items[i]
        if (it.type === "app") { launcher.launch(it.id); closeRequested() }
        else openResult(it)
    }
    onQueryChanged: { reload(); searchTimer.restart() }
    onActiveChanged: if (active) { field.text = ""; query = ""; reload(); field.forceActiveFocus() }
    Connections { target: launcher; function onChanged() { if (panel.active) panel.reload() } }
    Connections { target: knowledge; function onGlobalChanged() { if (panel.active && panel.query !== "") { panel.content = knowledge.globalResults; panel.current = Math.min(panel.current, Math.max(0, panel.items.length - 1)) } } }
    Timer { id: searchTimer; interval: 150; onTriggered: knowledge.globalSearch(panel.query) } // not at every key: when the typing pauses

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 46
        spacing: 12
        LineIcon { glyph: "search"; sourceSize: Qt.size(22, 22); color: theme.textDim }
        TextField {
            id: field
            Layout.fillWidth: true
            Layout.fillHeight: true
            placeholderText: "Search apps, files, recordings, forms…"
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
                    panel.current = Math.min(panel.items.length - 1, panel.current + 1); e.accepted = true
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
        visible: panel.query !== "" && panel.rows === 0
        Layout.fillWidth: true
        Layout.topMargin: 8
        Layout.preferredHeight: 44
        verticalAlignment: Text.AlignVCenter
        text: "Nothing found for “" + panel.query + "”"
        color: theme.textDim
        font.pixelSize: 14
    }
    Repeater {
        model: panel.items
        delegate: ColumnLayout {
            id: cell
            required property var modelData
            required property int index
            readonly property bool picked: index === panel.current
            readonly property bool inside: modelData.type === "content"
            readonly property string above: panel.titleAbove(index)
            Layout.fillWidth: true
            spacing: 0
            Text {
                visible: cell.above !== ""
                Layout.topMargin: cell.index === 0 ? 8 : 6
                Layout.preferredHeight: 16
                text: cell.above.toUpperCase()
                color: theme.textFaint
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }
            Rectangle {
                id: row
                Layout.fillWidth: true
                Layout.topMargin: cell.above === "" && cell.index === 0 ? 8 : 0
                Layout.preferredHeight: cell.inside && cell.modelData.snippet !== "" ? 58 : 46
                radius: theme.radiusMd
                color: cell.picked ? theme.accentSoft : rowHover.hovered ? theme.hoverFill : "transparent"
                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor; onHoveredChanged: if (hovered) panel.current = cell.index }
                TapHandler { onTapped: panel.launchRow(cell.index) }
                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 12 }
                    spacing: 12
                    Image {
                        visible: !cell.inside
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        source: cell.inside ? "" : cell.modelData.iconSource
                        sourceSize: Qt.size(60, 60)
                        asynchronous: true
                        fillMode: Image.PreserveAspectFit
                    }
                    Rectangle {
                        visible: cell.inside
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        radius: 9
                        color: theme.accentSoft
                        LineIcon { anchors.centerIn: parent; glyph: cell.modelData.kind === "recording" ? "mic" : cell.modelData.kind === "form" ? "doc" : cell.modelData.kind === "task" ? "check" : "folder"; sourceSize: Qt.size(16, 16); color: theme.accentText }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text { Layout.fillWidth: true; textFormat: Text.PlainText; text: cell.inside ? cell.modelData.title : cell.modelData.name; color: theme.text; font.pixelSize: 15; font.weight: cell.picked ? Font.DemiBold : Font.Medium; elide: Text.ElideRight }
                        Text { visible: cell.inside && cell.modelData.snippet !== ""; Layout.fillWidth: true; textFormat: Text.StyledText; text: cell.modelData.snippet; color: theme.textDim; font.pixelSize: 12; elide: Text.ElideRight; maximumLineCount: 1 }
                    }
                    Text { text: cell.inside ? cell.modelData.subtitle : cell.modelData.category; color: theme.textDim; font.pixelSize: 11 }
                    Text { visible: cell.picked; text: "↵"; color: theme.accentText; font.pixelSize: 16 }
                    Text { visible: !cell.picked && cell.index < 7; text: "Ctrl+" + (cell.index + 1); color: theme.textFaint; font.pixelSize: 10 }
                }
            }
        }
    }
    Text {
        visible: !knowledge.enabled && panel.query !== ""
        Layout.fillWidth: true
        Layout.topMargin: 8
        text: "Turn on search in Settings → Files to find inside your files, recordings and forms."
        color: theme.textFaint
        font.pixelSize: 11
        wrapMode: Text.Wrap
    }
}
