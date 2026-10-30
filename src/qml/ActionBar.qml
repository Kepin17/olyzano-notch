import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// What the open notch can show, one section at a time: Files, Music, Timer, Share, and Add (drop files
// is an action too). Picking one sets the page; the buttons that have nothing to show are left out.
RowLayout {
    id: bar
    required property string page
    property bool musicAvailable: false
    property bool shareAvailable: false
    property bool shareBusy: false
    signal pick(string target)
    signal addFiles()
    signal newNote()
    property bool colorAvailable: false
    property int tasksBadge: 0 // overdue tasks
    signal openShares()
    spacing: 6
    // With Music, Color and Sharing all there, the words of Note and Add no longer fit the panel: they shrink to icons.
    readonly property bool tight: (musicAvailable ? 1 : 0) + (colorAvailable ? 1 : 0) + (shareAvailable ? 1 : 0) >= 2

    // The pages as one control: the open one shows its icon, name and count; the others are icons (with their count as a small
    // badge) in parts wide enough to hit. A thumb slides to the open one. Click or drag across, scroll the wheel, or press
    // left/right.
    Item {
        id: slider
        readonly property var pages: [
            { id: "shelf", label: "Files", icon: "folder", badge: 0, on: true },
            { id: "music", label: "Music", icon: "music", badge: 0, on: bar.musicAvailable },
            { id: "timer", label: "Timer", icon: "timer", badge: 0, on: true },
            { id: "tasks", label: "Tasks", icon: "check", badge: bar.tasksBadge, on: true },
            { id: "color", label: "Color", icon: "palette", badge: 0, on: bar.colorAvailable },
            { id: "clipboard", label: "Clipboard", icon: "copy", badge: 0, on: true },
            { id: "camera", label: "Camera", icon: "camera", badge: 0, on: true },
            { id: "dev", label: "Dev", icon: "server", badge: 0, on: true }
        ].filter(p => p.on)
        readonly property int current: { for (let i = 0; i < pages.length; ++i) if (pages[i].id === bar.page) return i; return -1 }
        readonly property Item cur: current >= 0 ? segs.itemAt(current) : null
        function at(x) { // the part under an x position (inside the row)
            for (let i = 0; i < segs.count; ++i) {
                const it = segs.itemAt(i)
                if (x < it.x + it.width)
                    return i
            }
            return pages.length - 1
        }
        function go(i) { if (i !== current && i >= 0) bar.pick(pages[i].id) }
        function step(d) { go(Math.max(0, Math.min(pages.length - 1, current + d))) }
        implicitWidth: segRow.implicitWidth + 6
        implicitHeight: 36
        activeFocusOnTab: true
        Accessible.role: Accessible.PageTabList
        Accessible.name: "Page: " + (current >= 0 ? pages[current].label : "")
        Keys.onLeftPressed: step(-1)
        Keys.onRightPressed: step(1)

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: theme.well
            border.width: 1
            border.color: theme.washi ? theme.outline : theme.hairline
        }
        Rectangle { // the thumb
            visible: slider.cur !== null
            y: 3
            x: slider.cur ? segRow.x + slider.cur.x : 0
            width: slider.cur ? slider.cur.width : 0
            height: slider.height - 6
            radius: height / 2
            color: Qt.alpha(theme.accent, pointer.pressed ? 0.28 : 0.18)
            border.width: 1
            border.color: Qt.alpha(theme.accent, 0.5)
            Behavior on x { enabled: activities.animations; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            Behavior on width { enabled: activities.animations; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        }
        Row {
            id: segRow
            x: 3
            y: 3
            height: slider.height - 6
            Repeater {
                id: segs
                model: slider.pages
                Item {
                    id: part
                    required property var modelData
                    required property int index
                    readonly property bool open: index === slider.current
                    height: segRow.height
                    width: open ? openRow.implicitWidth + 26 : 42
                    Behavior on width { enabled: activities.animations; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    Row {
                        id: openRow
                        anchors.centerIn: parent
                        spacing: 5
                        LineIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: part.modelData.icon
                            sourceSize: Qt.size(14, 14)
                            color: part.open ? theme.accentText : pointerHover.hovered && slider.at(pointerHover.point.position.x - segRow.x) === part.index ? theme.text : theme.textSoft
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: part.open
                            text: part.modelData.label
                            color: theme.accentText
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        Rectangle { // the count, next to the name
                            visible: part.open && part.modelData.badge > 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(16, badgeText.implicitWidth + 8)
                            height: 16
                            radius: theme.radiusSm
                            color: theme.danger
                            Text { id: badgeText; anchors.centerIn: parent; text: part.modelData.badge; color: theme.onColor(theme.danger); font.pixelSize: 10; font.weight: Font.DemiBold }
                        }
                    }
                    Rectangle { // the count on an icon that is not open
                        visible: !part.open && part.modelData.badge > 0
                        x: parent.width / 2 + 3
                        y: 2
                        width: Math.max(14, badgeSmall.implicitWidth + 6)
                        height: 14
                        radius: 7
                        color: theme.danger
                        Text { id: badgeSmall; anchors.centerIn: parent; text: part.modelData.badge; color: theme.onColor(theme.danger); font.pixelSize: 9; font.weight: Font.DemiBold }
                    }
                }
            }
        }
        HoverHandler { id: pointerHover; cursorShape: Qt.PointingHandCursor }
        ToolTip { // the name of the part under the pointer
            readonly property int over: slider.at(pointerHover.point.position.x - segRow.x)
            visible: pointerHover.hovered && !pointer.pressed && over !== slider.current
            delay: 150
            x: segs.itemAt(over) ? segRow.x + segs.itemAt(over).x + segs.itemAt(over).width / 2 - width / 2 : 0
            y: slider.height + 4
            text: slider.pages[over] ? slider.pages[over].label : ""
        }
        MouseArea {
            id: pointer
            anchors.fill: parent
            function follow(m) { slider.go(slider.at(m.x - segRow.x)) }
            onPressed: (m) => { slider.forceActiveFocus(); follow(m) }
            onPositionChanged: (m) => { if (pressed) follow(m) }
            onWheel: (w) => slider.step((w.angleDelta.y || w.angleDelta.x) < 0 ? 1 : -1)
        }
    }
    PillButton { visible: bar.shareAvailable; text: bar.shareBusy ? "Sharing" : "Share"; iconName: "share"; checked: bar.shareBusy; onClicked: bar.openShares() }
    Item { Layout.fillWidth: true; Layout.preferredWidth: 0 }
    PillButton { visible: bar.page === "shelf"; text: "Note"; iconName: "pen"; iconOnly: bar.tight; onClicked: bar.newNote() }
    PillButton { visible: bar.page === "shelf"; text: "Add"; iconName: "plus"; iconOnly: bar.tight; onClicked: bar.addFiles() } // adding files belongs to Files
}
