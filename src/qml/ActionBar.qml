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
    property bool lyricsOn: false
    property bool colorAvailable: false
    property int tasksBadge: 0 // overdue tasks
    signal openShares()
    signal toggleLyrics()
    spacing: 6
    // With Music, Color and Sharing all there, the words of Note and Add no longer fit the panel: they shrink to icons.
    readonly property bool tight: (musicAvailable ? 1 : 0) + (colorAvailable ? 1 : 0) + (shareAvailable ? 1 : 0) >= 2

    // One track, one thumb that slides to the page in front: the open one shows its name, the rest are icons.
    // Click, drag along the track, scroll the wheel or press left/right to go from page to page.
    Item {
        id: slider
        readonly property var pages: [
            { id: "shelf", label: "Files", icon: "folder", on: true },
            { id: "music", label: "Music", icon: "music", on: bar.musicAvailable },
            { id: "timer", label: "Timer", icon: "timer", on: true },
            { id: "tasks", label: bar.tasksBadge > 0 ? "Tasks · " + bar.tasksBadge : "Tasks", icon: "check", on: true },
            { id: "color", label: "Color", icon: "palette", on: bar.colorAvailable }
        ].filter(p => p.on)
        readonly property int current: { for (let i = 0; i < pages.length; ++i) if (pages[i].id === bar.page) return i; return -1 }
        readonly property Item cur: current >= 0 ? segs.itemAt(current) : null
        implicitHeight: 32
        implicitWidth: segRow.implicitWidth + 6
        activeFocusOnTab: true
        Accessible.role: Accessible.PageTabList
        Keys.onLeftPressed: step(-1)
        Keys.onRightPressed: step(1)
        function step(d) {
            const i = Math.max(0, Math.min(pages.length - 1, current + d))
            if (i !== current) bar.pick(pages[i].id)
        }
        function at(x) { // the page under an x position
            for (let i = 0; i < segs.count; ++i) {
                const it = segs.itemAt(i)
                if (x < it.x + it.width + 3) return i
            }
            return pages.length - 1
        }

        Rectangle {
            anchors.fill: parent
            radius: theme.washi ? theme.chipRadius : theme.radiusSm + 2
            color: theme.well
            border.width: 1
            border.color: theme.washi ? theme.outline : theme.hairline
        }
        Rectangle { // the thumb
            visible: slider.cur !== null
            x: slider.cur ? segRow.x + slider.cur.x : 0
            y: 3
            width: slider.cur ? slider.cur.width : 0
            height: slider.height - 6
            radius: theme.washi ? Math.max(2, theme.chipRadius - 2) : theme.radiusSm
            color: Qt.alpha(theme.accent, 0.18)
            border.width: 1
            border.color: Qt.alpha(theme.accent, 0.45)
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
                    id: seg
                    required property var modelData
                    required property int index
                    readonly property bool open: index === slider.current
                    height: segRow.height
                    width: segContent.implicitWidth + (open ? 22 : 24)
                    Behavior on width { enabled: activities.animations; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    Row {
                        id: segContent
                        anchors.centerIn: parent
                        spacing: 5
                        LineIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: seg.modelData.icon
                            sourceSize: Qt.size(14, 14)
                            color: seg.open ? theme.accentText : segHover.hovered ? theme.text : theme.textSoft
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: seg.open
                            text: seg.modelData.label
                            color: theme.accentText
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }
                    HoverHandler { id: segHover }
                    Accessible.role: Accessible.PageTab
                    Accessible.name: seg.modelData.label
                    ToolTip.visible: !seg.open && segHover.hovered
                    ToolTip.delay: 400
                    ToolTip.text: seg.modelData.label
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            function go(x) {
                const i = slider.at(x - segRow.x)
                if (i !== slider.current) bar.pick(slider.pages[i].id)
            }
            onPressed: (m) => { slider.forceActiveFocus(); go(m.x) }
            onPositionChanged: (m) => { if (pressed) go(m.x) }
            onWheel: (w) => slider.step((w.angleDelta.y || w.angleDelta.x) < 0 ? 1 : -1)
        }
    }
    PillButton { visible: bar.shareAvailable; text: bar.shareBusy ? "Sharing" : "Share"; iconName: "share"; checked: bar.shareBusy; onClicked: bar.openShares() }
    Item { Layout.fillWidth: true }
    PillButton { visible: bar.page === "music"; text: "Lyrics"; checked: bar.lyricsOn; onClicked: bar.toggleLyrics() }
    PillButton { visible: bar.page === "shelf"; text: "Note"; iconName: "pen"; iconOnly: bar.tight; onClicked: bar.newNote() }
    PillButton { visible: bar.page === "shelf"; text: "Add"; iconName: "plus"; iconOnly: bar.tight; onClicked: bar.addFiles() } // adding files belongs to Files
}
